/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crypto/rx/RxAesDiagnostics.h"
#include "crypto/rx/RxHardwareAES.h"
#include "crypto/rx/RxAlgo.h"
#include "crypto/rx/RxCache.h"
#include "crypto/rx/RxDataset.h"
#include "crypto/rx/RxConfig.h"
#include "crypto/common/VirtualMemory.h"
#include "crypto/randomx/randomx.h"
#include "crypto/randomx/blake2/blake2.h"
#ifdef XMRIG_FEATURE_AVX2
#include "crypto/randomx/blake2/avx2/blake2b.h"
#endif
#include "crypto/randomx/virtual_machine.hpp"
#include "backend/cpu/Cpu.h"
#include "base/kernel/Platform.h"
#include "base/tools/Arguments.h"
#include "base/tools/Chrono.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Vm = std::unique_ptr<randomx_vm, decltype(&randomx_destroy_vm)>;

void initializeDiagnostics()
{
    xmrig::VirtualMemory::init(0, 2048);
    // Match the dispatch used by Rx::init during normal mining.
#ifdef XMRIG_FEATURE_SSE4_1
    if (xmrig::Cpu::info()->has(xmrig::ICpuInfo::FLAG_SSE41)) { rx_blake2b_compress = rx_blake2b_compress_sse41; }
#endif
#ifdef XMRIG_FEATURE_AVX2
    if (xmrig::Cpu::info()->has(xmrig::ICpuInfo::FLAG_AVX2)) { rx_blake2b = blake2b_avx2; }
#endif
}

void require(bool value, const char *message) { if (!value) { throw std::runtime_error(message); } }

Vm vmFor(randomx_flags flags, randomx_cache *cache, randomx_dataset *dataset, uint8_t *scratchpad)
{
    auto *vm = randomx_create_vm(flags, cache, dataset, scratchpad, 0);
    require(vm != nullptr, "RandomX VM allocation failed");
    return Vm(vm, &randomx_destroy_vm);
}

unsigned number(const xmrig::Arguments &args, const char *name, unsigned fallback, unsigned maximum)
{
    const char *text = args.value(name);
    if (!text) { return fallback; }
    require(*text != '\0', "empty numeric argument");
    unsigned value = 0;
    for (; *text; ++text) {
        require(*text >= '0' && *text <= '9', "invalid numeric argument");
        require(value <= maximum / 10, "numeric argument too large");
        value = value * 10 + unsigned(*text - '0');
        require(value <= maximum, "numeric argument too large");
    }
    require(value != 0, "numeric argument must be positive");
    return value;
}

double benchmark(xmrig::RxDataset &dataset, const xmrig::HardwareAES &impl, unsigned threads, unsigned seconds)
{
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false}, failed{false};
    double deadline = 0;
    std::vector<uint64_t> counts(threads);
    std::vector<std::thread> workers;
    workers.reserve(threads);
    try {
        for (unsigned t = 0; t < threads; ++t) {
            workers.emplace_back([&, t]() {
                bool announced = false;
                try {
                    require(xmrig::Platform::setThreadAffinity(t), "benchmark CPU affinity unavailable");
                    xmrig::VirtualMemory scratch(RandomX_CurrentConfig.ScratchpadL3_Size, true, false, false);
                    require(scratch.raw() != nullptr, "scratchpad allocation failed");
                    int flags = RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES;
                    if (xmrig::Cpu::info()->assembly() == xmrig::Assembly::RYZEN) { flags |= RANDOMX_FLAG_AMD; }
                    auto vm = vmFor(static_cast<randomx_flags>(flags), nullptr, dataset.get(), scratch.raw());
                    static_cast<randomx::VmBase<0> *>(vm.get())->setHardwareAES(impl.function);
                    alignas(64) uint64_t temp[8];
                    std::array<uint8_t, 80> input{};
                    uint8_t hash[32];
                    input[0] = uint8_t(t);
                    randomx_calculate_hash_first(vm.get(), temp, input.data(), input.size());
                    for (unsigned i = 0; i < 32; ++i) {
                        input[1] = uint8_t(i);
                        randomx_calculate_hash_next(vm.get(), temp, input.data(), input.size(), hash);
                    }
                    ++ready;
                    announced = true;
                    while (!start.load(std::memory_order_acquire)) { std::this_thread::yield(); }
                    uint64_t count = 0;
                    do {
                        std::memcpy(input.data() + 8, &count, sizeof(count));
                        randomx_calculate_hash_next(vm.get(), temp, input.data(), input.size(), hash);
                        ++count;
                    } while (xmrig::Chrono::highResolutionMSecs() < deadline);
                    counts[t] = count;
                }
                catch (...) { failed = true; if (!announced) { ++ready; } }
            });
        }
    }
    catch (...) {
        // Release and join already-created workers if another thread cannot start.
        start.store(true, std::memory_order_release);
        for (auto &worker : workers) { worker.join(); }
        throw;
    }
    while (ready.load() != threads) { std::this_thread::yield(); }
    const double begin = xmrig::Chrono::highResolutionMSecs();
    deadline = begin + seconds * 1000.0;
    start.store(true, std::memory_order_release);
    for (auto &worker : workers) { worker.join(); }
    const double elapsed = xmrig::Chrono::highResolutionMSecs() - begin;
    require(!failed.load(), "benchmark worker failed");
    uint64_t total = 0;
    for (auto count : counts) { total += count; }
    return total * 1000.0 / elapsed;
}
}

