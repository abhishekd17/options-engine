# Part 1 verification (pricing core)

Evidence for Part 1: the exact commands that were run and their output, so a reviewer can check
the results without re-running the suites.

Machine: Apple M5 Pro (arm64), macOS. `c++ --version`: Apple clang version 21.0.0
(clang-2100.3.34.2). `cmake --version`: 4.4.4. `ninja --version`: 1.13.2. Every run below
started with `export PATH=/opt/homebrew/bin:$PATH`, from
`/Users/abhilnk/projects/options-engine`.

## Clean builds and test runs

Each preset was built from a deleted build directory, so no stale object could hide an error.
Warnings are errors on project targets (`-Werror` via `opteng_configure_target`).

```bash
for p in release asan debug; do
  rm -rf build/$p
  cmake --preset $p > /tmp/oe-$p.log 2>&1 && cmake --build --preset $p >> /tmp/oe-$p.log 2>&1
  echo "$p build_rc=$? warnings=$(grep -c 'warning:' /tmp/oe-$p.log) errors=$(grep -c 'error:' /tmp/oe-$p.log)"
  caffeinate -i ctest --preset $p > /tmp/oe-$p-test.log 2>&1
  echo "$p ctest_rc=$?"
  tail -n 3 /tmp/oe-$p-test.log
done
```

Output:

```
release build_rc=0 warnings=0 errors=0
release ctest_rc=0
100% tests passed out of 73
Total Test time (real) =   5.48 sec
asan build_rc=0 warnings=0 errors=0
asan ctest_rc=0
100% tests passed out of 73
Total Test time (real) =   7.82 sec
debug build_rc=0 warnings=0 errors=0
debug ctest_rc=0
100% tests passed out of 73
Total Test time (real) =   7.83 sec
```

The asan run takes 7.82 s, well inside the 3-minute budget of design section 3. Configure logs
confirm the presets: `/tmp/oe-asan.log` has `-- opteng: sanitizer = asan` (ASan + UBSan), and
`/tmp/oe-release.log` has `-- opteng: native tuning = -mcpu=native (arm64, AppleClang)`.
`otool -L build/asan/tests/opt_pricing_tests` lists `libclang_rt.asan_osx_dynamic.dylib`. The
clean release configure downloaded GoogleTest, Benchmark and nlohmann/json and accepted all three
pinned SHA-256 hashes (a wrong hash fails configure).

The 73 ctest entries: 34 in `opt_core_tests`, 36 in `opt_pricing_tests`, `opt_info.runs`, and
`bench.smoke.iv_bench` and `bench.smoke.tree_mc_bench` (each benchmark run once with
`--benchmark_min_time=1x`). The asan list, from `grep -E "Test +#" /tmp/oe-asan-test.log`:

