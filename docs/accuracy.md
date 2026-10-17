# Accuracy

Every number on this page is pasted from the output of the command written next to it, run on
the dev machine (Apple M5 Pro, Apple clang 21.0.0, `release` preset, `opt_info`: `simd backend:
neon (width 2)`).

## Implied volatility round trip

Command:

```bash
./build/release/tools/iv_accuracy
```

Output:

```
implied vol round trip: 56000 grid points, S = 100
well-conditioned (vega >= 1e-5 x D (F N(w d1) + K N(w d2)) and price / (D F) >= 1e-12)
  points:                    44872
  max |vol_hat - vol|:       1.534e-11  (Ok points)
  max |P(vol_hat) - P| / P:  1.263e-10  (Ok points)
  iterations (Ok points):    mean 6.22, max 12
  status counts: Ok=44872 InvalidInput=0 BelowIntrinsic=0 AboveUpperBound=0 AboveMaxVol=0 NotConverged=0
ill-conditioned (everything else)
  points:                    11128
  max |vol_hat - vol|:       2.332e-01  (Ok points)
  max |P(vol_hat) - P| / P:  1.887e-10  (Ok points)
  iterations (Ok points):    mean 6.40, max 49
  status counts: Ok=11091 InvalidInput=0 BelowIntrinsic=35 AboveUpperBound=2 AboveMaxVol=0 NotConverged=0
all points (both regions)
  points:                    56000
  max |vol_hat - vol|:       2.332e-01  (Ok points)
  max |P(vol_hat) - P| / P:  1.887e-10  (Ok points)
  iterations (Ok points):    mean 6.25, max 49
  status counts: Ok=55963 InvalidInput=0 BelowIntrinsic=35 AboveUpperBound=2 AboveMaxVol=0 NotConverged=0
design 5.8 definition, for comparison (vega / (D F) >= 1e-6 and price / (D F) >= 1e-12)
  points:                    42464
  max |vol_hat - vol|:       2.332e-01  (Ok points)
  max |P(vol_hat) - P| / P:  8.422e-11  (Ok points)
  iterations (Ok points):    mean 6.23, max 12
  status counts: Ok=42445 InvalidInput=0 BelowIntrinsic=18 AboveUpperBound=1 AboveMaxVol=0 NotConverged=0
```

The grid (`include/opt/pricing/detail/iv_grid.hpp`, shared with the test
`ImpliedVol.RoundTripGrid`): sigma at 40 log-spaced values in [0.01, 3]; T in {1 hour, 1 day,
1 week, 1 month, 3 months, 1 year, 5 years}; standardised moneyness z = ln(K/F) / (sigma sqrt T)
at 25 values in [-6, 6]; calls and puts; r in {0, 0.05}; q in {0, 0.03}; S = 100. Each price is
`bsm_price` at the true sigma; the solver gets that price and must recover sigma.

### What limits the accuracy (conditioning)

A price error dP becomes a vol error dP / vega. The solver cannot do better than the price it is
given, and a double-precision price is not exact: `bsm_price` computes
D (F N(omega d1) - K N(omega d2)), the difference of two terms, so its absolute rounding error is
a few eps times D (F N(omega d1) + K N(omega d2)). The output above calls that sum the price
scale. The vol error is then about eps x price scale / vega. Where vega is tiny next to the price
scale (deep out of the money, very short expiry, or deep in the money with K far above F), even an
exact solver cannot recover sigma to 1e-10.

### Why the well-conditioned region differs from design 5.8

The design defined "well-conditioned" as vega / (D F) >= 1e-6. That normalises by the forward,
which is the right scale for calls and out-of-the-money puts but wrong for in-the-money puts with
K much larger than F: their price is about D K, so its rounding error is about eps D K. On this
grid K / F reaches about 1e17 (z = 6, sigma = 3, T = 5), so those points passed the design's
vega / F test while carrying no information about sigma. The last block of the output shows the
result under the design definition: 42,464 points, 19 of them not even solvable (18
BelowIntrinsic, 1 AboveUpperBound, where the price rounded onto a bound), and a maximum vol error
of 0.233.

The test uses vega >= 1e-5 x price scale instead. The solver stops once |f| is below
4 eps x price scale (the rounding noise of the Black formula), so a vol error of at most
4 eps / (vega / price scale) = 8.9e-11 is expected at the 1e-5 boundary, and the measured maximum
is 1.534e-11 over 44,872 points, all Ok. The tolerance (1e-10) is the design's; only the region
definition changed.

