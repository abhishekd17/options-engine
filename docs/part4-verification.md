# Part 4 verification (options market maker and simulation)

Evidence for Part 4: the exact commands that were run and their output, so a reviewer can check
the results without re-running anything.

Machine: Apple M5 Pro (arm64), macOS 26.7.1 (`sw_vers -productVersion`). `c++ --version`:
Apple clang version 21.0.0 (clang-2100.3.34.2). cmake 4.4.4, ninja 1.13.2. Every run below
started with `export PATH=/opt/homebrew/bin:$PATH`, from `/Users/abhilnk/projects/options-engine`.
Date of the runs: 2026-10-17.

## What Part 4 added

| Piece | Files |
|---|---|
| Config and `validate(SimConfig)` | `include/opt/mm/config.hpp`, `src/mm/synthetic.cpp` |
| Market state and `MarketSource` concept | `include/opt/mm/market.hpp` |
| Synthetic generator (GBM spot, log-OU vol, SVI smile, arbitrage guard) | `include/opt/mm/synthetic.hpp`, `src/mm/synthetic.cpp` |
| Quoter (pure) | `include/opt/mm/quoter.hpp`, `src/mm/quoter.cpp` |
| Fill model (Poisson, "fill" stream) | `include/opt/mm/fill_model.hpp`, `src/mm/fill_model.cpp` |
| Portfolio and risk | `include/opt/mm/portfolio.hpp`, `src/mm/portfolio.cpp` |
| Hedger | `include/opt/mm/hedger.hpp`, `src/mm/hedger.cpp` |
| PnL attribution | `include/opt/mm/pnl.hpp`, `src/mm/pnl.cpp` |
| Simulator, events, fill hash, latency histogram | `include/opt/mm/sim.hpp`, `src/mm/sim.cpp` |
| Recorded source (`opt::mm_recorded`) | `include/opt/mm/recorded.hpp`, `src/mm_recorded/recorded.cpp` |
| Tool | `tools/mm_sim.cpp` |
| Tests | `tests/mm/*.cpp` (`opt_mm_tests`, 29 tests), `tests/alloc_test.cpp` (simulator case), `tests/mm_sim_cli_test.cmake` (`cli.mm_sim`) |
| Docs | `docs/mm_model.md` (model, defaults with units, biases, attribution, results) |

CMake targets: `opt_mm` (alias `opt::mm`, links `opt::batch`, `opt::vol`) and `opt_mm_recorded`
(alias `opt::mm_recorded`, links `opt::mm`, `opt::deribit`), as in design section 3.

Deviations from the design and plan, all small:

- `MarketState` has two fields beyond design 8.2: `arbitrage_ok` (how the synthetic source
  reports an in-run guard failure to `Simulator::run`, which returns
  `SimStatus::ArbitrageViolation`) and `listed` (per-instrument flags from the recorded source;
  unlisted instruments keep their last mark and are not quoted).
- `make_quotes` takes a `LimitConfig` next to the `QuoterConfig`, because the pre-trade check
  needs the limits.
- Fills drawn in the same step are applied in order (instrument, then bid before ask), and a
  fill that would break a limit given the fills already applied in that step is dropped and
  counted as a "limit-rejected fill". Without this, two fills in one step could break a limit
  together even though each side passed the pre-trade check. Over the golden run and the
  full default day the count is 0; the 20,000-step tight-limit test has 1.
- Recorded mode: when a quoted expiry fails to fit, the source keeps the last good surface for
  marking and sets `surface_ok = false`, and the quoter then pulls quotes for all instruments
  in that state (design 8.2 says "for that expiry"). On the committed series every fit is Ok, so
  this path does not run (`steps without surface 0`).
- The fill hash serialises qty as the fill size in contracts (|qty|); side already gives the
  sign.
- Fills use the time since the previous state as dt. The first state of a recorded run has no
  previous state, so it has no fills.

## Clean builds and tests

All five presets were configured and built from a deleted build directory, then tested. Script
used (run under `caffeinate -i`):

```bash
for p in release asan tsan debug release-lto; do
  rm -rf build/$p
  cmake --preset $p > /tmp/oe-$p.log 2>&1 && cmake --build --preset $p >> /tmp/oe-$p.log 2>&1
  ctest --preset $p > /tmp/oe-$p-test.log 2>&1
done
```