```
 1/73 Test #1: Rng.SplitMix64MatchesReference ....................................... Passed 0.02 sec
 2/73 Test #2: Rng.Xoshiro256ppMatchesReference ..................................... Passed 0.02 sec
 3/73 Test #3: Rng.StreamsDiffer .................................................... Passed 0.02 sec
 4/73 Test #4: Rng.Uniform01EndsAreStrictlyInside ................................... Passed 0.02 sec
 5/73 Test #5: Rng.Uniform01MomentsWithinFiveStandardErrors ......................... Passed 0.02 sec
 6/73 Test #6: Rng.WorksWithStandardDistributions ................................... Passed 0.02 sec
 7/73 Test #7: Normal.KnownValues ................................................... Passed 0.02 sec
 8/73 Test #8: Normal.InverseCdfRoundTrip ........................................... Passed 0.04 sec
 9/73 Test #9: Normal.InverseCdfIsAntisymmetric ..................................... Passed 0.02 sec
10/73 Test #10: Normal.CdfSymmetry ................................................... Passed 0.02 sec
11/73 Test #11: Normal.PdfMatchesDerivativeOfCdf ..................................... Passed 0.02 sec
12/73 Test #12: NeumaierSum.StartsAtZeroOrInitial .................................... Passed 0.02 sec
13/73 Test #13: NeumaierSum.RecoversBitsLostToLargeTerms ............................. Passed 0.02 sec
14/73 Test #14: NeumaierSum.ManySmallTermsAreExactToOneUlp ........................... Passed 0.03 sec
15/73 Test #15: LatencyHistogram.BucketsCoverEveryValueContiguously .................. Passed 0.02 sec
16/73 Test #16: LatencyHistogram.SmallValuesAreExact ................................. Passed 0.02 sec
17/73 Test #17: LatencyHistogram.BucketWidthIsBoundedRelativeToValue ................. Passed 0.02 sec
18/73 Test #18: LatencyHistogram.EmptyReportsZeros ................................... Passed 0.02 sec
19/73 Test #19: LatencyHistogram.OneValue ............................................ Passed 0.02 sec
20/73 Test #20: LatencyHistogram.NearestRankWithoutRoundingSurprises ................. Passed 0.02 sec
21/73 Test #21: LatencyHistogram.PercentilesNeverUnderReportAndStayWithinPrecision ... Passed 0.03 sec
22/73 Test #22: LatencyHistogram.RecordNEqualsRepeatedRecord ......................... Passed 0.02 sec
23/73 Test #23: LatencyHistogram.MergeEqualsRecordingEverything ...................... Passed 0.02 sec
24/73 Test #24: LatencyHistogram.ExtremeValues ....................................... Passed 0.02 sec
25/73 Test #25: LatencyHistogram.ForEachBucketVisitsNonEmptyBucketsInOrder ........... Passed 0.02 sec
26/73 Test #26: Args.ParseDoubleAcceptsWholeFiniteInRange ............................ Passed 0.02 sec
27/73 Test #27: Args.ParseDoubleRejectsBadInput ...................................... Passed 0.02 sec
28/73 Test #28: Args.ParseU64 ........................................................ Passed 0.02 sec
29/73 Test #29: Args.IteratorWalksFlagsAndValues ..................................... Passed 0.02 sec
30/73 Test #30: Args.MissingValueThrows .............................................. Passed 0.02 sec
31/73 Test #31: Args.NoArguments ..................................................... Passed 0.02 sec
32/73 Test #32: Version.IsSemver ..................................................... Passed 0.02 sec
33/73 Test #33: Version.SimdBackendMatchesWidth ...................................... Passed 0.02 sec
34/73 Test #34: Version.CompilerIdIsNonEmpty ......................................... Passed 0.02 sec
35/73 Test #35: BlackScholes.PutCallParity ........................................... Passed 0.02 sec
36/73 Test #36: BlackScholes.HullExample15_6 ......................................... Passed 0.02 sec
37/73 Test #37: BlackScholes.GreeksMatchFiniteDifferences ............................ Passed 0.02 sec
38/73 Test #38: BlackScholes.Black76MatchesBsmAndFiniteDifferences ................... Passed 0.02 sec
39/73 Test #39: BlackScholes.ZeroTimeGivesIntrinsic .................................. Passed 0.02 sec
40/73 Test #40: BlackScholes.ZeroVolLimits ........................................... Passed 0.02 sec
41/73 Test #41: BlackScholes.ExtremeMoneynessIsFiniteAndHitsLimits ................... Passed 0.02 sec
42/73 Test #42: BlackScholes.PriceWithinNoArbitrageBounds ............................ Passed 0.02 sec
43/73 Test #43: BlackScholes.ValidateReportsFirstProblem ............................. Passed 0.02 sec
44/73 Test #44: ImpliedVol.HullSection15_11 .......................................... Passed 0.02 sec
45/73 Test #45: ImpliedVol.RoundTripGrid ............................................. Passed 0.05 sec
46/73 Test #46: ImpliedVol.BoundsAndInvalidInputs .................................... Passed 0.02 sec
47/73 Test #47: ImpliedVol.DeepInTheMoneyCallSolvedViaPut ............................ Passed 0.02 sec
48/73 Test #48: ImpliedVol.OneHourExpiry ............................................. Passed 0.02 sec
49/73 Test #49: ImpliedVol.HighVolInsideMaxVol ....................................... Passed 0.02 sec
50/73 Test #50: ImpliedVol.VolAboveMaxVol ............................................ Passed 0.02 sec
51/73 Test #51: ImpliedVol.ExtremelyLowVega .......................................... Passed 0.02 sec
52/73 Test #52: ImpliedVol.NotConvergedWithTinyIterationBudget ....................... Passed 0.02 sec
53/73 Test #53: Crr.HullExample21_1AmericanPut ....................................... Passed 0.02 sec
54/73 Test #54: Crr.AmericanPutAtLeastEuropean ....................................... Passed 0.02 sec
55/73 Test #55: Crr.AmericanCallWithoutDividendEqualsEuropeanBitwise ................. Passed 0.03 sec
56/73 Test #56: Crr.AmericanCallWithDividendAtLeastEuropean .......................... Passed 0.02 sec
57/73 Test #57: Crr.DeepInTheMoneyAmericanPutIsExercisedImmediately .................. Passed 0.02 sec
58/73 Test #58: Crr.ConvergesToBsmAtTheMoney ......................................... Passed 0.08 sec
59/73 Test #59: Crr.NonAtmErrorBounded ............................................... Passed 0.14 sec
60/73 Test #60: Crr.ArgumentValidation ............................................... Passed 0.02 sec
61/73 Test #61: Crr.ProbabilityOutOfRange ............................................ Passed 0.02 sec
62/73 Test #62: Crr.Limits ........................................................... Passed 0.02 sec
63/73 Test #63: Crr.WorkspaceAndAllocatingOverloadsAgree ............................. Passed 0.02 sec
64/73 Test #64: MonteCarlo.WithinFourStandardErrorsOfBsm ............................. Passed 0.08 sec
65/73 Test #65: MonteCarlo.CoverageOverSeeds ......................................... Passed 0.04 sec
66/73 Test #66: MonteCarlo.StandardErrorShrinksAsOneOverSqrtN ........................ Passed 0.02 sec
67/73 Test #67: MonteCarlo.EachVarianceReductionHelps ................................ Passed 0.03 sec
68/73 Test #68: MonteCarlo.DeterministicForSeed ...................................... Passed 0.02 sec
69/73 Test #69: MonteCarlo.DegenerateInputs .......................................... Passed 0.02 sec
70/73 Test #70: MonteCarlo.InvalidConfigThrows ....................................... Passed 0.02 sec
71/73 Test #71: opt_info.runs ........................................................ Passed 0.78 sec
72/73 Test #72: bench.smoke.iv_bench ................................................. Passed 1.92 sec
73/73 Test #73: bench.smoke.tree_mc_bench ............................................ Passed 1.86 sec
```

