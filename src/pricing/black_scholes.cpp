#include "opt/pricing/black_scholes.hpp"

#include "opt/core/normal.hpp"
#include "opt/pricing/types.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace opt {

InputError validate(const OptionSpec& s) noexcept {
  if (!std::isfinite(s.spot) || !std::isfinite(s.strike) || !std::isfinite(s.time) ||
      !std::isfinite(s.rate) || !std::isfinite(s.carry_yield) || !std::isfinite(s.vol)) {
    return InputError::NonFinite;
  }
  if (!(s.spot > 0.0)) {
    return InputError::NonPositiveSpot;
  }
  if (!(s.strike > 0.0)) {
    return InputError::NonPositiveStrike;
  }
  if (s.time < 0.0) {
    return InputError::NegativeTime;
  }
  if (s.vol < 0.0) {
    return InputError::NegativeVol;
  }
  return InputError::Ok;
}

namespace {

// Moneyness weight for the zero-vol and zero-time limits: 1 in the money,
// 0 out of the money, 1/2 exactly at the money.
double itm_weight(double omega_times_diff) noexcept {
  if (omega_times_diff > 0.0) {
    return 1.0;
  }
  return omega_times_diff == 0.0 ? 0.5 : 0.0;
}

}  // namespace

double black_undiscounted(OptionType type, double forward, double strike, double time,
                          double vol) noexcept {
  const double w = omega(type);
  const double sd = vol * std::sqrt(time);
  if (sd == 0.0) {
    return std::max(w * (forward - strike), 0.0);
  }
  const double d1 = (std::log(forward / strike) + 0.5 * sd * sd) / sd;
  const double d2 = d1 - sd;
  // Rounding can push a deep out-of-the-money value a hair below 0.
  return std::max(w * (forward * norm_cdf(w * d1) - strike * norm_cdf(w * d2)), 0.0);
}

Greeks black76(OptionType type, double forward, double strike, double time, double discount,
               double vol) noexcept {
  assert(forward > 0.0 && strike > 0.0 && time >= 0.0 && discount > 0.0 && vol >= 0.0);
  const double w = omega(type);
  Greeks g;
  const double sd = vol * std::sqrt(time);
  if (sd == 0.0) {
    const double intrinsic = std::max(w * (forward - strike), 0.0);
    g.price = discount * intrinsic;
    g.delta = w * discount * itm_weight(w * (forward - strike));
    if (time > 0.0) {
      const double r = -std::log(discount) / time;
      g.theta = r * g.price;
      g.rho = -time * g.price;
    }
    return g;
  }
  const double sqrt_t = std::sqrt(time);
  const double d1 = (std::log(forward / strike) + 0.5 * sd * sd) / sd;
  const double d2 = d1 - sd;
  const double nd1 = norm_cdf(w * d1);
  const double nd2 = norm_cdf(w * d2);
  const double pdf = norm_pdf(d1);
  const double r = -std::log(discount) / time;
  g.price = discount * std::max(w * (forward * nd1 - strike * nd2), 0.0);
  g.delta = w * discount * nd1;
  g.rho = -time * g.price;
  if (pdf == 0.0) {  // |d1| > 38: every density term is 0 (and d1 d2 may overflow)
    g.theta = r * g.price;
    return g;
  }
  g.gamma = discount * pdf / (forward * sd);
  g.vega = discount * forward * pdf * sqrt_t;
  g.theta = r * g.price - discount * forward * pdf * vol / (2.0 * sqrt_t);
  g.vanna = -discount * pdf * d2 / vol;
  g.volga = g.vega * d1 * d2 / vol;
  return g;
}

double bsm_forward(const OptionSpec& s) noexcept {
  return s.spot * std::exp((s.rate - s.carry_yield) * s.time);
}

double bsm_discount(const OptionSpec& s) noexcept {
  return std::exp(-s.rate * s.time);
}

double bsm_price(const OptionSpec& s) noexcept {
  assert(validate(s) == InputError::Ok);
  const double w = omega(s.type);
  if (s.time == 0.0) {
    return std::max(w * (s.spot - s.strike), 0.0);
  }
  return bsm_discount(s) * black_undiscounted(s.type, bsm_forward(s), s.strike, s.time, s.vol);
}

Greeks bsm_greeks(const OptionSpec& s) noexcept {
  assert(validate(s) == InputError::Ok);
  const double w = omega(s.type);
  const double spot = s.spot;
  const double strike = s.strike;
  const double t = s.time;
  const double r = s.rate;
  const double q = s.carry_yield;
  Greeks g;
  if (t == 0.0) {
    g.price = std::max(w * (spot - strike), 0.0);
    g.delta = w * itm_weight(w * (spot - strike));
    return g;
  }
  const double fwd = bsm_forward(s);
  const double disc = bsm_discount(s);
  const double disc_q = std::exp(-q * t);
  const double sd = s.vol * std::sqrt(t);
  if (sd == 0.0) {
    const double weight = itm_weight(w * (fwd - strike));
    g.price = disc * std::max(w * (fwd - strike), 0.0);
    g.delta = weight * w * disc_q;
    g.rho = weight * w * strike * t * disc;
    g.theta = weight * w * (q * spot * disc_q - r * strike * disc);
    return g;
  }
  const double sqrt_t = std::sqrt(t);
  const double d1 = (std::log(fwd / strike) + 0.5 * sd * sd) / sd;
  const double d2 = d1 - sd;
  const double nd1 = norm_cdf(w * d1);
  const double nd2 = norm_cdf(w * d2);
  const double pdf = norm_pdf(d1);
  g.price = disc * std::max(w * (fwd * nd1 - strike * nd2), 0.0);
  g.delta = w * disc_q * nd1;
  g.rho = w * strike * t * disc * nd2;
  const double carry_terms = -w * r * strike * disc * nd2 + w * q * spot * disc_q * nd1;
  if (pdf == 0.0) {  // |d1| > 38: every density term is 0 (and d1 d2 may overflow)
    g.theta = carry_terms;
    return g;
  }
  g.gamma = disc_q * pdf / (spot * sd);
  g.vega = spot * disc_q * pdf * sqrt_t;
  g.theta = -spot * disc_q * pdf * s.vol / (2.0 * sqrt_t) + carry_terms;
  g.vanna = -disc_q * pdf * d2 / s.vol;
  g.volga = g.vega * d1 * d2 / s.vol;
  return g;
}

}  // namespace opt
