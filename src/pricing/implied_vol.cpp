#include "opt/pricing/implied_vol.hpp"

#include "opt/core/normal.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace opt {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

IvResult fail(IvStatus status) noexcept {
  return IvResult{kNaN, status, 0};
}

bool positive_finite(double x) noexcept {
  return std::isfinite(x) && x > 0.0;
}

struct VegaAndNoise {
  double vega;   // undiscounted vega F phi(d1) sqrt(T) (same for calls and puts)
  double noise;  // size of the rounding error in the computed Black price
};

// black_undiscounted computes omega (F N(omega d1) - K N(omega d2)): each of
// the two terms carries a few ulp of rounding error, so the difference has an
// absolute error of a few eps (F N(omega d1) + K N(omega d2)). Once |f| is
// below that, f is rounding noise: its sign is random and a Newton step
// computed from it moves sigma by noise / vega, which may still exceed
// vol_tol where vega is small. Treating such an f as zero is the
// floating-point form of the "f == 0" stopping rule.
VegaAndNoise vega_and_noise(OptionType type, double forward, double strike, double time,
                            double vol) noexcept {
  constexpr double kEps = 0x1.0p-52;
  const double sqrt_t = std::sqrt(time);
  const double sd = vol * sqrt_t;
  if (sd == 0.0) {
    return {0.0, 0.0};
  }
  const double w = omega(type);
  const double d1 = (std::log(forward / strike) + 0.5 * sd * sd) / sd;
  const double d2 = d1 - sd;
  const double terms = forward * norm_cdf(w * d1) + strike * norm_cdf(w * d2);
  return {forward * norm_pdf(d1) * sqrt_t, 4.0 * kEps * terms};
}

// Corrado and Miller (1996), "A note on a simple, accurate formula to compute
// implied standard deviations", J. Banking & Finance 20: written on the
// forward with the undiscounted call price. Returns NaN if the square-root
// argument is negative.
double corrado_miller(double call, double forward, double strike, double time) noexcept {
  const double half_gap = 0.5 * (forward - strike);
  const double x = call - half_gap;
  const double arg = x * x - (forward - strike) * (forward - strike) / std::numbers::pi;
  if (arg < 0.0) {
    return kNaN;
  }
  return std::sqrt(2.0 * std::numbers::pi) / (forward + strike) * (x + std::sqrt(arg)) /
         std::sqrt(time);
}

// Far out of the money the price falls like a Gaussian in x / sd, with
// x = ln(K/F): to leading order c ~ sqrt(F K) exp(-x^2 / (2 sd^2)), so
// sd ~ |x| / sqrt(2 L) with L = -ln(c / sqrt(F K)) > 0. Used where
// Corrado-Miller fails (its square root goes negative in the wings), and
// where the Brenner-Subrahmanyam ATM formula is far too small. Returns NaN
// at the money (x == 0).
double wing_guess(double otm_price, double forward, double strike, double time) noexcept {
  const double x = std::abs(std::log(strike / forward));
  const double big_l = -std::log(otm_price / std::sqrt(forward * strike));
  if (!(x > 0.0) || !(big_l > 0.0)) {
    return kNaN;
  }
  return x / std::sqrt(2.0 * big_l) / std::sqrt(time);
}

}  // namespace