## Build info

```bash
./build/release/tools/opt_info
```

```
options-engine 0.1.0
simd backend: neon (width 2)
compiler: apple clang 21.0.0
```

## Measured values printed by the tests

```bash
(./build/release/tests/opt_core_tests; ./build/release/tests/opt_pricing_tests) | grep "\[measure\]"
```

```
[measure] AS241 round trip: max error / bound = 0.671, max (err/p - 1e-14)/(x^2 eps) = 4.01 (bound coefficient 6)
[measure] norm_cdf symmetry: max |N(x)+N(-x)-1| = 0
[measure] put-call parity: max |c - p - (S e^-qT - K e^-rT)| / (S + K) = 3.9e-16 (tolerance 1e-12, 10000 points)
[measure] Hull 15.6: c = 4.759422 (book 4.76), p = 0.808599 (book 0.81)
[measure] Greeks vs FD (2000 points, 2095 draws): max error/tolerance delta 0.00329 gamma 0.0202 vega 0.0242 theta 0.436 rho 0.00157 vanna 0.0872 volga 0.547
[measure] bounds: max violation / (S + K) = 1.22e-16 (allowance 1e-14)
[measure] Hull 15.11: implied vol = 0.234513 (book 0.235), 4 iterations
[measure] IV round trip: 56000 points, 44872 well-conditioned (max |vol_hat - vol| = 1.53e-11, tolerance 1e-10); ill-conditioned: 11091 Ok (max reprice rel error 1.89e-10; 1 above 1e-10 relative, max 7.78 eps x price_scale, floor 16), 35 BelowIntrinsic, 2 AboveUpperBound; iterations (Ok) mean 6.25 max 49
[measure] Hull 21.1: 5-step American put = 4.488459 (book 4.49)
[measure] American put early-exercise premium: max 40.549965 over 200 points
[measure] American call (q > 0) early-exercise premium: max 7.961356
[measure] CRR ATM call r=0.05 q=0.00 vol=0.20 T=1.00: e(100)=1.997e-02 e(400)=4.998e-03 e(1600)=1.250e-03 e(6400)=3.124e-04 ratios 0.2502 0.2501 0.2500
[measure] CRR ATM put r=0.05 q=0.00 vol=0.20 T=1.00: e(100)=1.997e-02 e(400)=4.998e-03 e(1600)=1.250e-03 e(6400)=3.124e-04 ratios 0.2502 0.2501 0.2500
[measure] CRR ATM call r=0.02 q=0.01 vol=0.40 T=0.50: e(100)=2.787e-02 e(400)=6.975e-03 e(1600)=1.744e-03 e(6400)=4.361e-04 ratios 0.2502 0.2501 0.2500
[measure] CRR ATM put r=0.10 q=0.03 vol=0.30 T=2.00: e(100)=3.862e-02 e(400)=9.664e-03 e(1600)=2.416e-03 e(6400)=6.042e-04 ratios 0.2502 0.2501 0.2500
[measure] CRR ATM call r=0.00 q=0.00 vol=0.80 T=0.25: e(100)=3.958e-02 e(400)=9.903e-03 e(1600)=2.476e-03 e(6400)=6.191e-04 ratios 0.2502 0.2501 0.2500
[measure] CRR non-ATM (K=110): max |CRR(N) - BSM| over N in [6400, 6410] = 1.552e-04 (bound 0.2)
[measure] MC call S=100 K=100: mc=10.451113 bsm=10.450584 se=2.758e-03 (err/se = +0.19) beta=2.504
[measure] MC put S=100 K=110: mc=15.264887 bsm=15.264541 se=2.106e-03 (err/se = +0.16) beta=0.979
[measure] MC call S=50 K=60: mc=9.950690 bsm=9.949885 se=1.855e-03 (err/se = +0.43) beta=1.086
[measure] MC put S=200 K=150: mc=0.048355 bsm=0.048299 se=5.945e-04 (err/se = +0.09) beta=0.129
[measure] MC call S=100 K=80: mc=14.355047 bsm=14.353775 se=4.034e-03 (err/se = +0.32) beta=1.742
[measure] MC coverage: 3 of 50 seeds outside 2 SE (expected ~2.3, limit 8)
[measure] MC SE(160000)/SE(40000) = 0.4946 (expected 0.5)
[measure] MC SE at 200000 paths: plain 3.317e-02, antithetic 2.343e-02 (x1.42), control variate 1.260e-02 (x2.63), both 6.259e-03 (x5.30)
```

