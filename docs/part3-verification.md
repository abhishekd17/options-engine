# Part 3 verification (volatility surface on real data)

Evidence for Part 3: the exact commands that were run and their output, so a reviewer can check
the results without re-running anything.

Machine: Apple M5 Pro (arm64), macOS 26.7.1 (`sw_vers -productVersion`). `c++ --version`:
Apple clang version 21.0.0 (clang-2100.3.34.2). `opt_info`: `simd backend: neon (width 2)`.
Every run below started with `export PATH=/opt/homebrew/bin:$PATH`, from
`/Users/abhilnk/projects/options-engine`. Date of the runs: 2026-10-17.

## What Part 3 added

| Piece | Files |
|---|---|
| Fetch and trim scripts (bash, curl, jq; public HTTPS GETs, no auth) | `scripts/fetch_deribit.sh`, `scripts/trim_snapshot.sh` |
| Committed real data (offline tests) | `tests/data/deribit/BTC-20261017T123254Z/`, `tests/data/deribit/series/` (6 snapshots), `tests/data/deribit/README.md` |
| Snapshot loader (`opt::deribit`, nlohmann/json private) | `include/opt/deribit/snapshot.hpp`, `src/deribit/snapshot.cpp` |
| Market IVs, mark comparison, fit-point selection | `include/opt/vol/market_iv.hpp`, `src/vol/market_iv.cpp` (built into `opt_deribit`) |
| Raw SVI, Zeliade + Huber fit | `include/opt/vol/svi.hpp`, `include/opt/vol/svi_fit.hpp`, `src/vol/svi_fit.cpp` |
| Butterfly, calendar and Lee checks | `include/opt/vol/arbitrage.hpp`, `src/vol/arbitrage.cpp` |
| Surface (total-variance interpolation in tau) | `include/opt/vol/surface.hpp`, `src/vol/surface.cpp` |
| Tool | `tools/vol_surface.cpp` (CSV and SVG output) |
| Tests | `tests/vol/*.cpp` (`opt_vol_tests`), `tests/deribit/snapshot_test.cpp` (`opt_deribit_tests`), `tests/vol_surface_cli_test.cmake` (`cli.vol_surface`) |
| Docs | `docs/conventions.md` (Deribit conventions, mark check, SVI domain), `docs/accuracy.md` (SVI fit section) |

Deviations from the plan, all small:

- `svi.hpp` is header-only (5 one-line inline functions), so there is no `src/vol/svi.cpp`.
- `market_iv.cpp` is compiled into `opt_deribit`, not `opt_vol`: it reads a `deribit::Snapshot`,
  and `opt_deribit` already links `opt_vol`, so putting it in `opt_vol` would make a cycle.
- The 10,000-step `validate_order()` test needs the Part 4 synthetic generator; it is added in
  Part 4 (noted in `tests/vol/surface_test.cpp`).
- Vogt citation: the arXiv version (1204.0646v1) has the example as Example 4.1 in Section 4,
  with g in eq. (4.1) and Lemma 4.1 (checked on arxiv.org). The code cites that version, since
  the published numbering could not be checked. The parameter values are unchanged.

## Data acquisition

The Deribit API was reachable. Commands and output:

```text
$ scripts/fetch_deribit.sh --bogus; echo "rc=$?"
error: unknown flag: --bogus
Usage: scripts/fetch_deribit.sh [--currency BTC|ETH] [--out DIR] [--count N] [--interval SEC]
...
rc=2
$ scripts/fetch_deribit.sh --count 1
wrote data/deribit/BTC-20261017T123254Z (938 book rows)
$ caffeinate -i scripts/fetch_deribit.sh --count 6 --interval 300 --out data/deribit/series-raw
wrote data/deribit/series-raw/BTC-20261017T123312Z (938 book rows)
wrote data/deribit/series-raw/BTC-20261017T123812Z (938 book rows)
wrote data/deribit/series-raw/BTC-20261017T124312Z (938 book rows)
wrote data/deribit/series-raw/BTC-20261017T124812Z (938 book rows)
wrote data/deribit/series-raw/BTC-20261017T125312Z (938 book rows)
wrote data/deribit/series-raw/BTC-20261017T125812Z (938 book rows)
```

