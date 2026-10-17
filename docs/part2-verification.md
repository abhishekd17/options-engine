# Part 2 verification (speed)

Evidence for Part 2: the exact commands that were run and their output, so a reviewer can check
the results without re-running anything.

Machine: Apple M5 Pro (arm64), 18 logical CPUs (`sysctl -n hw.ncpu`), macOS 26.7.1
(`sw_vers -productVersion`). `c++ --version`: Apple clang version 21.0.0 (clang-2100.3.34.2).
Every run below started with `export PATH=/opt/homebrew/bin:$PATH`, from
`/Users/abhilnk/projects/options-engine`.

## What Part 2 added

| Piece | Files |
|---|---|
| SIMD layer: NEON (`F64x2`), AVX2+FMA (`F64x4`), scalar `double`, test-only `Emulated<W>`, chosen at compile time; `Lane<V>` concept | `include/opt/simd/*.hpp` |
| Fast exp, log, normal CDF/PDF, inverse CDF (lane templates, IEEE operations only) | `include/opt/core/fast_math.hpp` |
| Batch API over structure-of-arrays spans; Reference, FastScalar, Simd engines | `include/opt/batch/batch.hpp`, `kernel.hpp`, `src/batch/batch.cpp` |
| Persistent thread pool (caller + `threads - 1` `std::jthread` workers) and `ThreadedPricer` | `include/opt/batch/thread_pool.hpp`, `src/batch/thread_pool.cpp` |
| Error measurement tool | `tools/fast_math_error.cpp` |
| Tests | `tests/core/simd_test.cpp`, `tests/core/fast_math_test.cpp`, `tests/batch/engine_test.cpp`, `tests/batch/thread_pool_test.cpp`, `tests/alloc_test.cpp` |
| Benchmarks and report script | `bench/batch_bench.cpp`, `bench/pricing_bench.cpp`, `scripts/run_benchmarks.sh` |
| Docs | `docs/accuracy.md` (fast math, batch tolerances), `docs/benchmarks.md`, `docs/bench-results/2026-10-17-macos-apple-m5-pro.json` |

## Clean builds and test runs

Each preset was built from a deleted build directory, so no stale object could hide an error.
Warnings are errors on project targets (`-Werror` via `opteng_configure_target`). The script
(`/tmp/oe-final.sh`, run with `caffeinate -i`):

```bash
for p in release asan tsan debug release-lto; do
  start=$(date +%s)
  rm -rf build/$p
  cmake --preset $p > /tmp/oe-$p.log 2>&1 && cmake --build --preset $p >> /tmp/oe-$p.log 2>&1
  brc=$?
  echo "$p build_rc=$brc warnings=$(grep -c 'warning:' /tmp/oe-$p.log) errors=$(grep -c 'error:' /tmp/oe-$p.log)"
  ctest --preset $p > /tmp/oe-$p-test.log 2>&1
  echo "$p ctest_rc=$? wall_s=$(( $(date +%s) - start ))"
  tail -n 3 /tmp/oe-$p-test.log
done
```

Output:

```
release build_rc=0 warnings=0 errors=0
release ctest_rc=0 wall_s=34
100% tests passed out of 120
Total Test time (real) =   9.70 sec
asan build_rc=0 warnings=0 errors=0
asan ctest_rc=0 wall_s=47
100% tests passed out of 120
Total Test time (real) =  21.32 sec
tsan build_rc=0 warnings=0 errors=0
tsan ctest_rc=0 wall_s=81
100% tests passed out of 120
Total Test time (real) =  52.86 sec
debug build_rc=0 warnings=0 errors=0
debug ctest_rc=0 wall_s=70
100% tests passed out of 120
Total Test time (real) =  48.98 sec
release-lto build_rc=0 warnings=0 errors=0
release-lto ctest_rc=0 wall_s=37
100% tests passed out of 120
```

(`tail -n 3` cut the release-lto "Total Test time" line, which reads 14.64 sec in
`/tmp/oe-release-lto-test.log`.) The asan run takes 21.32 s, inside the 3-minute budget of
design section 3.

Sanitizer evidence:

```bash
for p in asan tsan; do
  echo "$p: tsan-warnings=$(grep -c 'WARNING: ThreadSanitizer' /tmp/oe-$p-test.log) asan-errors=$(grep -c 'ERROR: AddressSanitizer' /tmp/oe-$p-test.log) ubsan-runtime-errors=$(grep -c 'runtime error' /tmp/oe-$p-test.log)"
  grep 'opteng:' /tmp/oe-$p.log
done
grep 'opteng:' /tmp/oe-release.log /tmp/oe-release-lto.log
otool -L build/tsan/tests/opt_batch_tests | grep -i tsan
otool -L build/asan/tests/opt_batch_tests | grep -i asan
```

```
asan: tsan-warnings=0 asan-errors=0 ubsan-runtime-errors=0
-- opteng: sanitizer = asan
tsan: tsan-warnings=0 asan-errors=0 ubsan-runtime-errors=0
-- opteng: sanitizer = tsan
/tmp/oe-release.log:-- opteng: native tuning = -mcpu=native (arm64, AppleClang)
/tmp/oe-release-lto.log:-- opteng: native tuning = -mcpu=native (arm64, AppleClang)
/tmp/oe-release-lto.log:-- opteng: LTO enabled for project targets
build/tsan/tests/opt_batch_tests:
	@rpath/libclang_rt.tsan_osx_dynamic.dylib (compatibility version 0.0.0, current version 0.0.0)
build/asan/tests/opt_batch_tests:
	@rpath/libclang_rt.asan_osx_dynamic.dylib (compatibility version 0.0.0, current version 0.0.0)
```

The asan preset is ASan + UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=all`), the
tsan preset is ThreadSanitizer; both instrument the dependencies too. The thread pool and
`ThreadedPricer` tests ran under ThreadSanitizer with no report.

### The 120 ctest entries

Part 1 had 76. Part 2 adds 44: 13 SIMD-layer tests and 12 fast-math tests in `opt_core_tests`
(59 tests total), 15 in the new `opt_batch_tests`, 2 in the new `opt_alloc_tests`, and
`bench.smoke.batch_bench` and `bench.smoke.pricing_bench`. The Part 2 entries from the tsan log
(`grep -E "Test +#" /tmp/oe-tsan-test.log`, filtered to the Part 2 suites):

```
 33/120 Test #33: Version.SimdBackendMatchesWidth ................................................. Passed 0.03 sec
 35/120 Test #35: SimdBackend.LaneOpsEqualScalarBitwise<opt::simd::Emulated<4ul>> ................. Passed 0.04 sec
 36/120 Test #36: SimdBackend.BroadcastLoadStoreUnaligned<opt::simd::Emulated<4ul>> ............... Passed 0.03 sec
 37/120 Test #37: SimdBackend.MasksSelectAny<opt::simd::Emulated<4ul>> ............................ Passed 0.03 sec
 38/120 Test #38: SimdBackend.LoadTypeGivesOmega<opt::simd::Emulated<4ul>> ........................ Passed 0.03 sec
 39/120 Test #39: SimdBackend.LaneOpsEqualScalarBitwise<opt::simd::Emulated<8ul>> ................. Passed 0.04 sec
 40/120 Test #40: SimdBackend.BroadcastLoadStoreUnaligned<opt::simd::Emulated<8ul>> ............... Passed 0.03 sec
 41/120 Test #41: SimdBackend.MasksSelectAny<opt::simd::Emulated<8ul>> ............................ Passed 0.03 sec
 42/120 Test #42: SimdBackend.LoadTypeGivesOmega<opt::simd::Emulated<8ul>> ........................ Passed 0.03 sec
 43/120 Test #43: SimdBackend.LaneOpsEqualScalarBitwise<opt::simd::F64x2> ......................... Passed 0.04 sec
 44/120 Test #44: SimdBackend.BroadcastLoadStoreUnaligned<opt::simd::F64x2> ....................... Passed 0.03 sec
 45/120 Test #45: SimdBackend.MasksSelectAny<opt::simd::F64x2> .................................... Passed 0.03 sec
 46/120 Test #46: SimdBackend.LoadTypeGivesOmega<opt::simd::F64x2> ................................ Passed 0.03 sec
 47/120 Test #47: SimdScalar.LoadTypeAndBits ...................................................... Passed 0.03 sec
 48/120 Test #48: FastMath.ExpRelativeError ....................................................... Passed 0.10 sec
 49/120 Test #49: FastMath.ExpSpecialValues ....................................................... Passed 0.03 sec
 50/120 Test #50: FastMath.LogError ............................................................... Passed 0.10 sec
 51/120 Test #51: FastMath.NormCdfLowerTailAbsolute ............................................... Passed 0.11 sec
 52/120 Test #52: FastMath.NormCdfLowerTailRelative ............................................... Passed 0.11 sec
 53/120 Test #53: FastMath.NormCdfCentreRelative .................................................. Passed 0.11 sec
 54/120 Test #54: FastMath.NormCdfUpperRelative ................................................... Passed 0.10 sec
 55/120 Test #55: FastMath.NormCdfFarTails ........................................................ Passed 0.17 sec
 56/120 Test #56: FastMath.NormInv ................................................................ Passed 0.13 sec
 57/120 Test #57: FastMathBackend.BitIdenticalToDouble<opt::simd::Emulated<4ul>> .................. Passed 0.18 sec
 58/120 Test #58: FastMathBackend.BitIdenticalToDouble<opt::simd::Emulated<8ul>> .................. Passed 0.28 sec
 59/120 Test #59: FastMathBackend.BitIdenticalToDouble<opt::simd::F64x2> .......................... Passed 0.08 sec
 99/120 Test #99: BatchEngine.GridIsInDomain ...................................................... Passed 0.12 sec