### Ill-conditioned points

Everywhere else the test requires status Ok, or BelowIntrinsic / AboveUpperBound only where the
price itself rounded onto a no-arbitrage bound (35 and 2 points above). When Ok, the vol must
reprice the input to 1e-10 relative, with one addition: a rounding floor of 16 eps x price scale.
For a tiny out-of-the-money price (for example z = 5.5, T = 1 hour) the price scale is far larger
than the price, so no vol reproduces it to 1e-10 relative. The test prints how many points
needed the floor:

```bash
./build/release/tests/opt_pricing_tests --gtest_filter='ImpliedVol.RoundTripGrid'
```

```
[measure] IV round trip: 56000 points, 44872 well-conditioned (max |vol_hat - vol| = 1.53e-11, tolerance 1e-10); ill-conditioned: 11091 Ok (max reprice rel error 1.89e-10; 1 above 1e-10 relative, max 7.78 eps x price_scale, floor 16), 35 BelowIntrinsic, 2 AboveUpperBound; iterations (Ok) mean 6.25 max 49
```

One point needed it, at 7.78 eps x price scale. The 0.233 maximum vol error in the
ill-conditioned block comes from the in-the-money puts with K / F up to 1e17 described above:
they reprice correctly (the vol is valid for the given price) but the price does not determine
sigma.

### Solver iterations

The solver is the safeguarded Newton method of design 5.3, with three measured additions
(`include/opt/pricing/implied_vol.hpp` explains each):

- The Newton step is taken on ln B - ln c instead of B - c (same root, same bracket). In the
  wings B is strongly convex in sigma, and plain Newton overshoots and then creeps back.
- A wing initial guess from the leading Gaussian term of the price, used where Corrado-Miller's
  square root goes negative.
- A stop when |f| is below the rounding noise of the Black formula. Without it, Newton steps
  smaller than half an ulp of sigma were rejected by the bracket test and the solver bisected
  for about 40 iterations at points that had already converged.

With them the mean is 6.25 iterations over all 55,963 Ok points (maximum 49, in the
ill-conditioned region where vega underflows and the solver bisects). These counts are the
baseline a Jaeckel "Let's Be Rational" solver (P. Jaeckel, 2015, which reports machine precision
after two iterations) would have to beat; it is listed as a possible extension, not implemented.

## Normal distribution

Command:

```bash
./build/release/tests/opt_core_tests --gtest_filter='Normal.*'
```

```
[measure] AS241 round trip: max error / bound = 0.671, max (err/p - 1e-14)/(x^2 eps) = 4.01 (bound coefficient 6)
[measure] norm_cdf symmetry: max |N(x)+N(-x)-1| = 0
```

Design 5.8 item 10 asked for |N(N^{-1}(p)) - p| <= (1e-14 + 4 x^2 2^-52) p on
(1e-300, 1 - 1e-16), with x = N^{-1}(p). In the lower tail N is ill-conditioned: a relative error
e in x becomes a relative error of about x^2 e in N(x). Two errors add up there: AS241's own
relative error in x (it is evaluated in double precision, so it carries a few ulp), amplified by
x^2, and norm_cdf's error (rounding x / sqrt 2 before erfc costs about x^2 ulp of the result).
The measured coefficient of x^2 eps is 4.01, just above the design's 4. The test uses 6 and
prints the measured value (4.01 above). This is a change of threshold, made for the reason
above; AS241 and norm_cdf were checked first and are implemented as published.

## Fast math error vs libm

`include/opt/core/fast_math.hpp` has the project's own exp, log and normal CDF/PDF. The
FastScalar and Simd batch engines use them. They use only correctly rounded IEEE operations
(+ - * / sqrt fma and bit casts), so every backend (scalar, NEON, AVX2, Emulated) gives the same
bits. The reference is libm (`std::exp`, `std::log`) or the reference normal functions in
`normal.hpp`. Those have about 1 ulp of error themselves, so the numbers below are errors
"vs libm", not errors against the exact value.