Raw full chain: 12 expiries, 938 options; `book_summary.json` 418,308 bytes, `instruments.json`
819,246 bytes. Trimming all 12 expiries gave 420 KB (`du -sk`), above the 400 KB budget, so the
committed copy keeps the 8 nearest expiries:

```text
$ scripts/trim_snapshot.sh data/deribit/BTC-20261017T123254Z tests/data/deribit \
    --expiries 18OCT26,19OCT26,20OCT26,21OCT26,23OCT26,30OCT26,6NOV26,27NOV26
wrote tests/data/deribit/BTC-20261017T123254Z (524 book rows, 240 KB)
$ for d in data/deribit/series-raw/BTC-*; do
    scripts/trim_snapshot.sh "$d" tests/data/deribit/series --expiries 19OCT26,20OCT26; done
wrote tests/data/deribit/series/BTC-20261017T123312Z (114 book rows, 64 KB)
... (6 lines, 114 rows and 64 KB each)
$ du -sk tests/data/deribit/series tests/data/deribit/BTC-*
384	tests/data/deribit/series
240	tests/data/deribit/BTC-20261017T123254Z
```

The series expiries (19OCT26, 20OCT26) were chosen by the jq query in
`tests/data/deribit/README.md`: the 2nd and 3rd nearest expiries with at least 10 strikes.

Failure path, with all network access denied by a macOS sandbox profile: the script prints the
error, exits 1 and leaves the output directory empty.

```text
$ sandbox-exec -p '(version 1)(allow default)(deny network*)' scripts/fetch_deribit.sh --out /tmp/oe-fail; echo "rc=$?"; ls -A /tmp/oe-fail
Deribit API unreachable or returned an error; no snapshot written: curl: curl: (6) Could not resolve host: www.deribit.com
...
rc=1
```

ETH (optional, not committed): `scripts/fetch_deribit.sh --currency ETH` wrote
`data/deribit/ETH-20261017T213058Z` (812 book rows); `vol_surface` on it printed
`OTM |iv_mark - mark_iv| n=394 median 0.0120 p95 0.0981 max 0.6708` and
`total: 11 of 12 expiries fitted (Ok 11), mean rmse 1.2252 vol pts, max rmse 2.7202 vol pts;
butterfly violations in 0 slices, calendar violations in 1 pairs, Lee violations in 0 slices`.

## Clean builds and test runs

Each preset was built from a deleted build directory. Warnings are errors on project targets.

```bash
rm -rf build/release build/asan build/tsan
for p in release asan tsan; do
  caffeinate -i cmake --preset $p > /tmp/oe-$p-cfg.log 2>&1 &&
  caffeinate -i cmake --build --preset $p > /tmp/oe-$p-build.log 2>&1 &&
  caffeinate -i ctest --preset $p > /tmp/oe-$p-ctest.log 2>&1
  echo "$p rc=$?"
done
grep -c "warning:" /tmp/oe-{release,asan,tsan}-build.log
```

| Preset | rc | Compiler warnings | Errors | ctest |
|---|---|---|---|---|
| release | 0 | 0 | 0 | `100% tests passed out of 150`, 14.11 s |
| asan (ASan + UBSan) | 0 | 0 | 0 | `100% tests passed out of 150`, 25.71 s |
| tsan | 0 | 0 | 0 | `100% tests passed out of 150`, 60.04 s |

Part 2 ended with 120 tests; Part 3 adds 30: 21 in `opt_vol_tests`, 8 in `opt_deribit_tests`
and `cli.vol_surface`.

Offline: ctest under the same no-network sandbox (curl inside it fails with `Could not resolve
host`):

```text
$ sandbox-exec -p '(version 1)(allow default)(deny network*)' ctest --preset release
...
100% tests passed out of 150
Total Test time (real) =   9.39 sec
```

## Test output with measured values

`./build/release/tests/opt_vol_tests` (printed lines):