| Preset | configure + build | compiler warnings (`grep -c "warning:"` on the build log) | ctest summary line | Total Test time |
|---|---|---|---|---|
| release | rc 0, 26 s | 0 | `100% tests passed out of 181` | 14.10 s |
| asan (ASan + UBSan) | rc 0, 31 s | 0 | `100% tests passed out of 181` | 29.30 s |
| tsan | rc 0, 29 s | 0 | `100% tests passed out of 181` | 68.56 s |
| debug | rc 0, 21 s | 0 | `100% tests passed out of 181` | 65.59 s |
| release-lto | rc 0, 23 s | 0 | `100% tests passed out of 181` | 19.20 s |

181 = the 150 tests of Part 3 plus 29 in `opt_mm_tests`, 1 new allocation test and
`cli.mm_sim`. The asan run stays under the 3-minute budget. The simulator is single-threaded;
the tsan run covers the whole suite, including the thread pool.

During development an incremental build printed one linker warning, `ld: warning: ignoring
duplicate libraries: 'src/libopt_batch.a'`, because `opt_alloc_tests` named both `opt::batch`
and `opt::mm`. It now links only `opt::mm` (which brings `opt::batch`), and the clean logs
have no warnings.

New tests, from `/tmp/oe-release-test.log` (all `Passed`):

```
Golden.SyntheticSeed42                 Golden.AttributionIdentityFullDay
Quoter.FlatBookQuotesSymmetricAroundFair   Quoter.QuotesWidenWithInventory
Quoter.MonotoneOverPositions           Quoter.WidensWithVegaAndGammaExposure
Quoter.SideThatWouldBreachIsInactive   Quoter.NotQuotableAndFloor   Quoter.SizeMismatchThrows
Limits.GoldenRunRespectsLimits         Limits.TightLimitsRespectedOver20000Steps
Hedger.StrictThreshold                 Hedger.ZeroThresholdHedgesAnyDelta
Hedger.SimulatorHedgesExactlyAboveThreshold
Pnl.HandcraftedTwoStepScenario         Pnl.LedgerMatchesSpreadCaptureAtFill
Pnl.PureDeltaMove                      Pnl.IdentityTolerance
FillModel.ProbabilityDecreasesWithDistance   FillModel.EmpiricalRateWithinFiveStandardErrors
Recorded.InstrumentsFromFirstSnapshot  Recorded.RunsToTheEndWithInvariants
Recorded.MissingDirectoryThrows
Synthetic.DefaultSmileHasNoStaticArbitrage   Synthetic.SameSeedSamePath
Synthetic.AtmTotalVarianceIsVolSquaredTau    Synthetic.StateSequenceAndInstruments
Synthetic.ConfigValidation             Synthetic.InRunGuardReportsViolation
Allocations.SyntheticSimulationStepAllocatesNothing
cli.mm_sim
```

How they map to design 8.10:

| 8.10 item | Test(s) |
|---|---|
| 1 golden run | `Golden.SyntheticSeed42` (exact counts, positions, FNV-1a hash; buckets and spot to 1e-9 relative) |
| 2 quotes widen with inventory | `Quoter.QuotesWidenWithInventory`, `Quoter.MonotoneOverPositions`, `Quoter.WidensWithVegaAndGammaExposure` |
| 3 limits respected | `Limits.*` (after every fill, golden run and 20,000 steps with tight limits), `Quoter.SideThatWouldBreachIsInactive` |
| 4 hedge trigger | `Hedger.StrictThreshold` (threshold - 1e-9, exactly at, + 1e-9), `Hedger.SimulatorHedgesExactlyAboveThreshold` |
| 5 attribution | `Pnl.HandcraftedTwoStepScenario` (hand-computed values), `Pnl.PureDeltaMove`, identity in `Golden.*` and `Recorded.RunsToTheEndWithInvariants` |
| 6 fill model | `FillModel.*` (1e6 draws within 5 SE) |
| 7 recorded mode | `Recorded.*` |
| 8 synthetic generator | `Synthetic.*` (plus the 7.9 `validate_order()` check after 10,000 steps) |
| 9 CLI | `cli.mm_sim` |

Printed measurements, `./build/release/tests/opt_mm_tests`:

