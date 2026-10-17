#include "opt/mm/synthetic.hpp"

#include "opt/core/fast_math.hpp"
#include "opt/core/rng.hpp"
#include "opt/mm/config.hpp"
#include "opt/vol/arbitrage.hpp"
#include "opt/vol/surface.hpp"
#include "opt/vol/svi.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace opt::mm {
namespace {

bool positive_finite(double x) noexcept { return x > 0.0 && std::isfinite(x); }

std::vector<double> expiry_years(const SyntheticConfig& c) {
  if (c.expiry_days.empty()) {
    throw std::invalid_argument("synthetic: expiry_days is empty");
  }
  std::vector<double> out;
  out.reserve(c.expiry_days.size());
  for (std::size_t i = 0; i < c.expiry_days.size(); ++i) {
    const double d = c.expiry_days[i];
    if (!positive_finite(d) || (i > 0 && !(c.expiry_days[i - 1] < d))) {
      throw std::invalid_argument("synthetic: expiry_days must be positive and strictly increasing");
    }
    out.push_back(d / 365.0);
  }
  return out;
}

VolSurface initial_surface(const SyntheticConfig& c, const std::vector<double>& expiry) {
  std::vector<SurfaceSlice> slices;
  slices.reserve(expiry.size());
  for (const double tau : expiry) {
    slices.push_back({tau, c.spot0, synthetic_svi(c, c.vol0, tau)});
  }
  return VolSurface{std::move(slices)};
}

}  // namespace

SviParams synthetic_svi(const SyntheticConfig& c, double v, double tau) noexcept {
  SviParams p;
  p.b = c.svi_beta * v * std::sqrt(tau);
  p.rho = c.svi_rho;
  p.m = c.svi_m;
  p.s = c.svi_s;
  p.a = v * v * tau - p.b * p.s;
  return p;
}

bool surface_arbitrage_free(const VolSurface& surface) noexcept {
  for (std::size_t i = 0; i < surface.size(); ++i) {
    const auto& s = surface.slice(i);
    if (butterfly_check(s.params, s.tau, kGuardKLo, kGuardKHi, kGuardPoints).violations != 0) {
      return false;
    }
    if (i > 0) {
      const auto& p = surface.slice(i - 1);
      if (calendar_check(p.params, p.tau, s.params, s.tau, kGuardKLo, kGuardKHi, kGuardPoints)
              .violations != 0) {
        return false;
      }
    }
  }
  return true;
}

SyntheticMarket::SyntheticMarket(const SimConfig& config)
    : c_(config.synthetic),
      rng_(make_stream(config.seed, kMarketStream)),
      steps_(config.steps),
      dt_(config.dt_sec / kSecondsPerYear),
      sqrt_dt_(std::sqrt(dt_)),
      theta_(fast_log(config.synthetic.vol_mean)),
      rho_c_(std::sqrt(1.0 - config.synthetic.rho_sv * config.synthetic.rho_sv)),
      expiry_(expiry_years(config.synthetic)),
      surface_(initial_surface(config.synthetic, expiry_)),
      s_(config.synthetic.spot0),
      x_(fast_log(config.synthetic.vol0)),
      v_(config.synthetic.vol0) {
  if (!surface_arbitrage_free(surface_)) {
    throw std::invalid_argument("synthetic: initial surface fails the static-arbitrage guard");
  }
}

void SyntheticMarket::update_slices() noexcept {
  for (std::size_t i = 0; i < expiry_.size(); ++i) {
    const double tau = expiry_[i] - t_;
    surface_.update_slice(i, tau, s_, synthetic_svi(c_, v_, tau));
  }
}

bool SyntheticMarket::next(MarketState& m) noexcept {
  if (started_) {
    if (n_ >= steps_) {
      return false;
    }
    const double z1 = fast_norm_inv(uniform01(rng_));
    const double z2 = fast_norm_inv(uniform01(rng_));
    // Spot uses v at the start of the step.
    s_ = s_ * fast_exp(-0.5 * v_ * v_ * dt_ + v_ * sqrt_dt_ * z1);
    x_ = x_ + c_.vol_kappa * (theta_ - x_) * dt_ +
         c_.vol_xi * sqrt_dt_ * (c_.rho_sv * z1 + rho_c_ * z2);
    v_ = fast_exp(x_);
    ++n_;
    t_ = static_cast<double>(n_) * dt_;
    update_slices();
    if (n_ % kGuardEvery == 0 && !surface_arbitrage_free(surface_)) {
      arbitrage_ok_ = false;
    }
  }
  started_ = true;
  m.t_years = t_;
  m.spot = s_;
  m.surface = &surface_;
  m.surface_ok = true;
  m.arbitrage_ok = arbitrage_ok_;
  m.listed = {};
  return true;
}

std::vector<Instrument> synthetic_instruments(const SimConfig& config) {
  const auto& c = config.synthetic;
  std::vector<Instrument> out;
  const std::size_t ne = std::min(config.quoted_expiries, c.expiry_days.size());
  for (std::size_t e = 0; e < ne; ++e) {
    for (const double k : config.target_k) {
      Instrument in;
      in.type = k < 0.0 ? OptionType::Put : OptionType::Call;
      in.strike = std::round(c.spot0 * fast_exp(k) / c.strike_round) * c.strike_round;
      in.expiry = c.expiry_days[e] / 365.0;
      char buf[64];
      std::snprintf(buf, sizeof buf, "SYN-%gD-%.0f-%c", c.expiry_days[e], in.strike,
                    in.type == OptionType::Put ? 'P' : 'C');
      in.name = buf;
      out.push_back(std::move(in));
    }
  }
  return out;
}