Command (10,000,000 seeded points per row plus edge points: range ends, both sides of every
exp range-reduction boundary, the log sqrt(2) switch, and the CDF's 7.07 switch):

```bash
./build/release/tools/fast_math_error
```

Output:

```
fast_math_error: 10000000 random points per row (seed 1) plus edge points; simd backend neon; errors vs libm / reference normal
fast_exp       uniform [-708, 709]        vs std::exp  points  10004096  max abs 9.979e+291 at x = 708.78542365627732       max rel 2.220e-16 at x = -145.5607060635341
fast_log       log-uniform [1e-300, 1e300] vs std::log  points  10000011  max abs 2.220e-16 at x = 0.35299159119978979      max rel 3.814e-16 at x = 1.1566774495242427
fast_norm_cdf  uniform [-38, 9]           vs norm_cdf  points  10000011  max abs 2.220e-16 at x = 0.66846289236038103      max rel 1.000e+00 at x = -38
fast_norm_cdf  uniform [-37, -7.0710678]  vs norm_cdf  points  10000002  max abs 2.775e-21 at x = -7.0710681558250492      max rel 8.912e-09 at x = -7.784734481608357
fast_norm_cdf  uniform [-7.0710678, 0]    vs norm_cdf  points  10000002  max abs 2.220e-16 at x = -0.18176062710308294     max rel 2.890e-09 at x = -7.0710678118654693
fast_norm_cdf  uniform [0, 9]             vs norm_cdf  points  10000003  max abs 2.220e-16 at x = 0.20161786669984838      max rel 4.440e-16 at x = 0.00016549183849801086
fast_norm_cdf  uniform [-38, -37]         vs norm_cdf  points  10000003  max abs 5.726e-300 at x = -37.000000000000007      max rel 1.000e+00 at x = -38
fast_norm_pdf  uniform [-38, 38]          vs norm_pdf  points  10000003  max abs 5.551e-17 at x = 0.94988096460878779      max rel 6.038e-12 at x = 37.886361718107892
fast_norm_inv  (1e-300, 1 - 1e-16)        vs AS241     points  10000005  max abs 6.217e-15 at x = 0.9999999999809337       max rel 9.404e-16 at x = 0.9999999999809337
```

How to read it:

- `fast_exp`: worst relative error 2.22e-16, which is 1 eps (2^-52). The design target was 2
  ulp. The absolute error is large only because e^709 is about 8e307.
- `fast_log`: worst relative error 3.81e-16 (1.7 eps).
- `fast_norm_cdf`: the absolute error is at most 2.22e-16 everywhere. The relative error is
  small at and above the centre (4.4e-16 on [0, 9]) but grows in the lower tail, up to 8.9e-9
  near x = -7.8. The next section explains why. Below -37 the function returns 0 by design
  (West's cut-off) while N(-37) is about 6e-300, so the relative error there is 1 and the
  absolute error is 5.7e-300.
- `fast_norm_pdf`: 6.0e-12 relative at x = 37.9, because the density there (about 1e-312) is
  subnormal and has few significant bits. Both sides compute the same argument
  `(-0.5 * x) * x`, so this is the exp rounding on a subnormal result, not an argument error.
- `fast_norm_inv` (AS241 with `fast_log`): 9.4e-16 relative.

### Fast normal CDF: why the bound is absolute

The design (section 4.1) expected `fast_norm_cdf` to be within 1e-14 + 2 x^2 eps relative on
[-37, 0]. The implementation is Hart's algorithm 5666 exactly as G. West published it ("Better
approximations to cumulative normal functions", Wilmott, 2005, figure 2; the coefficients were
transcribed from the paper and checked against it). The measurement above shows that this
algorithm is accurate in absolute terms (2.2e-16) but not in relative terms in the lower tail:
2.9e-9 at the 7.07 switch point from the rational approximation, and 8.9e-9 near -7.8 from the
short continued fraction. The published coefficients have 15 significant digits, so this is a
property of the approximation, not a transcription bug.

Following the design's 6.6 rule (record the measured value and the reason before changing a
threshold), `tests/core/fast_math_test.cpp` asserts:

| Row | Design 4.1 bound | Bound used | Measured by the test (1,000,000 points) |
|---|---|---|---|
| [-37, 0] absolute | (none) | 4.5e-16 | 1.943e-16 |
| [-37, 0] relative | 1e-14 + 2 x^2 eps | 2e-8 | 8.912e-9 |
| [-2, 0] relative | 1e-14 + 2 x^2 eps (on [-37, 0]) | 1e-14 + 2 x^2 eps | 1.755e-15 |
| [0, 8.3] relative | 1e-14 | 1e-14 | 4.438e-16 |
| [-38, -37] absolute | 1e-299 | 1e-299 | 5.726e-300 |
| [8.3, 38] absolute vs 1 | 1e-15 | 1e-15 | 0 |

