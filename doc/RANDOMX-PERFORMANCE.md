# RandomX hashing performance

The shared RandomX scratchpad hash/refill step can use VAES-512 on AMD Zen 5.
This applies to all supported RandomX variants, including Zecnero v1 and v2;
it does not alter any algorithm parameters, hashes, or activation rules.

The implementation comes from [XMRig PR #3758](https://github.com/xmrig/xmrig/pull/3758),
commit `ed80a8a8286948c6c093ac37157b1cdd9a33f0d2`. The original BSD license is
preserved. Caller buffers use unaligned vector loads and stores because this
fork's VM guarantees 16-byte alignment, not 64-byte alignment.

## Selection and compatibility

* Automatic selection follows upstream's Zen 5 restriction and requires AES,
  VAES, AVX-512F, and operating-system support for the vector register state.
* Before enabling the new implementation, the miner compares its scratchpad,
  hash, and fill state against the existing implementation, including every
  permitted alignment. Verification runs once for each scratchpad size used.
* Other CPUs, unsupported compilers, builds with `WITH_VAES=OFF`, unknown
  scratchpad sizes, and failed verification retain the existing implementation.
* The function is selected when a VM is initialized, avoiding CPU detection
  inside the hashing loop. Software AES is unchanged.

The wide-vector instructions are compiled in a separate source file. They are
not enabled globally and are never dispatched on CPUs without the required
features. No new mining configuration is required.

## Verify hashes

Run the existing miner executable:

```sh
./build/xmrigMiner --randomx-aes-test
```

This offline test verifies the available hardware AES implementations with
256 KiB, 1 MiB, and 2 MiB scratchpads. It then compares three consecutive
pipelined hashes against the unchanged one-shot path, using both interpreter
and JIT execution for every supported RandomX variant. A mismatch returns a
nonzero exit status. This diagnostic requires hardware AES.

## Compare full hashing throughput

Stop other miners before measuring:

```sh
./build/xmrigMiner --randomx-aes-bench=rx/0 --threads=24 --seconds=10
```

The benchmark compares the reference implementation with the implementation
automatically selected for that CPU. It runs three samples of each, alternates
their order, and reports the median hashes per second. `--seconds` is the
duration of each sample, not the entire command; accepted values are 1–60.
`--threads` defaults to all logical CPUs, and workers are pinned to logical
CPU IDs starting at zero. Affinity failures cause the diagnostic to fail.

To evaluate VAES-512 on another capable CPU without changing production mining:

```sh
./build/xmrigMiner --randomx-aes-bench=rx/wow --threads=24 --seconds=10 --aes-impl=vaes-512
```

`--aes-impl` only affects this offline benchmark. Available names are
`reference` and, when supported by the build and CPU, `vaes-512`. The benchmark
rejects unavailable implementations. Any supported RandomX algorithm may be
used, including `rx/arq`, `rx/zecnero`, and `rx/zecnero2`.

The benchmark uses full-memory JIT hashing and attempts huge-page allocation.
Dataset initialization is excluded from the measured interval. It uses the
same AES-independent settings on both paths, including normal Blake2 dispatch.
It does not contact pools, submit blocks, load wallet credentials, change MSRs,
or rewrite the miner configuration. Its absolute hashrate may differ from a
normal mining run that enables MSR tuning. It does not measure power usage.

## Local validation

On a Ryzen 9 7900X (Zen 4), Linux x86-64, GCC 15.2, hash equivalence passed
across all 13 supported RandomX variants. Preliminary 24-thread comparisons
found no reliable gain from VAES-512 on that CPU. Custom AES prefetch distances
and loop unrolling were also evaluated and removed after measuring lower
full-hashing throughput. Zen 4 therefore retains the reference path.

Final 24-thread measurements used 1 GiB dataset pages, three alternating
5-second samples per implementation, and normal Blake2 CPU dispatch:

| Algorithm | Reference H/s | VAES-512 H/s | Difference |
| --- | ---: | ---: | ---: |
| `rx/0` | 15,985.35 | 15,954.79 | -0.191% |
| `rx/wow` | 17,092.35 | 17,096.71 | +0.025% |
| `rx/arq` | 71,512.94 | 71,773.03 | +0.364% |
| `rx/zecnero2` | 14,269.09 | 14,345.65 | +0.537% |

These short measurements contain clock, thermal, and scheduling variation;
they do not establish a repeatable improvement on Zen 4. VAES-512 was forced
only inside the benchmark for this comparison.

Zen 5 performance has not been measured locally. Upstream reports a small
gain for that architecture; do not expect a large hashrate increase or infer
an energy-efficiency gain from these changes.
