# Benchmarks (macOS preliminary)

**macOS preliminary.** These numbers come from one run on a developer laptop that was not idle
(other applications were using CPU; see "Machine load" below). They are not from a Linux server
with pinned cores and a fixed clock. Every number on this page is pasted from the output of
`scripts/run_benchmarks.sh`; nothing is typed in by hand. The raw Google Benchmark JSON is
committed next to this file.

## How this was run

```bash
export PATH=/opt/homebrew/bin:$PATH
caffeinate -i scripts/run_benchmarks.sh > /tmp/oe-bench.md
```

The script configures and builds the `release` preset, runs `batch_bench`, `pricing_bench`,
`iv_bench` and `tree_mc_bench` with `--benchmark_repetitions=5`, stores the JSON in
`docs/bench-results/`, and prints the report below: for every benchmark the median of the 5
repetitions and, in brackets, the minimum and maximum. Its output, unchanged:

- CPU: Apple M5 Pro, 18 logical CPUs
- OS: macOS 26.7.1
- Compiler: Apple clang version 21.0.0 (clang-2100.3.34.2)
- Build: `release` preset (-O3 -DNDEBUG, -mcpu/-march=native)
- simd backend: neon (width 2)
- Date (UTC): 2026-10-17T09:38:51Z
- Command: `scripts/run_benchmarks.sh`
- Raw JSON: `docs/bench-results/2026-10-17-macos-apple-m5-pro.json`
- Each cell: median of 5 repetitions, then [min .. max].

### batch_bench