IvResult implied_vol_black(OptionType type, double price, double forward, double strike,
                           double time, double discount, const IvOptions& options) noexcept {
  // 1. Validate.
  if (!positive_finite(time) || !positive_finite(forward) || !positive_finite(strike) ||
      !positive_finite(discount) || !std::isfinite(price) || price < 0.0 ||
      (type != OptionType::Call && type != OptionType::Put) ||
      !positive_finite(options.max_vol) || !(options.vol_tol >= 0.0) || options.max_iter < 1) {
    return fail(IvStatus::InvalidInput);
  }
  const double c = price / discount;

  // 2. No-arbitrage bounds.
  const bool is_call = type == OptionType::Call;
  const double intrinsic = is_call ? std::max(forward - strike, 0.0)
                                   : std::max(strike - forward, 0.0);
  const double upper = is_call ? forward : strike;
  if (c <= intrinsic) {
    return fail(IvStatus::BelowIntrinsic);
  }
  if (c >= upper) {
    return fail(IvStatus::AboveUpperBound);
  }

  // 3. Solve on the out-of-the-money option.
  OptionType otm = type;
  double target = c;
  if (is_call && forward > strike) {
    otm = OptionType::Put;
    target = c - (forward - strike);
  } else if (!is_call && strike > forward) {
    otm = OptionType::Call;
    target = c - (strike - forward);
  }
  const double otm_upper = otm == OptionType::Call ? forward : strike;
  if (!(target > 0.0)) {
    return fail(IvStatus::BelowIntrinsic);  // c - intrinsic rounded to <= 0
  }
  if (target >= otm_upper) {
    return fail(IvStatus::AboveUpperBound);
  }
  auto black = [&](double vol) noexcept {
    return black_undiscounted(otm, forward, strike, time, vol);
  };

  // 4. Bracket.
  double lo = 0.0;
  double hi = options.max_vol;
  if (black(hi) - target <= 0.0) {
    return fail(IvStatus::AboveMaxVol);
  }

  // 5. Initial guess.
  const double call_equiv = otm == OptionType::Call ? target : target + (forward - strike);
  double vol = corrado_miller(call_equiv, forward, strike, time);
  if (!(vol > lo && vol < hi)) {
    vol = wing_guess(target, forward, strike, time);
  }
  if (!(vol > lo && vol < hi)) {
    vol = std::sqrt(2.0 * std::numbers::pi / time) * target / forward;
  }
  vol = std::clamp(vol, 1e-3 * std::min(1.0, hi), 0.5 * hi);

  // 6. Safeguarded Newton. The step is Newton's step for
  // g(sigma) = ln B(sigma) - ln c, which has the same root and sign as
  // f = B - c (so the bracket logic is unchanged): dsigma = -g B / vega.
  // Near the root g ~ f / c and the step equals the plain Newton step on f.
  // In the wings B behaves like exp(-x^2 / (2 sigma^2 T)), which is strongly
  // convex in sigma: plain Newton overshoots far to the right from below and
  // then creeps back, about 25 iterations at |z| = 6 on the test grid; the
  // log is close to linear there and converges in a few steps.
  const double log_target = std::log(target);
  double abs_g_prev = std::numeric_limits<double>::infinity();   // |g| one iteration ago
  double abs_g_prev2 = std::numeric_limits<double>::infinity();  // two iterations ago
  double best_vol = vol;
  double best_abs_f = std::numeric_limits<double>::infinity();
  for (int iter = 1; iter <= options.max_iter; ++iter) {
    const double b = black(vol);
    const double fv = b - target;
    if (fv == 0.0) {
      return IvResult{vol, IvStatus::Ok, iter};
    }
    if (std::abs(fv) < best_abs_f) {
      best_abs_f = std::abs(fv);
      best_vol = vol;
    }
    if (fv < 0.0) {
      lo = vol;
    } else {
      hi = vol;
    }
    const double tol = options.vol_tol * std::max(1.0, vol);
    const auto [vega, noise] = vega_and_noise(otm, forward, strike, time, vol);
    if (std::abs(fv) <= noise) {
      return IvResult{vol, IvStatus::Ok, iter};  // f is zero to rounding
    }
    // b == 0 (underflow) has no log: bisect.
    const double gv = b > 0.0 ? std::log(b) - log_target : kNaN;
    double next = kNaN;
    if (vega >= 1e-300 && b > 0.0) {
      const double newton = vol - gv * (b / vega);
      const double newton_step = std::abs(newton - vol);
      if (newton_step <= tol) {
        // Converged. Checked before the bracket test: a step below half an
        // ulp of vol rounds to newton == vol == lo (or hi), which the strict
        // bracket test would reject, sending the solver into dozens of
        // bisection steps across a wide bracket for nothing.
        return IvResult{newton, IvStatus::Ok, iter};
      }
      const bool inside = newton > lo && newton < hi;
      const bool halving = std::abs(gv) <= 0.5 * abs_g_prev2;
      if (inside && halving) {
        next = newton;
      }
    }
    if (std::isnan(next)) {
      next = 0.5 * (lo + hi);
    }
    const double step = next - vol;
    abs_g_prev2 = abs_g_prev;
    abs_g_prev = std::isnan(gv) ? std::numeric_limits<double>::infinity() : std::abs(gv);
    vol = next;
    if (std::abs(step) <= tol || hi - lo <= tol) {
      return IvResult{vol, IvStatus::Ok, iter};
    }
  }
  return IvResult{best_vol, IvStatus::NotConverged, options.max_iter};
}

IvResult implied_vol_bsm(double price, const OptionSpec& s, const IvOptions& options) noexcept {
  if (!positive_finite(s.spot) || !positive_finite(s.strike) || !positive_finite(s.time) ||
      !std::isfinite(s.rate) || !std::isfinite(s.carry_yield)) {
    return fail(IvStatus::InvalidInput);
  }
  return implied_vol_black(s.type, price, bsm_forward(s), s.strike, s.time, bsm_discount(s),
                           options);
}

}  // namespace opt