How these map to design 5.8:

| Item | Test(s) | Result |
|---|---|---|
| 1 put-call parity | `BlackScholes.PutCallParity` | max 3.9e-16 (S+K), tolerance 1e-12 |
| 2a Hull European | `BlackScholes.HullExample15_6` | c 4.759422, p 0.808599 (book 4.76 / 0.81, tol 0.005) |
| 2b Hull implied vol | `ImpliedVol.HullSection15_11` | 0.234513 (book 0.235, tol 0.0005) |
| 2c Hull American put | `Crr.HullExample21_1AmericanPut` | 4.488459 (book 4.49, tol 0.005) |
| 3 Greeks vs FD | `BlackScholes.GreeksMatchFiniteDifferences` | worst error/tolerance 0.547 (volga) |
| 4 IV round trip | `ImpliedVol.RoundTripGrid`, `tools/iv_accuracy` | 44,872 well-conditioned points, max vol error 1.53e-11 (tol 1e-10) |
| 5 IV edge cases | `ImpliedVol.BoundsAndInvalidInputs` and 6 more | pass |
| 6 American vs European | `Crr.American*`, `Crr.DeepInTheMoney...` | pass, call equality is bitwise |
| 7 CRR convergence | `Crr.ConvergesToBsmAtTheMoney`, `Crr.NonAtmErrorBounded` | ratios 0.2500 to 0.2502 (bound 0.30); non-ATM 1.552e-04 (bound 0.2) |
| 8 Monte Carlo | `MonteCarlo.*` | all 5 sets within 0.43 SE; 3 of 50 outside 2 SE; SE ratio 0.4946 |
| 9 BSM edge values | `BlackScholes.Zero*`, `ExtremeMoneyness...`, `PriceWithin...` | pass |
| 10 RNG and normal | `Rng.*`, `Normal.*` | pass (AS241 coefficient 6, see below) |

The RNG reference values in `tests/core/rng_test.cpp` came from the authors' C code
(`prng.di.unimi.it`), compiled separately; the comment there gives the curl and cc commands.

## Implied-vol accuracy tool

```bash
./build/release/tools/iv_accuracy; echo rc=$?
```

