/* XMRig
 * Copyright (c) 2018-2019 tevador     <tevador@gmail.com>
 * Copyright (c) 2018-2020 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2020 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "crypto/randomx/randomx.h"
#include "backend/cpu/Cpu.h"
#include "crypto/rx/RxAlgo.h"
#include "crypto/rx/RxCache.h"
#include "crypto/rx/RxDataset.h"
#include "crypto/rx/RxVm.h"
#include "base/io/log/Log.h"
#include "base/io/log/Tags.h"


#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <thread>


#ifdef XMRIG_RX_TEST_HOOKS
#   include <atomic>
#   include <cstdlib>


// Starts from XMRIG_TEST_FAIL_VM_CREATES so that a whole miner built with the hook can be exercised.
static std::atomic<uint32_t> failCreates{ static_cast<uint32_t>(std::getenv("XMRIG_TEST_FAIL_VM_CREATES") ? std::strtoul(std::getenv("XMRIG_TEST_FAIL_VM_CREATES"), nullptr, 10) : 0) };


void xmrig::RxVm::failNextCreates(uint32_t count)
{
    failCreates = count;
}
#endif


static randomx_vm *createVm(randomx_flags flags, randomx_cache *cache, randomx_dataset *dataset, uint8_t *scratchpad, uint32_t node)
{
#   ifdef XMRIG_RX_TEST_HOOKS
    uint32_t pending = failCreates.load();
    while (pending > 0) {
        if (failCreates.compare_exchange_weak(pending, pending - 1)) {
            return nullptr;
        }
    }
#   endif

    return randomx_create_vm(flags, cache, dataset, scratchpad, node);
}


randomx_vm *xmrig::RxVm::create(RxDataset *dataset, uint8_t *scratchpad, bool softAes, const Assembly &assembly, uint32_t node)
{
    int flags = 0;

    // On RISC-V, force software AES path even if CPU reports AES capability.
    // The RandomX portable intrinsics will throw at runtime when HAVE_AES is not defined
    // for this architecture. Until native AES intrinsics are wired for RISC-V, avoid
    // setting HARD_AES to prevent "Platform doesn't support hardware AES" aborts.
#   ifndef XMRIG_RISCV
    if (!softAes) {
       flags |= RANDOMX_FLAG_HARD_AES;
    }
#   else
    (void)softAes; // unused on RISC-V to force soft AES
#   endif

    if (dataset->get()) {
        flags |= RANDOMX_FLAG_FULL_MEM;
    }

    if (!dataset->cache() || dataset->cache()->isJIT()) {
        flags |= RANDOMX_FLAG_JIT;
    }

#   ifdef XMRIG_RISCV
    if (RandomX_CurrentConfig.Tweak_V2_AES) { flags &= ~RANDOMX_FLAG_JIT; }
#   endif

    const auto asmId = assembly == Assembly::AUTO ? Cpu::info()->assembly() : assembly.id();
    if ((asmId == Assembly::RYZEN) || (asmId == Assembly::BULLDOZER)) {
        flags |= RANDOMX_FLAG_AMD;
    }

    randomx_cache *cache = !dataset->get() ? dataset->cache()->get() : nullptr;

    for (uint32_t attempt = 1; attempt <= kCreateAttempts; ++attempt) {
        randomx_vm *vm = createVm(static_cast<randomx_flags>(flags), cache, dataset->get(), scratchpad, node);
        if (vm) {
            return vm;
        }

        if (attempt < kCreateAttempts) {
            LOG_WARN("%s " YELLOW_BOLD("failed to create RandomX VM (attempt %u of %u), retrying"), Tags::randomx(), attempt, kCreateAttempts);
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
        }
    }

    LOG_ERR("%s " RED_BOLD("failed to create RandomX VM after %u attempts"), Tags::randomx(), kCreateAttempts);

    return nullptr;
}


void xmrig::RxVm::destroy(randomx_vm* vm)
{
    if (vm) {
        randomx_destroy_vm(vm);
    }
}


const char *xmrig::RxVm::unsupported(Algorithm::Id algorithm, const RxDataset *dataset)
{
    // The same full-memory and JIT decisions as create().
    const bool fullMem = dataset->get() != nullptr;
    const bool jit     = !dataset->cache() || dataset->cache()->isJIT();

    return unsupported(algorithm, fullMem, jit, dataset->mode() == RxConfig::LightMode);
}


const char *xmrig::RxVm::unsupported(Algorithm::Id algorithm, bool fullMem, bool jit, bool lightByConfig)
{
    if (!RxAlgo::isFastJitOnly(algorithm)) {
        return nullptr;
    }

    if (!fullMem) {
        return lightByConfig ? "RandomX runs in light mode because the config asks for it (\"randomx\": {\"mode\": \"light\"} or --randomx-mode=light); set the mode to auto"
                             : "RandomX runs in light mode because the 2080 MB dataset could not be allocated; free more RAM";
    }

    if (!jit) {
        return "the RandomX JIT is not available (the system refused executable memory); allow JIT code for xmrig-zecnero";
    }

    return nullptr;
}


bool xmrig::RxVm::isSupported(const Algorithm &algorithm, const RxDataset *dataset, uint64_t height)
{
    // Reset whenever something else is hashed, so each switch to a refused
    // algorithm logs once and not once per thread or per job.
    static std::atomic<bool> refusalLogged{ false };
    static std::atomic<bool> warned{ false };

    const char *reason = unsupported(algorithm, dataset);
    if (!reason) {
        refusalLogged = false;

        if (algorithm == Algorithm::RX_ZECNERO) {
            const char *ahead = unsupported(Algorithm::RX_ZECNERO2, dataset);
            if (ahead && !warned.exchange(true)) {
                LOG_WARN("%s" YELLOW_BOLD("rx/zecnero2 (NU2) will not be mined on this machine: %s, and restart before NU2.")
                         YELLOW(" This version mines RandomX v2 only in fast mode with the JIT, which needs about 2.3 GB of free RAM and an OS that allows JIT code; without them it stops hashing at the NU2 height."),
                         Tags::randomx(), ahead);
            }
        }

        return true;
    }

    if (!refusalLogged.exchange(true)) {
        char where[48] = "NU2";
        if (height) {
            snprintf(where, sizeof(where), "NU2, block %" PRIu64, height);
        }

        LOG_ERR("%s" RED_BOLD("%s (%s) is not mined: %s, and restart, or update xmrig-zecnero.")
                RED(" This version mines RandomX v2 only in fast mode with the JIT, which needs about 2.3 GB of free RAM and an OS that allows JIT code; no shares are sent until then."),
                Tags::randomx(), algorithm.name(), where, reason);
    }

    return false;
}