std::string validate(const SimConfig& c) {
  auto bad = [](const char* field, const char* why) { return std::string{field} + ": " + why; };
  if (!positive_finite(c.dt_sec)) return bad("dt_sec", "must be positive and finite");
  if (c.steps == 0) return bad("steps", "must be at least 1");
  if (c.target_k.empty()) return bad("target_k", "is empty");
  for (const double k : c.target_k) {
    if (!std::isfinite(k) || std::abs(k) > 2.0) return bad("target_k", "values must be in [-2, 2]");
  }
  if (c.quoted_expiries == 0) return bad("quoted_expiries", "must be at least 1");
  const auto& q = c.quoter;
  if (q.size < 1) return bad("quote size", "must be at least 1 contract");
  if (!(q.half_spread >= 0.0) || !(q.w_inv >= 0.0) || !(q.w_vega >= 0.0) ||
      !(q.w_gamma >= 0.0) || !std::isfinite(q.half_spread + q.w_inv + q.w_vega + q.w_gamma)) {
    return bad("quoter", "spread parameters must be non-negative and finite");
  }
  if (!std::isfinite(q.k_inv) || !std::isfinite(q.k_vega)) return bad("quoter", "skew must be finite");
  if (!(q.min_vol >= 1e-6) || q.min_vol > 1.0) return bad("min_vol", "must be in [1e-6, 1]");
  const auto& l = c.limits;
  if (l.max_position < 1) return bad("max_position", "must be at least 1");
  if (!positive_finite(l.max_net_delta)) return bad("max_net_delta", "must be positive");
  if (!positive_finite(l.max_vega)) return bad("max_vega", "must be positive");
  if (!positive_finite(l.max_gamma_1pct)) return bad("max_gamma_1pct", "must be positive");
  if (!(c.hedge.threshold >= 0.0) || !std::isfinite(c.hedge.threshold)) {
    return bad("hedge threshold", "must be non-negative and finite");
  }
  if (!(c.hedge.cost_bps >= 0.0) || !std::isfinite(c.hedge.cost_bps)) {
    return bad("hedge cost_bps", "must be non-negative and finite");
  }
  if (!(c.fill.intensity >= 0.0) || !std::isfinite(c.fill.intensity)) {
    return bad("fill intensity", "must be non-negative and finite");
  }
  if (!(c.fill.kappa >= 0.0) || !std::isfinite(c.fill.kappa)) {
    return bad("fill kappa", "must be non-negative and finite");
  }
  if (c.mode == Mode::Recorded) {
    return {};
  }
  const auto& s = c.synthetic;
  if (!positive_finite(s.spot0)) return bad("spot0", "must be positive");
  if (!positive_finite(s.vol0) || s.vol0 > 5.0) return bad("vol0", "must be in (0, 5]");
  if (!positive_finite(s.vol_mean) || s.vol_mean > 5.0) return bad("vol_mean", "must be in (0, 5]");
  if (!(s.vol_kappa >= 0.0) || !std::isfinite(s.vol_kappa)) return bad("vol_kappa", "must be >= 0");
  if (!(s.vol_xi >= 0.0) || !std::isfinite(s.vol_xi)) return bad("vol_xi", "must be >= 0");
  if (!(std::abs(s.rho_sv) < 1.0)) return bad("rho_sv", "must be in (-1, 1)");
  if (!(std::abs(s.svi_rho) < 1.0)) return bad("svi_rho", "must be in (-1, 1)");
  if (!positive_finite(s.svi_s)) return bad("svi_s", "must be positive");
  if (!(s.svi_beta >= 0.0) || !std::isfinite(s.svi_beta)) return bad("svi_beta", "must be >= 0");
  if (!positive_finite(s.strike_round)) return bad("strike_round", "must be positive");
  if (s.expiry_days.empty()) return bad("expiry_days", "is empty");
  for (std::size_t i = 0; i < s.expiry_days.size(); ++i) {
    if (!positive_finite(s.expiry_days[i]) || (i > 0 && !(s.expiry_days[i - 1] < s.expiry_days[i]))) {
      return bad("expiry_days", "must be positive and strictly increasing");
    }
  }
  if (c.quoted_expiries > s.expiry_days.size()) {
    return bad("quoted_expiries", "exceeds the number of synthetic expiries");
  }
  const double horizon_days = static_cast<double>(c.steps) * c.dt_sec / 86'400.0;
  if (!(horizon_days < s.expiry_days.front())) {
    return "horizon (steps x dt = " + std::to_string(horizon_days) +
           " days) must be shorter than the first expiry (" +
           std::to_string(s.expiry_days.front()) + " days)";
  }
  std::vector<double> expiry;
  for (const double d : s.expiry_days) expiry.push_back(d / 365.0);
  if (!surface_arbitrage_free(initial_surface(s, expiry))) {
    return "synthetic surface: initial smile fails the static-arbitrage guard";
  }
  return {};
}

}  // namespace opt::mm
