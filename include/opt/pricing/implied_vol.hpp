#pragma once
// Implied volatility from a European option price.
//
// Method (all in undiscounted terms, c = price / discount):
//   1. Validate: T > 0, F, K, D > 0, price finite and >= 0, else InvalidInput.
//   2. No-arbitrage bounds: call max(F-K, 0) < c < F, put max(K-F, 0) < c < K.
//      c <= intrinsic -> BelowIntrinsic (includes price 0); c >= upper bound
//      -> AboveUpperBound.
//   3. Solve on the out-of-the-money option: an in-the-money call becomes the
//      put with the same strike through forward parity p = c - (F - K) (and
//      vice versa). The subtraction keeps the absolute precision the ITM
//      price had, and the OTM price has the better-conditioned vega.
//   4. Bracket [0, max_vol]: f(sigma) = B(sigma) - c increases in sigma,
//      f(0) < 0 by step 2; f(max_vol) <= 0 -> AboveMaxVol.
//   5. Initial guess: Corrado-Miller (1996) quadratic approximation; if its
//      square-root argument is negative or the result leaves the bracket, a
//      wing guess from the leading Gaussian term c ~ sqrt(F K)
//      exp(-x^2 / (2 sigma^2 T)), x = ln(K/F); then Brenner-Subrahmanyam
//      (1988) sigma0 = sqrt(2 pi / T) c / F. Clamped into the bracket.
//   6. Safeguarded Newton (the "rtsafe" idea of Press et al., Numerical
//      Recipes ch. 9; own code): Newton steps with the analytic vega, the
//      bracket updated from the sign of f after every evaluation, and a
//      bisection step instead whenever the Newton step leaves the bracket,
//      vega < 1e-300, or |g| has not halved since two iterations ago. The
//      Newton step is taken on g = ln B - ln c (same root and sign as f):
//      dsigma = -g B / vega, equal to the plain step near the root, but far
//      better in the wings where B is strongly convex in sigma (plain Newton
//      took about 25 iterations at |z| = 6 on the test grid, the log form
//      about 6). Stops when |step| <= vol_tol * max(1, sigma), the bracket
//      is that narrow, or f == 0 to rounding: |f| <= 4 eps (F N(omega d1) +
//      K N(omega d2)), the rounding noise of the Black formula, below which
//      Newton steps only chase noise.
//
// Differences from design 5.3, all measured on the round-trip grid (see
// docs/accuracy.md): the wing guess, the log-space Newton step, and the
// rounding-level "f == 0" test are additions; the bracket, the bisection
// rule and the statuses are as designed.
//
// Why this method: the maintained bracket guarantees convergence for every
// price strictly inside the bounds (bisection alone would get there), and
// Newton makes it fast; the code is short enough to explain line by line. A
// separate Brent fallback adds nothing here: Brent's advantage is not needing
// a derivative, and the exact vega is available; the bracket already gives
// the guarantee Brent would. Jaeckel's "Let's Be Rational" reaches machine
// precision in two iterations but needs about a thousand lines of rational
// approximations; it is a possible extension, and the measured Newton
// iteration counts (docs/accuracy.md) are the baseline it would have to beat.
//
// Conditioning: a price error dP becomes a vol error dP / vega. Where vega is
// tiny (deep out of the money, very short expiry) no solver can recover sigma
// to 1e-10 from a double-precision price; see docs/accuracy.md.
#include "opt/pricing/types.hpp"

#include <cstdint>

namespace opt {

enum class IvStatus : std::uint8_t {
  Ok,
  InvalidInput,
  BelowIntrinsic,
  AboveUpperBound,
  AboveMaxVol,
  NotConverged,
};

struct IvOptions {
  double max_vol = 10.0;
  double vol_tol = 1e-14;
  int max_iter = 100;
};

// vol is NaN for every status except Ok and NotConverged; NotConverged
// carries the iterate with the smallest |f| seen (still not a valid answer).
// iterations counts evaluations of the Black formula inside the Newton loop.
struct IvResult {
  double vol;
  IvStatus status;
  int iterations;
};

// Black-76: price of a European option on a forward, discount = e^{-rT}.
[[nodiscard]] IvResult implied_vol_black(OptionType type, double price, double forward,
                                         double strike, double time, double discount,
                                         const IvOptions& options = {}) noexcept;

// Black-Scholes-Merton: spec.vol is ignored. Uses the same forward and
// discount as bsm_price, so implied_vol_bsm(bsm_price(s), s) inverts exactly
// the function that produced the price.
[[nodiscard]] IvResult implied_vol_bsm(double price, const OptionSpec& spec_without_vol,
                                       const IvOptions& options = {}) noexcept;

}  // namespace opt
