#include "opt/pricing/binomial.hpp"

#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace opt {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Value of exercising at time t on the deterministic (sigma == 0) path.
double exercise_value(const OptionSpec& s, double t) noexcept {
  const double w = omega(s.type);
  const double spot_t = s.spot * std::exp((s.rate - s.carry_yield) * t);
  return std::exp(-s.rate * t) * std::max(w * (spot_t - s.strike), 0.0);
}

double american_zero_vol(const OptionSpec& s) noexcept {
  double best = std::max(exercise_value(s, 0.0), exercise_value(s, s.time));
  // d/dt of e^{-rt}(S e^{(r-q)t} - K) = -q S e^{-qt} + r K e^{-rt}: zero at
  // t* = ln(q S / (r K)) / (q - r); the put's derivative has the same root.
  const double r = s.rate;
  const double q = s.carry_yield;
  if (r != q && r * q > 0.0) {
    const double t_star = std::log((q * s.spot) / (r * s.strike)) / (q - r);
    if (t_star > 0.0 && t_star < s.time) {
      best = std::max(best, exercise_value(s, t_star));
    }
  }
  return best;
}

// Node spots are clamped to [e^{-kLogSpotLimit}, e^{kLogSpotLimit}]. e^{+-700}
// is a normal double (the normal range ends near e^{+-708}) and leaves a
// factor e^9 of headroom before overflow for payoffs and the backward sums.
constexpr double kLogSpotLimit = 700.0;

// Index of the first node j in [0, count] with x0 + j * step >= x (step > 0).
std::size_t first_node_at_or_above(double x0, double step, double x, std::size_t count) noexcept {
  const double j = std::ceil((x - x0) / step);
  if (!(j > 0.0)) {
    return 0;
  }
  return j >= static_cast<double>(count) ? count : static_cast<std::size_t>(j);
}

}  // namespace

