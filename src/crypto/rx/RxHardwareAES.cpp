/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crypto/rx/RxHardwareAES.h"
#include "backend/cpu/Cpu.h"
#include "base/io/log/Log.h"
#include "base/io/log/Tags.h"
#include "crypto/common/VirtualMemory.h"

#include <atomic>
#include <cstring>
#include <mutex>

#ifdef XMRIG_RANDOMX_VAES512
void hashAndFillAes1Rx4_VAES512(void *, size_t, void *, void *);
#endif

namespace {
const xmrig::HardwareAES reference = {"reference", &hashAndFillAes1Rx4<0, 2>};
constexpr size_t sizes[] = {256 * 1024, 1024 * 1024, 2 * 1024 * 1024};
std::once_flag prepared[3];
std::atomic<const xmrig::HardwareAES *> selected[3]{};
xmrig::HardwareAES choices[3];

size_t sizeIndex(size_t size)
{
    for (size_t i = 0; i < 3; ++i) { if (size == sizes[i]) { return i; } }
    return 3;
}
}

std::vector<xmrig::HardwareAES> xmrig::hardwareAESImplementations()
{
    std::vector<HardwareAES> result{reference};
#ifdef XMRIG_RANDOMX_VAES512
    if (Cpu::info()->hasAES() && Cpu::info()->hasVAES() && Cpu::info()->has(ICpuInfo::FLAG_AVX512F)) {
        result.push_back({"vaes-512", &hashAndFillAes1Rx4_VAES512});
    }
#endif
    return result;
}

bool xmrig::verifyHardwareAES(const HardwareAES &impl, size_t size)
{
    if (sizeIndex(size) == 3 || !Cpu::info()->hasAES()) { return false; }
    VirtualMemory original(size + 64, false, false, false);
    VirtualMemory candidate(size + 64, false, false, false);
    if (!original.raw() || !candidate.raw()) { return false; }

    // Exercise every allowed 16-byte alignment, nonzero data, and continued fills.
    for (size_t offset : {size_t(0), size_t(16), size_t(32), size_t(48)}) {
        auto *a = original.raw() + offset;
        auto *b = candidate.raw() + offset;
        for (size_t i = 0; i < size; ++i) { a[i] = uint8_t((i * 37 + (i >> 8) + offset) & 255); }
        std::memcpy(b, a, size);
        alignas(64) uint8_t hashA[128] = {}, hashB[128] = {};
        alignas(64) uint8_t stateA[128] = {}, stateB[128] = {};
        for (size_t i = 0; i < 64; ++i) { stateA[offset + i] = uint8_t(i * 19 + offset + 1); }
        std::memcpy(stateB, stateA, sizeof(stateA));
        for (int round = 0; round < 2; ++round) {
            reference.function(a, size, hashA + offset, stateA + offset);
            impl.function(b, size, hashB + offset, stateB + offset);
            if (std::memcmp(hashA, hashB, sizeof(hashA)) ||
                std::memcmp(stateA, stateB, sizeof(stateA)) || std::memcmp(a, b, size)) {
                return false;
            }
        }
    }
    return true;
}

xmrig::HardwareAES xmrig::selectedHardwareAES(size_t size)
{
    const size_t index = sizeIndex(size);
    const HardwareAES *impl = index < 3 ? selected[index].load(std::memory_order_acquire) : nullptr;
    return impl ? *impl : reference;
}

void xmrig::prepareHardwareAES(size_t size)
{
    const size_t index = sizeIndex(size);
    // XMRig enables this implementation on Zen 5. Full hashing measurements on
    // Zen 4 did not show a reliable gain, even when an isolated AES loop did.
    if (index == 3 || Cpu::info()->arch() != ICpuInfo::ARCH_ZEN5 || !Cpu::info()->hasAES()) { return; }
    std::call_once(prepared[index], [index, size]() {
        const auto impls = hardwareAESImplementations();
        for (const auto &impl : impls) {
            if (std::strcmp(impl.name, "vaes-512") != 0) { continue; }
            if (!verifyHardwareAES(impl, size)) {
                LOG_WARN("%s hardware AES verification failed; retaining reference", Tags::randomx());
                return;
            }
            choices[index] = impl;
            selected[index].store(&choices[index], std::memory_order_release);
            LOG_INFO("%s hardware AES %s (%zu KB scratchpad)", Tags::randomx(), impl.name, size / 1024);
        }
    });
}