```
full day: total 185881.972746 ledger 185881.972746 diff 5.821e-11 tol 2.493e-05
tight limits: fills 341  blocked sides 155039  rejected fills 1  max |q| 2  max |vega| 99.937  max |gamma_1pct| 0.09990  max |delta| 1.2918
pure delta move dS 10: delta 13.798180 gamma 0.005191945 residual -6.487e-08
pure delta move dS 20: delta 27.596359 gamma 0.020767782 residual -5.214e-07
pure delta move dS 40: delta 55.192718 gamma 0.083071126 residual -4.213e-06
fill rate d=0.0 dt=1: empirical 0.008270 expected 0.008299 (-0.32 SE)
fill rate d=1.0 dt=1: empirical 0.003043 expected 0.003061 (-0.33 SE)
fill rate d=0.5 dt=30: empirical 0.140923 expected 0.140696 (0.65 SE)
fill rate d=2.0 dt=300: empirical 0.287127 expected 0.287046 (0.18 SE)
recorded: fills 62 hedges 1 steps without surface 0  total 339.217804 ledger 339.217804 diff 6.139e-12
[==========] 29 tests from 8 test suites ran. (470 ms total)
[  PASSED  ] 29 tests.
```

The tight limits are `max_position 2`, `max_net_delta 1.5`, `max_vega 100`, `max_gamma_1pct
0.1`. The pure-delta residual grows by 8.0x and 8.1x when dS doubles, the dS^3 scaling of the
third-order term.

## Golden run capture

Captured after every other test passed, with:

```
OPTENG_PRINT_GOLDEN=1 ./build/release/tests/opt_mm_tests --gtest_filter='Golden.SyntheticSeed42'
```

which printed:

```
constexpr Golden kGolden{52, 48, 6, 0, 0,
  {4, 1, -2, -2, 4, -2, -4, 2, 2, 1},
  0xc351c1a7d762bfe0ULL,
  -110.26842489369244, -10.106817346277898, -71.283404331781696, 10.792731397085117,
  0.15990465913792945, 5168.3927195975057, 298.91821105026526, 4688.7684980317117, 80011.818243312693};
```

(bid fills, ask fills, hedges, limit-blocked sides, limit-rejected fills, final positions,
fill hash, PnL delta, gamma, vega, theta, residual, spread capture, hedge cost, total, final
spot). The same values pass unchanged under release (`-O3 -mcpu=native`), release-lto, asan
and tsan (`-O1`) and debug (`-O0`): `Golden.SyntheticSeed42` is `Passed` in all five ctest logs
above. `cli.mm_sim` checks the same numbers in the tool output. Cross-compiler identity (GCC
and Clang on Linux) is checked by CI, which has not run yet because nothing is pushed.

## Zero allocation

`Allocations.SyntheticSimulationStepAllocatesNothing` (in `opt_alloc_tests`, the counting
`operator new` executable): after a 10-step warm-up, 2,000 steps of generator + mark +
attribution + quotes + fills + hedges allocate 0 times, a window that includes the in-run
arbitrage guard at steps 1000 and 2000. `Passed` in all five presets.

## mm_sim, synthetic (seed 42, one day of 1-second steps)

```
./build/release/tools/mm_sim --mode synthetic --seed 42 --events /tmp/oe-mm-events.csv
```

Exit code 0. `--events` sends the event lines to a CSV (3,746 lines: header, 3,539 fills,
206 hedges; `wc -l`, `grep -c ",fill,"`, `grep -c ",hedge,"`). Its first lines:

```
step,t,kind,instrument,side,qty,price,fair,spread_capture,cost
6,1.90258752e-07,fill,SYN-30D-88000-C,buy,1,1845.084201,1922.366131,77.28192989,0
28,8.87874176e-07,fill,SYN-30D-72000-P,sell,-1,1842.264391,1770.780521,71.48386998,0
49,1.55377981e-06,fill,SYN-7D-98000-C,buy,1,17.61211218,20.23056977,2.618457589,0
92,2.91730086e-06,fill,SYN-30D-98000-C,sell,-1,729.5713365,683.6683122,45.90302432,0
```

Stdout (second of three runs):