100/120 Test #100: BatchEngine.ReferenceEqualsScalarBsm ............................................ Passed 0.22 sec
101/120 Test #101: BatchEngine.FastScalarMatchesReference .......................................... Passed 0.36 sec
102/120 Test #102: BatchEngine.SimdMatchesReferenceAndFastScalar ................................... Passed 0.46 sec
103/120 Test #103: BatchEngine.SizesAndUnalignedOffsets ............................................ Passed 0.80 sec
104/120 Test #104: BatchEngine.ValidateBatchReturnsFirstInvalidIndex ............................... Passed 0.03 sec
105/120 Test #105: BatchEngine.SizeMismatchThrowsBeforeWork ........................................ Passed 0.03 sec
106/120 Test #106: EmulatedKernel.MatchesReferenceAndFastScalarBitwise<opt::simd::Emulated<4ul>> ... Passed 0.60 sec
107/120 Test #107: EmulatedKernel.MatchesReferenceAndFastScalarBitwise<opt::simd::Emulated<8ul>> ... Passed 0.74 sec
108/120 Test #108: ThreadPool.ResolvesThreadCount .................................................. Passed 0.03 sec
109/120 Test #109: ThreadPool.EveryChunkRunsExactlyOnce ............................................ Passed 0.04 sec
110/120 Test #110: ThreadPool.ManyJobsOnOnePool .................................................... Passed 0.04 sec
111/120 Test #111: ThreadPool.DestructionWhileIdle ................................................. Passed 0.04 sec
112/120 Test #112: ThreadPool.ConcurrentRunCallsAreSerialised ...................................... Passed 0.04 sec
113/120 Test #113: ThreadedPricer.EqualsSimdBitwise ................................................ Passed 0.19 sec
114/120 Test #114: Allocations.CounterIsLive ....................................................... Passed 0.03 sec
115/120 Test #115: Allocations.BatchEnginesAllocateNothing ......................................... Passed 0.61 sec
116/120 Test #116: opt_info.runs ................................................................... Passed 0.85 sec
117/120 Test #117: bench.smoke.iv_bench ............................................................ Passed 1.96 sec
118/120 Test #118: bench.smoke.tree_mc_bench ....................................................... Passed 2.61 sec
119/120 Test #119: bench.smoke.batch_bench ......................................................... Passed 5.73 sec
120/120 Test #120: bench.smoke.pricing_bench ....................................................... Passed 2.05 sec
```

What the key tests check:

- `SimdBackend.*`: every lane operation of every backend equals the scalar result bit for bit,
  unaligned load/store, masks, `load_type` gives omega = +1 / -1.
- `FastMath.*`: the accuracy bounds in `docs/accuracy.md` on 1,000,000 seeded points per row
  plus edge points; `FastMathBackend.*`: every backend is bit-identical to the double version.
- `BatchEngine.*`, `EmulatedKernel.*`: 200,003 random + 304 edge options, every engine against
  the Reference engine within the tolerances in `docs/accuracy.md`; Simd, Emulated<4> and
  Emulated<8> bit-identical to FastScalar; sizes 0, 1, W-1, W, W+1, 2W+3 (W = 2, 4, 8) at
  offsets 0, 1, 3, 1001 without writing outside the range; `validate_batch` returns the first
  bad index; size mismatches throw before any output is written.
- `ThreadPool.*`: each chunk runs exactly once for 0, 1, threads-1, threads and 1,000 chunks
  with 1, 2, 3 and 18 threads; 1,000 jobs on one pool; destruction while idle; two threads
  calling `run` at once.
- `ThreadedPricer.EqualsSimdBitwise`: 21,483 options (5 full chunks plus a partial one), 3
  repetitions each for 1, 2, 3 and 18 threads, all 7 outputs bit-identical to Simd.
- `Allocations.BatchEnginesAllocateNothing`: 20 rounds of price and price+Greeks for
  Reference, FastScalar, Simd, `ThreadedPricer(4)` and `ThreadedPricer(0)` (18 threads) with a
  counting global `operator new`: 0 allocations. `Allocations.CounterIsLive` shows the counter
  works (a `make_unique` and a pool construction are counted).

## AVX2 backend (x86_64 under Rosetta 2)

The Mac is arm64, so the AVX2 code was checked by building an x86_64 binary with AVX2 and FMA
enabled and running it under Rosetta 2. This is not a preset; it is a one-off build directory.

```bash
rm -rf build/x86-avx2
cmake -S . -B build/x86-avx2 -G Ninja -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-mavx2 -mfma" -DOPTENG_BUILD_BENCH=OFF > /tmp/oe-x86.log 2>&1 \
  && cmake --build build/x86-avx2 >> /tmp/oe-x86.log 2>&1