| Benchmark | real time | Mopt/s | ns/option | pool_threads |
|---|---|---|---|---|
| `BM_Price/Reference/4096` | 152678 [151274 .. 174870] ns | 26.87 [24.52 .. 27.11] | 37.22 [36.89 .. 40.79] |  |
| `BM_Price/Reference/1048576` | 4.402e7 [4.397e7 .. 4.432e7] ns | 23.85 [23.68 .. 23.88] | 41.93 [41.88 .. 42.22] |  |
| `BM_Price/FastScalar/4096` | 76765 [75785 .. 80523] ns | 53.48 [52.11 .. 54.11] | 18.7 [18.48 .. 19.19] |  |
| `BM_Price/FastScalar/1048576` | 1.966e7 [1.947e7 .. 1.982e7] ns | 53.46 [53.1 .. 53.94] | 18.7 [18.54 .. 18.83] |  |
| `BM_Price/Simd/4096` | 40963 [40941 .. 41684] ns | 100.1 [98.55 .. 100.2] | 9.989 [9.984 .. 10.15] |  |
| `BM_Price/Simd/1048576` | 1.064e7 [1.063e7 .. 1.067e7] ns | 98.67 [98.38 .. 98.79] | 10.14 [10.12 .. 10.16] |  |
| `BM_Greeks/Reference/4096` | 188373 [187652 .. 191613] ns | 21.77 [21.43 .. 21.85] | 45.94 [45.76 .. 46.67] |  |
| `BM_Greeks/Reference/1048576` | 5.946e7 [5.926e7 .. 6.005e7] ns | 17.67 [17.48 .. 17.72] | 56.6 [56.44 .. 57.2] |  |
| `BM_Greeks/FastScalar/4096` | 109294 [108890 .. 111662] ns | 37.52 [36.9 .. 37.66] | 26.65 [26.56 .. 27.1] |  |
| `BM_Greeks/FastScalar/1048576` | 2.837e7 [2.83e7 .. 2.864e7] ns | 37 [36.65 .. 37.09] | 27.02 [26.96 .. 27.28] |  |
| `BM_Greeks/Simd/4096` | 58186 [57840 .. 58227] ns | 70.63 [70.42 .. 70.95] | 14.16 [14.1 .. 14.2] |  |
| `BM_Greeks/Simd/1048576` | 1.549e7 [1.544e7 .. 1.558e7] ns | 67.76 [67.56 .. 68] | 14.76 [14.71 .. 14.8] |  |
| `BM_ThreadedPrice/n:4096/threads:1/real_time` | 41298 [41182 .. 41374] ns | 99.18 [99 .. 99.46] | 10.08 [10.05 .. 10.1] | 1 [1 .. 1] |
| `BM_ThreadedPrice/n:4096/threads:2/real_time` | 46842 [46687 .. 48446] ns | 87.44 [84.55 .. 87.73] | 11.44 [11.4 .. 11.83] | 2 [2 .. 2] |
| `BM_ThreadedPrice/n:4096/threads:4/real_time` | 46953 [46617 .. 47353] ns | 87.24 [86.5 .. 87.86] | 11.46 [11.38 .. 11.56] | 4 [4 .. 4] |
| `BM_ThreadedPrice/n:4096/threads:8/real_time` | 50039 [49861 .. 50186] ns | 81.86 [81.62 .. 82.15] | 12.22 [12.17 .. 12.25] | 8 [8 .. 8] |
| `BM_ThreadedPrice/n:4096/threads:0/real_time` | 77216 [74113 .. 97112] ns | 53.05 [42.18 .. 55.27] | 18.85 [18.09 .. 23.71] | 18 [18 .. 18] |
| `BM_ThreadedPrice/n:1048576/threads:1/real_time` | 1.067e7 [1.065e7 .. 1.069e7] ns | 98.24 [98.13 .. 98.45] | 10.18 [10.16 .. 10.19] | 1 [1 .. 1] |
| `BM_ThreadedPrice/n:1048576/threads:2/real_time` | 5.42e6 [5.41e6 .. 5.487e6] ns | 193.5 [191.1 .. 193.8] | 5.169 [5.16 .. 5.232] | 2 [2 .. 2] |
| `BM_ThreadedPrice/n:1048576/threads:4/real_time` | 2.853e6 [2.814e6 .. 3.186e6] ns | 367.5 [329.1 .. 372.6] | 2.721 [2.684 .. 3.039] | 4 [4 .. 4] |
| `BM_ThreadedPrice/n:1048576/threads:8/real_time` | 1.637e6 [1.635e6 .. 1.76e6] ns | 640.5 [595.7 .. 641.5] | 1.561 [1.559 .. 1.679] | 8 [8 .. 8] |
| `BM_ThreadedPrice/n:1048576/threads:0/real_time` | 943136 [904875 .. 978333] ns | 1112 [1072 .. 1159] | 0.8994 [0.863 .. 0.933] | 18 [18 .. 18] |
| `BM_ThreadedGreeks/n:4096/threads:1/real_time` | 58009 [57835 .. 58230] ns | 70.61 [70.34 .. 70.82] | 14.16 [14.12 .. 14.22] | 1 [1 .. 1] |
| `BM_ThreadedGreeks/n:4096/threads:2/real_time` | 63910 [63198 .. 65210] ns | 64.09 [62.81 .. 64.81] | 15.6 [15.43 .. 15.92] | 2 [2 .. 2] |
| `BM_ThreadedGreeks/n:4096/threads:4/real_time` | 63752 [63371 .. 72186] ns | 64.25 [56.74 .. 64.64] | 15.56 [15.47 .. 17.62] | 4 [4 .. 4] |
| `BM_ThreadedGreeks/n:4096/threads:8/real_time` | 70272 [66238 .. 77895] ns | 58.29 [52.58 .. 61.84] | 17.16 [16.17 .. 19.02] | 8 [8 .. 8] |
| `BM_ThreadedGreeks/n:4096/threads:0/real_time` | 85910 [85713 .. 91963] ns | 47.68 [44.54 .. 47.79] | 20.97 [20.93 .. 22.45] | 18 [18 .. 18] |
| `BM_ThreadedGreeks/n:1048576/threads:1/real_time` | 1.552e7 [1.547e7 .. 1.577e7] ns | 67.56 [66.49 .. 67.79] | 14.8 [14.75 .. 15.04] | 1 [1 .. 1] |
| `BM_ThreadedGreeks/n:1048576/threads:2/real_time` | 8.061e6 [7.893e6 .. 8.1e6] ns | 130.1 [129.5 .. 132.8] | 7.687 [7.528 .. 7.725] | 2 [2 .. 2] |
| `BM_ThreadedGreeks/n:1048576/threads:4/real_time` | 4.382e6 [4.373e6 .. 4.448e6] ns | 239.3 [235.7 .. 239.8] | 4.179 [4.17 .. 4.242] | 4 [4 .. 4] |
| `BM_ThreadedGreeks/n:1048576/threads:8/real_time` | 2.726e6 [2.68e6 .. 2.752e6] ns | 384.7 [381 .. 391.2] | 2.6 [2.556 .. 2.625] | 8 [8 .. 8] |
| `BM_ThreadedGreeks/n:1048576/threads:0/real_time` | 1.645e6 [1.591e6 .. 1.671e6] ns | 637.3 [627.6 .. 658.9] | 1.569 [1.518 .. 1.593] | 18 [18 .. 18] |