Exit code 0. The full output is in `docs/accuracy.md`. `./build/release/tools/iv_accuracy
--bogus` prints `error: unknown flag '--bogus'` and the usage text, and exits 2.

## Benchmarks (informational, not yet the documented numbers)

Plan item 1.6 says these runs are informational; the numbers to quote come from
`scripts/run_benchmarks.sh` in Part 2. The machine was not otherwise idle (load average about
4.6).

```bash
caffeinate -i ./build/release/bench/iv_bench --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
caffeinate -i ./build/release/bench/tree_mc_bench --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```

Medians:

```
BM_ImpliedVolNearMoney_median    1016602 ns      1015467 ns            5 items_per_second=4.03361M/s mean_iter=4.53198 time/solve=247.917ns
BM_ImpliedVolWings_median        1569932 ns      1568693 ns            5 items_per_second=2.61109M/s mean_iter=6.01562 time/solve=382.982ns
BM_CrrEuropean/100_median                        873 ns          872 ns            5 nodes/s=5.79177G/s
BM_CrrEuropean/500_median                      15420 ns        15404 ns            5 nodes/s=8.13124G/s
BM_CrrEuropean/2000_median                    237266 ns       237022 ns            5 nodes/s=8.44224G/s
BM_CrrEuropean/10000_median                  5948459 ns      5942159 ns            5 nodes/s=8.41529G/s
BM_CrrAmerican/100_median                       2851 ns         2848 ns            5 nodes/s=1.77304G/s
BM_CrrAmerican/500_median                      78089 ns        78005 ns            5 nodes/s=1.60567G/s
BM_CrrAmerican/2000_median                   1328775 ns      1327435 ns            5 nodes/s=1.50742G/s
BM_CrrAmerican/10000_median                 34578881 ns     34522600 ns            5 nodes/s=1.44847G/s
BM_MonteCarlo/variance_reduction:0_median       17.4 ms         17.4 ms            5 paths/s=57.3849M/s std_error=0.0128316
BM_MonteCarlo/variance_reduction:1_median       10.5 ms         10.5 ms            5 paths/s=95.7244M/s std_error=3.33733m
```

## Where Part 1 differs from the design, and why

Each change was measured first. None loosens the 1e-10 implied-vol tolerance or edits an
expected reference value.

- **Implied-vol well-conditioned region**: vega >= 1e-5 x D (F N(w d1) + K N(w d2)) instead of
  vega / (D F) >= 1e-6. The design's region included in-the-money puts with K / F up to 1e17,
  whose prices carry no information about sigma (max vol error 0.233, 19 points not solvable).
  Details and both sets of numbers: `docs/accuracy.md`.
- **Ill-conditioned reprice check**: adds a rounding floor of 16 eps x price scale (1 of 56,000
  points needed it, at 7.78), and accepts AboveUpperBound where the price rounded onto the bound
  (2 points).
- **Solver**: Newton step in log-price space, a wing initial guess, and a stop at the Black
  formula's rounding noise. In the design's well-conditioned region, mean iterations went from
  10.63 (an earlier `iv_accuracy` run, with the noise stop but plain Newton) to 6.23 (the
  current output in `docs/accuracy.md`, last block); `docs/accuracy.md` explains each change.
- **AS241 round-trip coefficient**: 6 instead of 4 (measured 4.01).
- **`uniform01`**: 52 random bits, not 53. With 53 bits, (2^53 - 1) + 0.5 rounds to 2^53 and the
  largest output is exactly 1.0, which `Rng.Uniform01EndsAreStrictlyInside` caught.
- **American tree at sigma == 0**: the best exercise time on the deterministic path instead of
  the European limit, which undervalues an in-the-money American put (`Crr.Limits`).
- **`IvStatus::NotConverged`** returns the best iterate; every other non-Ok status returns NaN
  (the design states both rules in different places).
- **MC coverage comment**: the binomial tail P(count > 8) is 3.8e-4, not the design's "~0.2%".

## Not run

- GCC and Linux: not available on this machine. The CI matrix (`.github/workflows/ci.yml`, gcc
  and clang on ubuntu-24.04, release and asan) covers them once the repository is pushed.
- `clang-format` and `clang-tidy`: not installed on this machine, so the code was formatted by
  hand to the `.clang-format` style.
- `tsan` and `release-lto`: the plan requires them from Part 2 on.