The other rows keep the design's bounds: `fast_exp` relative 4 eps (measured 2.217e-16,
err/bound 0.250), `fast_log` relative 4 eps, or absolute 4 eps where |ln x| < 1 (err/bound
0.241), `fast_norm_inv` 1e-14 |ref| + 1e-15 (err/bound 0.083). Every backend (Emulated<4>,
Emulated<8>, NEON) gives results bit-identical to the double version for `fast_exp`,
`fast_log`, `fast_norm_cdf` and `fast_norm_pdf` (test `FastMathBackend.BitIdenticalToDouble`).

Command and the lines the test prints:

```bash
./build/release/tests/opt_core_tests --gtest_filter='FastMath*'
```

```
[fast_math] fast_exp [-708,709] rel vs std::exp      max abs 9.979e+291 (x=708.98684208681948)  max rel 2.217e-16 (x=618.28873678968421)  max err/bound 0.250
[fast_math] fast_log [1e-300,1e300] vs std::log      max abs 2.220e-16 (x=0.35426814770297693)  max rel 3.650e-16 (x=1.3554648678025221)  max err/bound 0.241
[fast_math] fast_norm_cdf [-37,0] abs vs norm_cdf    max abs 1.943e-16 (x=-0.67939151449192536)  max rel 8.912e-09 (x=-7.7850441652718594)  max err/bound 0.432
[fast_math] fast_norm_cdf [-37,0] rel vs norm_cdf    max abs 1.665e-16 (x=-0.16744408802276212)  max rel 8.912e-09 (x=-7.7848323831889772)  max err/bound 0.446
[fast_math] fast_norm_cdf [-2,0] rel vs norm_cdf     max abs 2.220e-16 (x=-0.67046733062232478)  max rel 1.755e-15 (x=-1.7559985730614593)  max err/bound 0.154
[fast_math] fast_norm_cdf [0,8.3] rel vs norm_cdf    max abs 2.220e-16 (x=0.21796760778107141)  max rel 4.438e-16 (x=0.00076008152534593485)  max err/bound 0.044
[fast_math] fast_norm_cdf [-38,-37] abs vs norm_cdf  max abs 5.726e-300 (x=-37.000000000000007)  max rel 1.000e+00 (x=-37.000000000000007)  max err/bound 0.573
[fast_math] fast_norm_cdf [8.3,38] abs vs 1          max abs 0.000e+00 (x=0)  max rel 0.000e+00 (x=0)  max err/bound 0.000
[fast_math] fast_norm_inv (1e-300,1-1e-16) vs AS241  max abs 3.553e-15 (x=0.99998664533225867)  max rel 8.459e-16 (x=0.99998664533225867)  max err/bound 0.083
```

What this means for prices: a price is a sum of terms like D K N(d2). An absolute error of
2.2e-16 in N gives an absolute price error of about 2.2e-16 x D K, which is what the batch
tolerances below allow. A deep out-of-the-money price (N about 1e-14) can therefore carry a
relative error of about 1e-8. That is far below any bid-ask spread, but the fast engines should
not be used to back out implied vols from prices as small as 1e-14 x spot.

## Batch engine tolerances

`tests/batch/engine_test.cpp` prices 200,003 seeded random options (not a multiple of 2, 4 or 8)
plus 304 edge options with every engine and compares each output with the Reference engine (a
loop over `bsm_price` / `bsm_greeks`): |fast - ref| <= atol + 1e-12 |ref|.

The random options cover the batch domain: S log-uniform on [1e-8, 1e12], K = S e^u with u
uniform on [-3, 3], T log-uniform on [1e-8, 100], sigma log-uniform on [1e-6, 10], r and q
uniform on [-0.05, 0.15] for 80% of the options and on [-1, 1] for the rest. The edge options
have T = 1e-8 and 1e-6, sigma = 1e-6 and 10, z = ln(F/K)/(sigma sqrt T) = +-8, +-37, +-38.5
and +-40, F == K exactly, S and K at the domain ends, and irregular call/put patterns. Each
engine also runs on sizes 0, 1, W-1, W, W+1 and 2W+3 (W = 2, 4, 8) at offsets 0, 1, 3 and 1001
(unaligned sub-spans), and must not write outside the requested range.

### atol: discounted scale instead of S and K