echo "build_rc=$? warnings=$(grep -c 'warning:' /tmp/oe-x86.log)"
arch -x86_64 build/x86-avx2/tools/opt_info
for t in opt_core_tests opt_pricing_tests opt_batch_tests opt_alloc_tests; do
  printf "%s: " $t
  arch -x86_64 build/x86-avx2/tests/$t > /tmp/oe-x86-$t.log 2>&1
  echo "rc=$? $(grep -E '^\[  PASSED' /tmp/oe-x86-$t.log)"
done
grep "bit-identical" /tmp/oe-x86-opt_batch_tests.log
```

```
build_rc=0 warnings=0
options-engine 0.1.0
simd backend: avx2+fma (width 4)
compiler: apple clang 21.0.0
opt_core_tests: rc=0 [  PASSED  ] 59 tests.
opt_pricing_tests: rc=0 [  PASSED  ] 39 tests.
opt_batch_tests: rc=0 [  PASSED  ] 15 tests.
opt_alloc_tests: rc=0 [  PASSED  ] 2 tests.
[batch] Simd (avx2+fma, width 4) vs FastScalar: 0 of 1402149 outputs not bit-identical
```

The scalar fallback backend (`Native = double`) is the path for x86 builds without AVX2+FMA,
for example the CI asan job (no native tuning). It was not built locally as the native backend;
its code is the `double` overload set that FastScalar and every tail already run, and
`Emulated<W>` is built on it.

Not verified locally: GCC. No GCC is installed on this machine, so the GCC 13 + libstdc++ jobs
in `.github/workflows/ci.yml` are the first GCC build of this code.

## Benchmarks

```bash
export PATH=/opt/homebrew/bin:$PATH
caffeinate -i scripts/run_benchmarks.sh > /tmp/oe-bench.md
```

Exit code 0. The report is pasted unchanged into `docs/benchmarks.md` ("macOS preliminary"),
and the JSON is `docs/bench-results/2026-10-17-macos-apple-m5-pro.json`. Medians of 5
repetitions (ns per option, from that report):

| Engine | price only, n = 4,096 | price only, n = 1,048,576 | price + Greeks, n = 4,096 | price + Greeks, n = 1,048,576 |
|---|---|---|---|---|
| Reference | 37.22 | 41.93 | 45.94 | 56.6 |
| FastScalar | 18.7 | 18.7 | 26.65 | 27.02 |
| Simd (NEON) | 9.989 | 10.14 | 14.16 | 14.76 |
| Threaded, 1 thread | 10.08 | 10.18 | 14.16 | 14.8 |
| Threaded, 8 threads | 12.22 | 1.561 | 17.16 | 2.6 |
| Threaded, 18 threads | 18.85 | 0.8994 | 20.97 | 1.569 |

Other medians from the same run: `implied_vol_black` 258.1 ns per solve near the money and
381.6 ns in the wings; CRR European 1042 ns / 16196 ns / 241428 ns / 6.014e6 ns at 100 / 500 /
2,000 / 10,000 steps (American 3194 / 81590 / 1.381e6 / 3.569e7 ns); Monte Carlo 5.591e7
paths/s plain and 9.147e7 paths/s with antithetic + control variate.

The run script's flag handling:

```bash
./build/release/tools/fast_math_error --bogus > /dev/null 2>&1; echo "fast_math_error --bogus rc=$?"
./build/release/tools/fast_math_error --points 0 > /dev/null 2>&1; echo "--points 0 rc=$?"
scripts/run_benchmarks.sh --nope > /dev/null 2>&1; echo "run_benchmarks.sh --nope rc=$?"
scripts/run_benchmarks.sh --repetitions x > /dev/null 2>&1; echo "--repetitions x rc=$?"
```

```
fast_math_error --bogus rc=2
--points 0 rc=2
run_benchmarks.sh --nope rc=2
--repetitions x rc=2
```

## Accuracy runs

`./build/release/tools/fast_math_error` (exit 0) and the test lines of
`./build/release/tests/opt_core_tests --gtest_filter='FastMath*'` and
`./build/release/tests/opt_batch_tests --gtest_filter='BatchEngine.*:EmulatedKernel*'` are
pasted in `docs/accuracy.md`. Headlines: `fast_exp` max relative error 2.220e-16, `fast_log`
3.814e-16, `fast_norm_cdf` max absolute error 2.220e-16 (relative up to 8.912e-9 in the lower
tail); every batch output within 4.837e-2 of its tolerance; Simd vs FastScalar 0 of 1,402,149
outputs different.

## Changes from the design, and why

- `fast_norm_cdf` bounds (design 4.1) and the batch atol scale (design 6.6) were changed after
  measurement. The measured values and the reasons are in `docs/accuracy.md` ("Fast normal CDF:
  why the bound is absolute" and "atol: discounted scale instead of S and K"). The algorithms
  are the ones the design names.
- Kernel layout (`include/opt/batch/kernel.hpp`). The design describes one kernel call per
  vector. In quick single runs on this machine (not the 5-repetition script), that version
  ran Simd prices at 18 to 20 ns/option and was limited by the long dependent fma chains inside
  exp, log and the CDF. The kernel now processes
  64-option tiles in four stages (exponentials; d1/d2; N(d1); N(d2) and outputs) with small stack
  arrays between stages, which lets the CPU overlap many independent chains: Simd prices went to
  about 10 ns/option. Each option still goes through exactly the same operations, so results did
  not change (still bit-identical across backends), and there is still no heap allocation.
- FastScalar auto-vectorization is switched off (`#pragma clang loop vectorize(disable)`, and
  `GCC novector` on GCC 14 or newer). With the tiled loops, clang auto-vectorized the scalar
  path, which made FastScalar and Simd almost the same speed and hid what the explicit SIMD code
  adds. Results are identical either way.
- Thread pool completion (design 6.4 describes a per-chunk "remaining" counter). The pool counts
  the workers that joined a job (`active_`) instead: `run` waits until it has claimed all chunks
  and `active_` is 0, then closes the job under the mutex. This also guarantees that a worker
  that wakes late can never run an old job's function on the next job's chunks. The protocol is
  written out in `thread_pool.hpp`.
- `fast_math_error` also reports `fast_norm_pdf` and `fast_norm_inv`, and splits the CDF domain
  into regions so the table shows where the error is.