```text
  truth (0.04, 0.1, -0.3, 0, 0.1): err a 1.44e-13 b -1.63e-13 rho -7.72e-15 m 3.36e-13 s -2.30e-12, rmse 6.35e-12 vol pts, 292 iterations
  truth (0.02, 0.2, -0.6, 0.1, 0.2): err a -6.31e-16 b 1.05e-15 rho 4.11e-15 m 0.00e+00 s 0.00e+00, rmse 4.66e-14 vol pts, 267 iterations
  truth (0.01, 0.05, 0.2, -0.1, 0.3): err a 2.83e-13 b -2.29e-13 rho 3.29e-12 m 1.71e-12 s -4.49e-12, rmse 5.99e-12 vol pts, 295 iterations
  truth (0.1, 0.3, -0.9, 0.2, 0.05): err a 1.08e-13 b -1.45e-13 rho -3.78e-13 m 8.71e-14 s -1.01e-12, rmse 5.86e-12 vol pts, 312 iterations
  noisy set (0.04, 0.1, -0.3, 0, 0.1): rmse 0.0985 vol pts
  noisy set (0.02, 0.2, -0.6, 0.1, 0.2): rmse 0.1018 vol pts
  noisy set (0.01, 0.05, 0.2, -0.1, 0.3): rmse 0.1046 vol pts
  noisy set (0.1, 0.3, -0.9, 0.2, 0.05): rmse 0.1012 vol pts
  a < 0: active-set objective 6.889550e-03, grid minimum 6.940167e-03
  c > 4s: active-set objective 7.010602e+00, grid minimum 7.011404e+00
  error on the 40 clean points: plain LS 0.4450 vol pts, Huber 0.0617 vol pts
  Vogt: min g -0.03286 at k 0.877, 82 of 401 points negative
  tau 0.004960: 17 points, rmse 0.7844 vol pts, max 1.8791 vol pts, Ok
  tau 0.007700: 23 points, rmse 0.5064 vol pts, max 1.1907 vol pts, Ok
  tau 0.010440: 24 points, rmse 0.7376 vol pts, max 2.8417 vol pts, Ok
  tau 0.015919: 19 points, rmse 1.0629 vol pts, max 3.5133 vol pts, Ok
  tau 0.035097: 45 points, rmse 0.9657 vol pts, max 5.2648 vol pts, Ok
  tau 0.054275: 18 points, rmse 0.2156 vol pts, max 0.5944 vol pts, Ok
  tau 0.111810: 48 points, rmse 0.5821 vol pts, max 2.2295 vol pts, Ok
[  PASSED  ] 21 tests.
```

`./build/release/tests/opt_deribit_tests`:

```text
  OTM |iv_mark - mark_iv|: n=256 median 0.0027 p95 0.0063 max 0.0927 vol pts
[  PASSED  ] 8 tests.
```

## vol_surface on the committed snapshot