Design 6.6 gave atol in units of S and K, for example price atol = 1e-14 (S + K). The first run
failed by factors up to 4.9e5 on options with large |r T| or |q T|. One example: S = 394,
K = 960, T = 38.6, r = -0.965, q = -0.923, where the discount factor e^{-rT} is 1.6e16. There a
price is of the size of S e^{-qT} or K e^{-rT}, not S or K, and so is its rounding error. The
test now uses the discounted spot S e^{-qT} and the discounted strike K e^{-rT} in every atol
(they equal S and K when r = q = 0), and theta also gets 1e-14 of its carry terms
|r| K e^{-rT} + |q| S e^{-qT}:

| Output | Design 6.6 atol | atol used (S' = S e^{-qT}, K' = K e^{-rT}, Dq = e^{-qT}) |
|---|---|---|
| price | 1e-14 (S + K) | 1e-14 (S' + K') |
| delta | 1e-14 | 1e-14 Dq |
| gamma | 1e-12 / (S sigma sqrt T) | 1e-12 Dq / (S sigma sqrt T) |
| vega | 1e-14 (S + K) sqrt T | 1e-14 (S' + K') sqrt T |
| theta | 1e-12 S sigma / sqrt T | 1e-12 S' sigma / sqrt T + 1e-14 (\|r\| K' + \|q\| S') |
| rho | 1e-14 K max(T, 1) | 1e-14 K' max(T, 1) |

rtol stays 1e-12 for every output. With this scaling the worst error is under 5% of the
tolerance for every output.

Command and the lines the test prints:

```bash
./build/release/tests/opt_batch_tests --gtest_filter='BatchEngine.*:EmulatedKernel*'
```

```
[batch] FastScalar greeks            price max err/tol 1.267e-02 (option 34014)
[batch] FastScalar greeks            delta max err/tol 4.384e-02 (option 1325)
[batch] FastScalar greeks            gamma max err/tol 1.832e-03 (option 1325)
[batch] FastScalar greeks            vega  max err/tol 4.837e-02 (option 1325)
[batch] FastScalar greeks            theta max err/tol 2.121e-02 (option 1325)
[batch] FastScalar greeks            rho   max err/tol 4.421e-02 (option 1325)
[batch] FastScalar price-only        price max err/tol 1.267e-02 (option 34014)
[batch] Simd greeks                  price max err/tol 1.267e-02 (option 34014)
[batch] Simd greeks                  delta max err/tol 4.384e-02 (option 1325)
[batch] Simd greeks                  gamma max err/tol 1.832e-03 (option 1325)
[batch] Simd greeks                  vega  max err/tol 4.837e-02 (option 1325)
[batch] Simd greeks                  theta max err/tol 2.121e-02 (option 1325)
[batch] Simd greeks                  rho   max err/tol 4.421e-02 (option 1325)
[batch] Simd price-only              price max err/tol 1.267e-02 (option 34014)
[batch] Simd (neon, width 2) vs FastScalar: 0 of 1402149 outputs not bit-identical
[batch] Emulated<4>                  price max err/tol 1.267e-02 (option 34014)
[batch] Emulated<4>                  delta max err/tol 4.384e-02 (option 1325)
[batch] Emulated<4>                  gamma max err/tol 1.832e-03 (option 1325)
[batch] Emulated<4>                  vega  max err/tol 4.837e-02 (option 1325)
[batch] Emulated<4>                  theta max err/tol 2.121e-02 (option 1325)
[batch] Emulated<4>                  rho   max err/tol 4.421e-02 (option 1325)
[batch] Emulated<8>                  price max err/tol 1.267e-02 (option 34014)
[batch] Emulated<8>                  delta max err/tol 4.384e-02 (option 1325)
[batch] Emulated<8>                  gamma max err/tol 1.832e-03 (option 1325)
[batch] Emulated<8>                  vega  max err/tol 4.837e-02 (option 1325)
[batch] Emulated<8>                  theta max err/tol 2.121e-02 (option 1325)
[batch] Emulated<8>                  rho   max err/tol 4.421e-02 (option 1325)
```

The SIMD engine's outputs are bit-identical to FastScalar: 0 of 1,402,149 outputs differ (the
6 price+Greeks outputs plus the price-only output for 200,307 options). The same holds for the
AVX2 backend: an x86_64 build with `-mavx2 -mfma`, run under Rosetta 2 on the same Mac, printed
`[batch] Simd (avx2+fma, width 4) vs FastScalar: 0 of 1402149 outputs not bit-identical` with
the same worst ratios (commands in `docs/part2-verification.md`). The threaded engine equals
the SIMD engine bit for bit for 1, 2, 3 and 18 threads (`ThreadedPricer.EqualsSimdBitwise`).

## SVI fit (Part 3)

### Synthetic smiles