int xmrig::randomxAesTest()
{
    try {
        require(Cpu::info()->hasAES(), "hardware AES is unavailable on this CPU");
        initializeDiagnostics();
        const auto impls = hardwareAESImplementations();
        for (size_t size : {size_t(256 * 1024), size_t(1024 * 1024), size_t(2 * 1024 * 1024)}) {
            for (const auto &impl : impls) {
                require(verifyHardwareAES(impl, size), "scratchpad/hash/fill-state equivalence failed");
                printf("PASS AES %s %zu KB: scratchpad, hash, state and alignment\n", impl.name, size / 1024);
            }
        }
        const auto algorithms = Algorithm::all([](const Algorithm &algo) { return algo.family() == Algorithm::RANDOM_X; });
        for (const auto &algo : algorithms) {
            RxAlgo::apply(algo);
            randomx_set_optimized_dataset_init(0);
            RxCache cache(false, 0);
            require(cache.get() != nullptr, "cache allocation failed");
            const Buffer seed(32, 0x3c);
            cache.init(seed);
            VirtualMemory scratch(algo.l3(), false, false, false);
            require(scratch.raw() != nullptr, "scratchpad allocation failed");
            std::array<std::array<uint8_t, 80>, 3> inputs{};
            std::array<std::array<uint8_t, 32>, 3> expected{};
            for (size_t i = 0; i < inputs.size(); ++i) {
                for (size_t j = 0; j < inputs[i].size(); ++j) { inputs[i][j] = uint8_t(i * 91 + j * 37); }
            }
            // The independent one-shot path uses the unchanged AES hash/fill routines.
            auto referenceVm = vmFor(RANDOMX_FLAG_HARD_AES, cache.get(), nullptr, scratch.raw());
            for (size_t i = 0; i < inputs.size(); ++i) {
                randomx_calculate_hash(referenceVm.get(), inputs[i].data(), inputs[i].size(), expected[i].data());
            }
            referenceVm.reset();
            for (int mode : {0, int(RANDOMX_FLAG_JIT)}) {
                auto vm = vmFor(static_cast<randomx_flags>(mode | RANDOMX_FLAG_HARD_AES), cache.get(), nullptr, scratch.raw());
                for (const auto &impl : impls) {
                    static_cast<randomx::VmBase<0> *>(vm.get())->setHardwareAES(impl.function);
                    alignas(64) uint64_t temp[8];
                    randomx_calculate_hash_first(vm.get(), temp, inputs[0].data(), inputs[0].size());
                    for (size_t i = 0; i < inputs.size(); ++i) {
                        uint8_t hash[32];
                        const auto &next = inputs[(i + 1) % inputs.size()];
                        randomx_calculate_hash_next(vm.get(), temp, next.data(), next.size(), hash);
                        require(std::memcmp(hash, expected[i].data(), 32) == 0, "full RandomX hash mismatch");
                    }
                }
            }
            printf("PASS %s: all %zu AES implementations, interpreter/JIT, 3 consecutive hashes\n", algo.name(), impls.size());
            fflush(stdout);
        }
        printf("PASS: %zu RandomX variants preserve reference hashes\n", algorithms.size());
        return 0;
    }
    catch (const std::exception &e) { fprintf(stderr, "RandomX AES test failed: %s\n", e.what()); return 1; }
}

int xmrig::randomxAesBenchmark(const Arguments &args)
{
    try {
        require(Cpu::info()->hasAES(), "hardware AES is unavailable on this CPU");
        const char *name = args.value("--randomx-aes-bench");
        Algorithm algo(name ? name : "rx/0");
        require(algo.family() == Algorithm::RANDOM_X, "benchmark requires a RandomX algorithm");
        const unsigned threads = number(args, "--threads", Cpu::info()->threads(), Cpu::info()->threads());
        const unsigned seconds = number(args, "--seconds", 5, 60);
        initializeDiagnostics();
        prepareHardwareAES(algo.l3());
        RxAlgo::apply(algo);
        randomx_set_optimized_dataset_init(0);
        RxDataset dataset(true, true, true, RxConfig::FastMode, 0);
        require(dataset.get() != nullptr, "full dataset allocation failed");
        require(dataset.init(Buffer(32, 0x3c), threads, -1), "dataset initialization failed");
        const auto impls = hardwareAESImplementations();
        auto selectedImpl = selectedHardwareAES(algo.l3());
        if (const char *forced = args.value("--aes-impl")) {
            auto it = std::find_if(impls.begin(), impls.end(), [forced](const HardwareAES &impl) { return std::strcmp(impl.name, forced) == 0; });
            require(it != impls.end(), "AES implementation unavailable on this build/CPU");
            selectedImpl = *it;
        }
        require(verifyHardwareAES(selectedImpl, algo.l3()), "selected AES verification failed");
        printf("Benchmark %s: %u threads, %u seconds/sample, candidate %s, 1GB dataset pages %s\n",
               algo.name(), threads, seconds, selectedImpl.name, dataset.isOneGbPages() ? "yes" : "no");
        fflush(stdout);
        std::array<double, 3> baseline{}, candidate{};
        for (size_t round = 0; round < 3; ++round) {
            for (size_t n = 0; n < 2; ++n) {
                const bool test = ((n + round) % 2) != 0;
                auto &values = test ? candidate : baseline;
                values[round] = benchmark(dataset, test ? selectedImpl : impls[0], threads, seconds);
                printf("%s round %zu: %.2f H/s\n", test ? selectedImpl.name : "reference", round + 1, values[round]);
                fflush(stdout);
            }
        }
        std::sort(baseline.begin(), baseline.end());
        std::sort(candidate.begin(), candidate.end());
        printf("Median reference %.2f H/s, %s %.2f H/s, change %+.3f%%\n", baseline[1], selectedImpl.name, candidate[1], 100.0 * (candidate[1] / baseline[1] - 1.0));
        return 0;
    }
    catch (const std::exception &e) { fprintf(stderr, "RandomX AES benchmark failed: %s\n", e.what()); return 1; }
}