```text
$ time ./build/release/tools/vol_surface tests/data/deribit/BTC-20261017T123254Z --csv /tmp/oe-surface.csv --svg /tmp/oe-surface.svg
snapshot: tests/data/deribit/BTC-20261017T123254Z
currency BTC  valuation 2026-10-17T12:32:52.212Z  index price 81437.86
quotes 524  expiries 8  skipped: forward_mismatch=205
mark convention check (Black-76 on price_btc x F, r = 0, ACT/365), vol points:
  OTM |iv_mark - mark_iv|    n=256 median 0.0027 p95 0.0063 max 0.0927
  ITM |iv_mark - mark_iv|    n=249 median 0.0134 p95 0.4692 max 12.4881
  OTM iv_mid - mark_iv       n=203 median +0.0186 p05 -0.3428 p95 +0.6702 max|.| 4.1208
  ITM iv_mid - mark_iv       n=221 median +0.1906 p05 -3.3384 p95 +10.2596 max|.| 47.6985
  mark IV status:            Ok=505 BelowIntrinsic=19
  mid IV status:             Ok=424 InvalidInput=59 BelowIntrinsic=41
expiry 18OCT26  tau 0.002221 (0.81 d)  F 81443.94  points 9  not fitted: TooShort
expiry 19OCT26  tau 0.004960 (1.81 d)  F 81462.42  points 17  status Ok
  svi a 1.52202e-20 b 0.0123428 rho -0.623684 m -0.0184241 s 0.0230447
  fit rmse 0.7844 vol pts  max 1.8791 vol pts  iterations 217
expiry 20OCT26  tau 0.007700 (2.81 d)  F 81479.03  points 23  status Ok
  svi a 1.15779e-18 b 0.0159897 rho -0.597507 m -0.0221292 s 0.0397664
  fit rmse 0.5064 vol pts  max 1.1907 vol pts  iterations 210
expiry 21OCT26  tau 0.010440 (3.81 d)  F 81472.45  points 24  status Ok
  svi a -2.23078e-19 b 0.0210726 rho -0.59688 m -0.0259447 s 0.0445785
  fit rmse 0.7376 vol pts  max 2.8417 vol pts  iterations 213
expiry 23OCT26  tau 0.015919 (5.81 d)  F 81526.32  points 19  status Ok
  svi a 1.10153e-19 b 0.0253258 rho -0.461367 m -0.0140002 s 0.0564154
  fit rmse 1.0629 vol pts  max 3.5133 vol pts  iterations 210
expiry 30OCT26  tau 0.035097 (12.81 d)  F 81637.69  points 45  status Ok
  svi a 3.78682e-18 b 0.0421378 rho -0.466723 m -0.0274154 s 0.0898355
  fit rmse 0.9657 vol pts  max 5.2648 vol pts  iterations 214
expiry 6NOV26   tau 0.054275 (19.81 d)  F 81715.29  points 18  status Ok
  svi a 1.3339e-18 b 0.0530222 rho -0.561098 m -0.0615582 s 0.15106
  fit rmse 0.2156 vol pts  max 0.5944 vol pts  iterations 203
expiry 27NOV26  tau 0.111810 (40.81 d)  F 81967.84  points 48  status Ok
  svi a -5.52315e-18 b 0.0773157 rho -0.429077 m -0.0585727 s 0.197052
  fit rmse 0.5821 vol pts  max 2.2295 vol pts  iterations 205
static arbitrage (401-point grids, data range +- 0.5 in k):
  butterfly 19OCT26  min g 0.08682 at k -0.062  violations 0/401  Lee slopes b(1+rho) 0.005 b(1-rho) 0.020 ok
  butterfly 20OCT26  min g 0.1492 at k -0.108  violations 0/401  Lee slopes b(1+rho) 0.006 b(1-rho) 0.026 ok
  butterfly 21OCT26  min g 0.1364 at k -0.122  violations 0/401  Lee slopes b(1+rho) 0.008 b(1-rho) 0.034 ok
  butterfly 23OCT26  min g 0.2038 at k -0.200  violations 0/401  Lee slopes b(1+rho) 0.014 b(1-rho) 0.037 ok
  butterfly 30OCT26  min g 0.1932 at k -0.300  violations 0/401  Lee slopes b(1+rho) 0.022 b(1-rho) 0.062 ok
  butterfly 6NOV26   min g 0.183 at k -0.459  violations 0/401  Lee slopes b(1+rho) 0.023 b(1-rho) 0.083 ok
  butterfly 27NOV26  min g 0.2029 at k -0.699  violations 0/401  Lee slopes b(1+rho) 0.044 b(1-rho) 0.110 ok
  calendar  19OCT26 -> 20OCT26  min (w2 - w1) 0.0002603 at k 0.036  violations 0/401
  calendar  20OCT26 -> 21OCT26  min (w2 - w1) 0.0002438 at k 0.007  violations 0/401
  calendar  21OCT26 -> 23OCT26  min (w2 - w1) 0.0004942 at k 0.029  violations 0/401
  calendar  23OCT26 -> 30OCT26  min (w2 - w1) 0.00206 at k 0.043  violations 0/401
  calendar  30OCT26 -> 6NOV26  min (w2 - w1) 0.001928 at k 0.646  violations 0/401
  calendar  6NOV26 -> 27NOV26  min (w2 - w1) 0.007119 at k 0.018  violations 0/401
total: 7 of 8 expiries fitted (Ok 7), mean rmse 0.6935 vol pts, max rmse 1.0629 vol pts; butterfly violations in 0 slices, calendar violations in 0 pairs, Lee violations in 0 slices
wrote /tmp/oe-surface.csv
wrote /tmp/oe-surface.svg
... 0.17s user 0.01s system 16% cpu 1.056 total
```