From `./build/release/tests/opt_vol_tests --gtest_filter='SviFit.*'`. 41 points over k in
[-1, 1], tau = 1:

| True (a, b, rho, m, s) | Largest parameter error | RMSE (vol pts) | Iterations |
|---|---|---|---|
| (0.04, 0.10, -0.30, 0.00, 0.10) | 2.30e-12 (s) | 6.35e-12 | 292 |
| (0.02, 0.20, -0.60, 0.10, 0.20) | 4.11e-15 (rho) | 4.66e-14 | 267 |
| (0.01, 0.05, 0.20, -0.10, 0.30) | 4.49e-12 (s) | 5.99e-12 | 295 |
| (0.10, 0.30, -0.90, 0.20, 0.05) | 1.01e-12 (s) | 5.86e-12 | 312 |

With 0.1 vol point Gaussian noise added to each vol (seeded), the fitted RMSE is 0.0985,
0.1018, 0.1046 and 0.1012 vol points: the fit absorbs no more than the noise. With one
+10 vol point outlier in 41 points, the error on the 40 clean points is 0.4450 vol points for
plain least squares and 0.0617 for the Huber fit. Test thresholds: parameter error <= 1e-6,
RMSE <= 1e-8 noise-free, noisy RMSE in [0.05, 0.2], Huber error < half the plain error.

### Committed Deribit snapshot

`./build/release/tools/vol_surface tests/data/deribit/BTC-20261017T123254Z --csv
/tmp/oe-surface.csv` (full output in [part3-verification.md](part3-verification.md)). Fit
points are OTM quotes with both sides, bid > 0 and a valid mid IV; 18OCT26 (0.81 days) is
below the 1-day minimum and is not fitted. "Outside bid-ask" counts fit points whose fitted
vol is below the bid IV or above the ask IV (from the CSV).

| Expiry | Days | Points | RMSE (vol pts) | Max error (vol pts) | Outside bid-ask | Butterfly min g |
|---|---|---|---|---|---|---|
| 19OCT26 | 1.81 | 17 | 0.7844 | 1.8791 | 5 | 0.08682 |
| 20OCT26 | 2.81 | 23 | 0.5064 | 1.1907 | 1 | 0.1492 |
| 21OCT26 | 3.81 | 24 | 0.7376 | 2.8417 | 1 | 0.1364 |
| 23OCT26 | 5.81 | 19 | 1.0629 | 3.5133 | 5 | 0.2038 |
| 30OCT26 | 12.81 | 45 | 0.9657 | 5.2648 | 13 | 0.1932 |
| 6NOV26 | 19.81 | 18 | 0.2156 | 0.5944 | 1 | 0.183 |
| 27NOV26 | 40.81 | 48 | 0.5821 | 2.2295 | 19 | 0.2029 |

All 7 fits have status Ok; mean RMSE 0.6935, max 1.0629 vol points. No butterfly, calendar or
Lee violations (all 6 adjacent calendar pairs have min (w2 - w1) > 0; the largest wing slope
b (1 - rho) is 0.110). 45 of the 194 fit points (23%) have a fitted vol outside their bid-ask
range. The largest errors are far out-of-the-money calls (for example 30OCT26 K = 102,000:
mid 47.56%, fit 42.30%), where the book is wide.

### Known limitation: the a >= 0 bound

Every fitted slice ends with a on its lower bound 0 (|a| < 1e-17). The Zeliade domain
requires a >= 0, but raw SVI only needs the minimum total variance a + b s sqrt(1 - rho^2) to
be non-negative, and BTC smiles want a < 0. The bound costs fit quality. A scratch experiment
(not in the code: the a >= 0 row replaced by a lower bound of -max w) on the full 12-expiry
raw chain lowered the RMSE on every expiry, for example 19OCT26 0.7844 -> 0.3715,
23OCT26 1.0629 -> 0.4626 and 25DEC26 3.2690 -> 0.8274, and none of those fits had a butterfly
violation or negative minimum variance. Relaxing the domain is a design change and is not
made here.

The raw full chain (12 expiries, not committed, so not reproducible offline) shows what the
bound does at longer expiries: 25DEC26 fits with RMSE 3.2690 vol points (puts about 3.5 vol
points too low and calls about 4 to 5 too high near the money), and the 25JUN27 -> 24SEP27
pair has a calendar violation far in the left wing (min (w2 - w1) = -0.01631 at k = -1.535,
39 of 401 grid points). The committed 8-expiry snapshot has neither.
