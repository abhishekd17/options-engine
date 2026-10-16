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

(Part 2.)

## Batch engine tolerances

(Part 2.)