TreeResult crr_price(const OptionSpec& s, Exercise exercise, int steps,
                     std::span<double> workspace) {
  if (steps < 1 || steps > kMaxTreeSteps) {
    throw std::invalid_argument("crr_price: steps = " + std::to_string(steps) +
                                " is outside [1, " + std::to_string(kMaxTreeSteps) + "]");
  }
  const auto nodes = static_cast<std::size_t>(steps) + 1;
  if (workspace.size() < nodes) {
    throw std::invalid_argument("crr_price: workspace.size() = " +
                                std::to_string(workspace.size()) + " < steps + 1 = " +
                                std::to_string(nodes));
  }
  if (validate(s) != InputError::Ok) {
    return TreeResult{kNaN, TreeStatus::InvalidInput};
  }
  const double w = omega(s.type);
  if (s.time == 0.0) {
    return TreeResult{std::max(w * (s.spot - s.strike), 0.0), TreeStatus::Ok};
  }
  if (s.vol == 0.0) {
    const double v = exercise == Exercise::American ? american_zero_vol(s) : bsm_price(s);
    return TreeResult{v, TreeStatus::Ok};
  }

  const double dt = s.time / steps;
  const double sigma_sqrt_dt = s.vol * std::sqrt(dt);
  const double u = std::exp(sigma_sqrt_dt);
  const double d = 1.0 / u;
  const double growth = std::exp((s.rate - s.carry_yield) * dt);
  const double p = (growth - d) / (u - d);
  if (!(p > 0.0 && p < 1.0)) {
    return TreeResult{kNaN, TreeStatus::ProbabilityOutOfRange};
  }
  const double disc = std::exp(-s.rate * dt);
  const double disc_up = disc * p;
  const double disc_down = disc * (1.0 - p);
  const double u2 = u * u;
  const double strike = s.strike;

  // A node spot is S e^{x} with log-offset x = (2j - i) sigma sqrt dt at
  // level i, clamped to [x_low, x_high] so that S e^{x} stays in
  // [e^{-700}, e^{700}]. Working with the offset (not ln S + x) keeps the root
  // node exactly S.
  // Terminal payoffs, node j = number of up moves: one exp per node (N + 1
  // calls, negligible next to the O(N^2) backward pass), so no node inherits
  // the error of a subnormal, zero or infinite neighbour.
  double* v = workspace.data();
  const double log_spot = std::log(s.spot);
  const double x_low = -kLogSpotLimit - log_spot;
  const double x_high = kLogSpotLimit - log_spot;
  // Clamping is exact for a put (its payoff is 0 above e^{700} and K minus a
  // negligible amount below e^{-700}) and for the low end of a call. The
  // high end of a call is exact only while the clamped nodes carry no weight:
  // under the share measure ln(S_T / S) has mean (r - q) T + sigma^2 T / 2
  // and sd sigma sqrt(T), so require that mean plus 8 sd to stay below
  // x_high (a normal tail beyond 8 sd is about 6e-16). This trips only for
  // sigma sqrt(T) above about 30, and only when N is large enough for the
  // top node to reach e^{700}.
  if (s.type == OptionType::Call && static_cast<double>(steps) * sigma_sqrt_dt > x_high) {
    const double sd = s.vol * std::sqrt(s.time);
    const double mean = (s.rate - s.carry_yield) * s.time + 0.5 * sd * sd;
    if (mean + 8.0 * sd > x_high) {
      return TreeResult{kNaN, TreeStatus::VolTooHigh};
    }
  }
  const double step_log = 2.0 * sigma_sqrt_dt;
  for (std::size_t j = 0; j < nodes; ++j) {
    const double x = (2.0 * static_cast<double>(j) - steps) * sigma_sqrt_dt;
    const double spot = s.spot * std::exp(std::clamp(x, x_low, x_high));
    v[j] = std::max(w * (spot - strike), 0.0);
  }

  const bool american = exercise == Exercise::American;
  const double low_spot = s.spot * std::exp(x_low);
  const double high_spot = s.spot * std::exp(x_high);
  for (int level = steps - 1; level >= 0; --level) {
    const auto count = static_cast<std::size_t>(level) + 1;
    if (american) {
      // Nodes below e^{-700} or above e^{700} take the clamped spot. The
      // nodes in between start from an exact, normal anchor and walk up by
      // u^2, so they never pass through a subnormal or infinite value.
      const double x0 = -level * sigma_sqrt_dt;
      const std::size_t lo = first_node_at_or_above(x0, step_log, x_low, count);
      const std::size_t hi = std::max(lo, first_node_at_or_above(x0, step_log, x_high, count));
      const double intrinsic_low = w * (low_spot - strike);
      for (std::size_t j = 0; j < lo; ++j) {
        const double cont = disc_up * v[j + 1] + disc_down * v[j];
        v[j] = std::max(cont, intrinsic_low);
      }
      double node_spot =
          lo < hi ? s.spot * std::exp(x0 + static_cast<double>(lo) * step_log) : 0.0;
      for (std::size_t j = lo; j < hi; ++j) {
        const double cont = disc_up * v[j + 1] + disc_down * v[j];
        v[j] = std::max(cont, w * (node_spot - strike));
        node_spot *= u2;
      }
      const double intrinsic_high = w * (high_spot - strike);
      for (std::size_t j = hi; j < count; ++j) {
        const double cont = disc_up * v[j + 1] + disc_down * v[j];
        v[j] = std::max(cont, intrinsic_high);
      }
    } else {
      for (std::size_t j = 0; j < count; ++j) {
        v[j] = disc_up * v[j + 1] + disc_down * v[j];
      }
    }
  }
  return TreeResult{v[0], TreeStatus::Ok};
}

TreeResult crr_price(const OptionSpec& s, Exercise exercise, int steps) {
  if (steps < 1 || steps > kMaxTreeSteps) {
    throw std::invalid_argument("crr_price: steps = " + std::to_string(steps) +
                                " is outside [1, " + std::to_string(kMaxTreeSteps) + "]");
  }
  std::vector<double> workspace(static_cast<std::size_t>(steps) + 1);
  return crr_price(s, exercise, steps, workspace);
}

}  // namespace opt