```
mm_sim: mode synthetic  seed 42  steps 86400  dt 1 s
config: quote size 1  half spread 1.00 volpt  w_inv 1.00 w_vega 1.00 w_gamma 0.50  k_inv 0.50 k_vega 0.50
limits: |position| 10  |net delta| 5.000 BTC  |vega| 2000.0 USD/volpt  |gamma_1pct| 1.000 BTC
hedge: threshold 1.000 BTC  cost 5.00 bps   fills: A 0.00833333 /s  kappa 1.000 /volpt
synthetic: S0 80000.00  v0 0.5000  vol mean 0.5000  kappa 5.000 /yr  xi 1.000 /sqrt(yr)  rho -0.300  expiries 7d 30d 90d  SVI beta 0.400 rho -0.200 m 0.000 s 0.100
instruments 10:
  [0] SYN-7D-65000-P         put  K   65000.0  expiry 7.0000 d
  [1] SYN-7D-72000-P         put  K   72000.0  expiry 7.0000 d
  [2] SYN-7D-80000-C         call K   80000.0  expiry 7.0000 d
  [3] SYN-7D-88000-C         call K   88000.0  expiry 7.0000 d
  [4] SYN-7D-98000-C         call K   98000.0  expiry 7.0000 d
  [5] SYN-30D-65000-P        put  K   65000.0  expiry 30.0000 d
  [6] SYN-30D-72000-P        put  K   72000.0  expiry 30.0000 d
  [7] SYN-30D-80000-C        call K   80000.0  expiry 30.0000 d
  [8] SYN-30D-88000-C        call K   88000.0  expiry 30.0000 d
  [9] SYN-30D-98000-C        call K   98000.0  expiry 30.0000 d
   state   t_hours       spot atm_vol net_delta gamma_1pct vega/volpt  theta/day open  quotes [0] and [1]: bid/ask vol pts (price)
       0    0.0000   80000.00  0.5000   +0.0000   +0.00000     +0.000     +0.000    0  69.60/71.60 (38.61/46.95) 57.80/59.80 (267.08/303.36)
    3600    1.0000   79561.62  0.5009   -0.0048   -0.17741   -185.947   +502.073    8  68.54/71.50 (39.79/52.98) 57.11/59.68 (291.59/341.17)
    7200    2.0000   80203.05  0.4992   -0.2290   +0.03312   -288.978   -114.691    9  69.51/72.24 (34.08/44.68) 57.97/60.29 (246.79/287.01)
   10800    3.0000   79904.88  0.4975   -0.6155   -0.09662   -255.186   +333.784    8  69.04/72.20 (35.42/48.28) 57.33/59.88 (257.97/303.73)
   14400    4.0000   80217.82  0.4961   -0.4126   +0.02584    -41.897   -133.773    9  69.58/72.05 (32.67/41.88) 57.50/59.97 (232.09/273.49)
   18000    5.0000   80151.02  0.4952   -0.0940   +0.13916    +92.717   -612.099   10  69.28/71.71 (31.71/40.60) 56.32/60.15 (215.77/279.38)
   21600    6.0000   80382.74  0.4935   +0.9888   +0.33834   +167.678   -959.712   10  69.03/72.13 (27.66/38.18) 57.45/60.36 (213.26/259.82)
   25200    7.0000   80742.95  0.4900   +0.5918   -0.04182   -246.238   +130.024   10  69.27/72.36 (24.22/33.65) 57.65/61.14 (188.44/240.63)
   28800    8.0000   80030.84  0.4968   -0.1568   -0.35565   -581.766   +913.400   10  68.96/72.50 (29.94/42.82) 57.30/60.64 (232.32/288.90)
   32400    9.0000   79969.23  0.4964   -0.7179   -0.09795    +77.685    +61.417    9  68.21/72.18 (27.59/41.55) 57.22/59.79 (233.06/276.29)
   36000   10.0000   80046.87  0.4872   -0.4274   +0.05976   +314.757   -331.810   10  67.53/71.30 (24.21/36.25) 56.11/59.08 (206.89/254.19)
   39600   11.0000   80100.30  0.4828   +0.3546   -0.16486   -261.133   +373.725   10  67.47/71.09 (22.96/34.03) 56.19/58.82 (201.52/242.30)
   43200   12.0000   80662.76  0.4855   -0.5278   +0.25356    -39.556   -631.492   10  68.62/72.11 (20.46/30.05) 57.37/60.06 (177.26/215.42)
   46800   13.0000   81053.58  0.4928   -0.7689   -0.19685   -116.688   +518.283    9  70.33/73.44 (20.84/29.19) 58.38/61.29 (164.00/203.00)
   50400   14.0000   80144.08  0.5043   -0.7868   -0.19122    +65.832   +600.835   10  70.74/73.20 (30.09/38.53) 58.22/61.07 (220.51/266.30)
   54000   15.0000   80234.78  0.5042   +0.4650   +0.08747   -512.803   -253.142    9  71.05/73.65 (29.31/38.07) 58.34/61.54 (212.69/263.02)
   57600   16.0000   80427.37  0.5105   +0.7551   -0.10087   -365.885   +201.439    9  72.09/74.56 (29.67/37.91) 58.58/62.65 (199.38/261.51)
   61200   17.0000   81029.88  0.5093   -0.6574   -0.08958   -224.980   +284.553    9  72.49/75.41 (24.11/32.52) 60.03/63.15 (176.71/219.51)
   64800   18.0000   80630.94  0.5071   +0.1617   +0.05752   +360.086   -303.462    9  71.70/74.51 (25.07/33.49) 58.75/62.36 (182.48/234.28)
   68400   19.0000   81250.42  0.5040   -0.1516   +0.04116    -64.648   -117.722    8  72.88/74.98 (21.95/27.41) 60.55/62.65 (164.88/191.88)
   72000   20.0000   80423.32  0.5101   -0.9267   +0.19022   -190.503   -451.610   10  71.85/74.83 (26.20/35.43) 59.76/62.54 (205.26/246.86)
   75600   21.0000   80085.91  0.5132   -0.0526   +0.14523    -15.651   -482.317    8  71.20/74.76 (27.14/38.75) 59.75/61.91 (227.52/261.56)
   79200   22.0000   80145.18  0.5149   +0.2725   +0.11455    -34.179   -469.173   10  71.55/75.10 (26.81/38.22) 59.76/62.31 (219.93/259.43)
   82800   23.0000   80186.93  0.5152   +0.7492   +0.18318   +257.017   -696.577    9  72.54/74.98 (28.52/36.33) 59.21/62.65 (205.85/258.02)
   86400   24.0000   79747.96  0.5162   -0.3736   +0.12612   -200.656   -496.504    9  71.48/74.81 (29.46/40.83) 58.85/62.18 (231.46/285.56)
events written to /tmp/oe-mm-events.csv
final report
  states 86401  fills bid 1772 ask 1767  hedges 206  limit-blocked sides 3028  limit-rejected fills 0
  steps without surface 0  unlisted instrument-steps 0
  fill hash 0xf358fe64ceb0d637
  final positions: 5 5 -1 8 -5 -2 -4 4 -5 0
  hedge position -1.853332 BTC  final spot 79747.9589  final t 1.000000 d
  final greeks: net delta -0.3736 BTC  gamma_1pct +0.12612 BTC  vega -200.656 USD/volpt  theta -496.504 USD/day
PnL attribution (USD):
  delta                 +371.776047
  gamma                  +82.785988
  vega                  -427.050881
  theta                 -135.043049
  residual               -66.321536
  spread capture     +195736.716711
  hedge cost           -9680.890533
  total              +185881.972746
  ledger total       +185881.972746
  difference       5.821e-11  (tolerance 2.493e-05: ok)
latency per quote update (mark + quote of 10 options, steady_clock ns):
  count 86401  p50 1215  p90 1255  p99 1503  p99.9 1791  max 23792
  per option: p50 121.5  p90 125.5  p99 150.3  p99.9 179.1  max 2379.2
```