The CSV has 525 lines (header + 524 quotes). Of its 194 fit points, 45 have a fitted vol
outside their bid-ask IV range (counted with awk on columns `iv_bid`, `iv_ask`, `iv_fit`,
`used_in_fit`; per expiry in `docs/accuracy.md`).

### IV vs Deribit mark_iv

| Comparison (vol points) | Quotes | Median | p95 | Max |
|---|---|---|---|---|
| OTM \|iv_mark - mark_iv\| | 256 | 0.0027 | 0.0063 | 0.0927 |
| ITM \|iv_mark - mark_iv\| | 249 | 0.0134 | 0.4692 | 12.4881 |
| OTM iv_mid - mark_iv | 203 | +0.0186 | +0.6702 | 4.1208 |
| ITM iv_mid - mark_iv | 221 | +0.1906 | +10.2596 | 47.6985 |

OTM marks match Deribit's `mark_iv` to 0.0027 vol points (median), below the 0.005 rounding
of `mark_iv` itself; the test bound is 0.05. Reasons for the ITM tail are in
`docs/conventions.md`.

### Per-expiry SVI fit RMSE

| Expiry | Days | Points | RMSE (vol pts) | Max error (vol pts) | Status |
|---|---|---|---|---|---|
| 18OCT26 | 0.81 | 9 | not fitted (TooShort, < 1 day) | | |
| 19OCT26 | 1.81 | 17 | 0.7844 | 1.8791 | Ok |
| 20OCT26 | 2.81 | 23 | 0.5064 | 1.1907 | Ok |
| 21OCT26 | 3.81 | 24 | 0.7376 | 2.8417 | Ok |
| 23OCT26 | 5.81 | 19 | 1.0629 | 3.5133 | Ok |
| 30OCT26 | 12.81 | 45 | 0.9657 | 5.2648 | Ok |
| 6NOV26 | 19.81 | 18 | 0.2156 | 0.5944 | Ok |
| 27NOV26 | 40.81 | 48 | 0.5821 | 2.2295 | Ok |

### Raw 12-expiry chain (not committed)

`./build/release/tools/vol_surface data/deribit/BTC-20261017T123254Z` on the untrimmed fetch
gives the same numbers for the 8 committed expiries, plus:

```text
  OTM |iv_mark - mark_iv|    n=463 median 0.0028 p95 0.0050 max 0.0927
expiry 25DEC26  tau 0.188522 (68.81 d)  F 82310.76  points 57  status Ok
  fit rmse 3.2690 vol pts  max 8.3611 vol pts  iterations 203
expiry 26MAR27  tau 0.437837 (159.81 d)  F 83346.44  points 52  status Ok
  fit rmse 0.5359 vol pts  max 2.0862 vol pts  iterations 196
expiry 25JUN27  tau 0.687152 (250.81 d)  F 84455.31  points 54  status Ok
  fit rmse 0.2738 vol pts  max 0.6845 vol pts  iterations 207
expiry 24SEP27  tau 0.936467 (341.81 d)  F 85536.36  points 42  status Ok
  fit rmse 0.1819 vol pts  max 0.4394 vol pts  iterations 206
  calendar  25JUN27 -> 24SEP27  min (w2 - w1) -0.01631 at k -1.535  violations 39/401
total: 11 of 12 expiries fitted (Ok 11), mean rmse 0.8286 vol pts, max rmse 3.2690 vol pts; butterfly violations in 0 slices, calendar violations in 1 pairs, Lee violations in 0 slices
```

The calendar check finds a real crossing of the two longest slices far in the put wing, and
25DEC26 fits poorly. Both trace to the Zeliade a >= 0 bound (every fit has a = 0 to
rounding). A scratch build with that bound relaxed (not committed) lowered the RMSE on every
expiry, 25DEC26 to 0.8274. See "Known limitation" in `docs/accuracy.md`.