### pricing_bench

| Benchmark | real time | ns/call |
|---|---|---|
| `BM_BsmPrice` | 139751 [139213 .. 140135] ns | 34.05 [33.95 .. 34.17] |
| `BM_BsmGreeks` | 165790 [165488 .. 166013] ns | 40.43 [40.36 .. 40.49] |
| `BM_NormCdf` | 30190 [30133 .. 30239] ns | 7.364 [7.349 .. 7.374] |
| `BM_FastNormCdf` | 15709 [15699 .. 15947] ns | 3.831 [3.828 .. 3.883] |
| `BM_StdExp` | 7395 [7325 .. 7526] ns | 1.802 [1.786 .. 1.828] |
| `BM_FastExp` | 9133 [9033 .. 9787] ns | 2.227 [2.2 .. 2.346] |
| `BM_StdLog` | 6762 [6733 .. 6918] ns | 1.649 [1.641 .. 1.684] |
| `BM_FastLog` | 6276 [6257 .. 6412] ns | 1.53 [1.526 .. 1.545] |

### iv_bench

| Benchmark | real time | mean_iter | ns/solve |
|---|---|---|---|
| `BM_ImpliedVolNearMoney` | 1.058e6 [1.055e6 .. 1.065e6] ns | 4.532 [4.532 .. 4.532] | 258.1 [257.2 .. 259.8] |
| `BM_ImpliedVolWings` | 1.564e6 [1.561e6 .. 1.568e6] ns | 6.016 [6.016 .. 6.016] | 381.6 [380.8 .. 382.3] |

### tree_mc_bench

| Benchmark | real time | nodes/s | paths/s | std_error |
|---|---|---|---|---|
| `BM_CrrEuropean/100` | 1042 [1040 .. 1045] ns | 4.851e9 [4.846e9 .. 4.862e9] |  |  |
| `BM_CrrEuropean/500` | 16196 [16183 .. 16214] ns | 7.742e9 [7.737e9 .. 7.747e9] |  |  |
| `BM_CrrEuropean/2000` | 241428 [240875 .. 242535] ns | 8.299e9 [8.265e9 .. 8.316e9] |  |  |
| `BM_CrrEuropean/10000` | 6.014e6 [6.004e6 .. 6.017e6] ns | 8.326e9 [8.321e9 .. 8.338e9] |  |  |
| `BM_CrrAmerican/100` | 3194 [3181 .. 3216] ns | 1.583e9 [1.572e9 .. 1.59e9] |  |  |
| `BM_CrrAmerican/500` | 81590 [81293 .. 81937] ns | 1.538e9 [1.53e9 .. 1.542e9] |  |  |
| `BM_CrrAmerican/2000` | 1.381e6 [1.375e6 .. 1.43e6] ns | 1.45e9 [1.426e9 .. 1.457e9] |  |  |
| `BM_CrrAmerican/10000` | 3.569e7 [3.567e7 .. 3.576e7] ns | 1.402e9 [1.401e9 .. 1.404e9] |  |  |
| `BM_MonteCarlo/variance_reduction:0` | 17.91 [17.84 .. 18.25] ms |  | 5.591e7 [5.514e7 .. 5.61e7] | 0.01283 [0.01283 .. 0.01283] |
| `BM_MonteCarlo/variance_reduction:1` | 10.95 [10.77 .. 11] ms |  | 9.147e7 [9.1e7 .. 9.295e7] | 0.003337 [0.003337 .. 0.003337] |

## Machine load

`uptime` just before the run started: `load averages: 7.59 9.28 7.67`; just after it ended:
`load averages: 3.78 7.54 7.42`. The largest other processes at the start (`ps -Ao pcpu,comm -r`)
were IntelliJ IDEA (about 300% CPU, so about 3 cores), Google Chrome (86%) and endpoint security
agents (about 115% together). For most single-threaded rows the slowest repetition is within 5% of
the median; the widest single-threaded ranges are `BM_Price/Reference/4096` (slowest 14% above
the median) and `BM_FastExp` (7%). The multithreaded rows compete with that load and spread
more (up to 25% for `BM_ThreadedPrice/n:4096/threads:0`), so they are the least reliable on
this page. (Spreads computed with jq from the committed JSON: (max - median) / median of
`real_time` per benchmark.)