Status-line columns: quotes in `[ ]` are inactive sides (blocked by a limit). The status line
in the output above shows the quotes of instruments 0 and 1.

Three runs of the same command: everything except the latency section was identical (`diff`
of the outputs without the latency lines printed nothing). Latency lines of the three runs:

```
run 1:  count 86401  p50 1215  p90 1255  p99 1503  p99.9 3583  max 186584
run 2:  count 86401  p50 1215  p90 1255  p99 1503  p99.9 1791  max 23792
run 3:  count 86401  p50 1215  p90 1255  p99 1543  p99.9 4735  max 23834
```

Clock resolution: `steady_clock` on this machine ticks every 41.67 ns (`mach_timebase_info`
numer 125 / denom 3; smallest non-zero `steady_clock` differences 41, 42, 83, 84, 125 ns, from
a short program in `/tmp/oe-tick/tick.cpp`, shown in `docs/mm_model.md`).

## mm_sim, recorded (committed Deribit series)

```
./build/release/tools/mm_sim --mode recorded --series tests/data/deribit/series
```

Exit code 0. Shown below without the three config lines (same as in the synthetic run) and
without the 62 fill and 1 hedge event lines between the status lines:

```
mm_sim: mode recorded  seed 42  series tests/data/deribit/series
snapshots 6
instruments 10:
  [0] BTC-19OCT26-71000-P    put  K   71000.0  expiry 1.8103 d
  [1] BTC-19OCT26-74000-P    put  K   74000.0  expiry 1.8103 d
  [2] BTC-19OCT26-81500-C    call K   81500.0  expiry 1.8103 d
  [3] BTC-19OCT26-90000-C    call K   90000.0  expiry 1.8103 d
  [4] BTC-19OCT26-91000-C    call K   91000.0  expiry 1.8103 d
  [5] BTC-20OCT26-72000-P    put  K   72000.0  expiry 2.8103 d
  [6] BTC-20OCT26-74000-P    put  K   74000.0  expiry 2.8103 d
  [7] BTC-20OCT26-81500-C    call K   81500.0  expiry 2.8103 d
  [8] BTC-20OCT26-90000-C    call K   90000.0  expiry 2.8103 d
  [9] BTC-20OCT26-92000-C    call K   92000.0  expiry 2.8103 d
   state   t_hours       spot atm_vol net_delta gamma_1pct vega/volpt  theta/day open  quotes [0] and [1]: bid/ask vol pts (price)
       0    0.0000   81451.76  0.2091   +0.0000   +0.00000     +0.000     +0.000    0  68.44/70.44 (2.31/3.13) 55.67/57.67 (7.01/9.39)
       1    0.0839   81411.28  0.2122   +0.0001   +0.00011     +0.025     -0.168    2  68.60/70.60 (2.43/3.28) 55.61/57.61 (7.16/9.58)
       2    0.1667   81408.30  0.2134   -0.0078   +0.00533     +1.217    -19.157    3  68.80/70.80 (2.50/3.36) 55.70/57.70 (7.24/9.69)
       3    0.2511   81399.70  0.2121   +0.4914   +0.17506    +28.340   -141.807    7  68.67/70.67 (2.43/3.28) 55.49/57.69 (7.00/9.66)
       4    0.3332   81383.10  0.2160   -0.0000   +0.60204    +76.940   -382.139    8  68.78/70.99 (2.49/3.44) 55.43/57.84 (7.00/9.92)
       5    0.4168   81398.72  0.2184   -0.9479   +0.15810    +23.681    -87.755    8  68.78/71.46 (2.43/3.61) 55.51/58.19 (6.93/10.20)
final report
  states 6  fills bid 29 ask 33  hedges 1  limit-blocked sides 0  limit-rejected fills 0
  steps without surface 0  unlisted instrument-steps 0
  fill hash 0x43038f3bd35ba32f
  final positions: -1 0 0 -1 -1 1 -2 1 -2 1
  hedge position -1.464814 BTC  final spot 81398.7200  final t 0.017369 d
  final greeks: net delta -0.9479 BTC  gamma_1pct +0.15810 BTC  vega +23.681 USD/volpt  theta -87.755 USD/day
PnL attribution (USD):
  delta                   -8.091110
  gamma                   +0.120118
  vega                   +12.140367
  theta                   -1.884710
  residual                -2.994049
  spread capture        +399.532758
  hedge cost             -59.605571
  total                 +339.217804
  ledger total          +339.217804
  difference       6.139e-12  (tolerance 1.127e-06: ok)
latency per quote update (mark + quote of 10 options, steady_clock ns):
  count 6  p50 3167  p90 8542  p99 8542  p99.9 8542  max 8542
  per option: p50 316.7  p90 854.2  p99 854.2  p99.9 854.2  max 854.2
```

The one hedge, from the event lines: `hedge  state       4  sell 1.464814 BTC @ 81383.10  cost
59.6056`.

## Error paths (from `cli.mm_sim`)

| Command | Exit | Message matched |
|---|---|---|
| `mm_sim --bogus` | 2 | `unknown flag '--bogus'`, `usage:` |
| `mm_sim --mode sideways` | 2 | `--mode` |
| `mm_sim --mode recorded` | 2 | `needs --series` |
| `mm_sim --steps 700000` (8.1 days > 7-day first expiry) | 2 | `horizon ... must be shorter than the first expiry` |
| `mm_sim --mode recorded --series <missing dir>` | 1 | `error: ` |
