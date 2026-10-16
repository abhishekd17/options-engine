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

  // Terminal payoffs, node j = number of up moves.
  double* v = workspace.data();
  double spot = s.spot * std::exp(-steps * sigma_sqrt_dt);
  for (std::size_t j = 0; j < nodes; ++j) {
    v[j] = std::max(w * (spot - strike), 0.0);
    spot *= u2;
  }

  const bool american = exercise == Exercise::American;
  for (int level = steps - 1; level >= 0; --level) {
    const auto count = static_cast<std::size_t>(level) + 1;
    if (american) {
      double node_spot = s.spot * std::exp(-level * sigma_sqrt_dt);
      for (std::size_t j = 0; j < count; ++j) {
        const double cont = disc_up * v[j + 1] + disc_down * v[j];
        v[j] = std::max(cont, w * (node_spot - strike));
        node_spot *= u2;
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