## What the numbers say

Ratios below are computed from the medians in the tables above.

Batch engines, n = 4,096 (inputs and outputs fit in cache):

| Engine | price only, ns/option | price + 5 Greeks, ns/option |
|---|---|---|
| Reference (libm exp/log/erfc) | 37.22 | 45.94 |
| FastScalar (own exp/log/CDF, one option at a time) | 18.7 | 26.65 |
| Simd (NEON, 2 options per instruction) | 9.989 | 14.16 |

- Simd is 3.73x faster than Reference for prices (37.22 / 9.989) and 3.24x for price + Greeks
  (45.94 / 14.16). It is 1.87x faster than FastScalar for prices, close to the 2x that a
  2-wide NEON vector allows.
- Simd and FastScalar give bit-identical results (`docs/accuracy.md`), so the speed-up costs no
  accuracy between them. Against Reference the difference is at most 5% of the test tolerance.
- The FastScalar kernel is kept scalar on purpose (compiler auto-vectorization is switched off
  for that path), so the FastScalar vs Simd rows show what the explicit SIMD code adds.

Threads, n = 1,048,576 (about 100 MB of inputs and outputs, out of cache):

- One thread: 10.18 ns/option (prices), the same as the single-threaded Simd engine (10.14).
- All 18 hardware threads: 0.8994 ns/option, which is 1112 million prices per second, 11.3x the
  single-threaded Simd engine (10.14 / 0.8994). Price + Greeks: 1.569 ns/option (637.3 M/s),
  9.4x.
- 8 threads give 6.5x over 1 thread (10.18 / 1.561). The M5 Pro mixes core types (sysctl:
  6 cores in performance level 0, 12 in level 1), and other applications were running, so the
  scaling is not linear. Chunks are claimed dynamically, so faster cores take more of them.
- At n = 4,096 there is only one 4,096-option chunk, so extra threads cannot help: they only
  add wake-up cost (18 threads: 18.85 ns/option vs 10.08 with 1). The threaded pricer is meant
  for large chains.

Other pricers (single thread):

- `pricing_bench`: scalar `bsm_price` 34.05 ns and `bsm_greeks` 40.43 ns per call;
  `fast_norm_cdf` 3.831 ns vs `norm_cdf` (erfc) 7.364 ns; Apple's libm `exp` (1.802 ns) is
  faster than `fast_exp` (2.227 ns) one call at a time, while `fast_log` (1.53 ns) is slightly
  faster than `std::log` (1.649 ns). The batch speed-up comes from running the same code on
  several options at once, which libm cannot do.
- Implied vol (`iv_bench`): 258.1 ns per solve near the money (mean 4.532 Newton iterations)
  and 381.6 ns in the wings (mean 6.016 iterations).
- CRR tree (`tree_mc_bench`): European 1042 ns at 100 steps, 16196 ns at 500, 241428 ns at
  2,000 and 6.014e6 ns (6.0 ms) at 10,000 steps; American 3194 ns, 81590 ns, 1.381e6 ns and
  3.569e7 ns (35.7 ms) at the same step counts.
- Monte Carlo, 1,000,000 paths: 5.591e7 paths/s plain and 9.147e7 paths/s with antithetic
  pairs and the control variate (the variance-reduced run also has a 3.8x smaller standard
  error, 0.003337 vs 0.01283, from the same table).

## Benchmark inputs

- `batch_bench`: a seeded chain with S = 100, K uniform on [50, 200], T uniform on [0.02, 2],
  sigma uniform on [0.1, 0.8], r uniform on [0, 0.05], q uniform on [0, 0.03], calls and puts
  by coin flip; 64-byte aligned buffers; n = 4,096 and 1,048,576. `threads:0` means all
  hardware threads (the `pool_threads` column shows how many: 18).
- `pricing_bench`: 4,096 seeded inputs per iteration (the same option ranges for `bsm_*`;
  x uniform on [-6, 6] for the CDFs, [-20, 20] for exp, [1e-3, 1e3] for log).
- `iv_bench`: 4,096 seeded prices with standardised moneyness |z| in [0, 1] (near the money) or
  [3, 6] (wings), sigma in [0.1, 1], T in [0.05, 2].
- `tree_mc_bench`: one put, S = 100, K = 105, T = 1, r = 0.05, q = 0.01, sigma = 0.25.
