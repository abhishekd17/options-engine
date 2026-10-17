#include "opt/vol/arbitrage.hpp"
#include "opt/vol/surface.hpp"
#include "opt/vol/svi.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
namespace opt {
namespace {
double grid_k(double k_lo, double k_hi, int points, int i) noexcept {
  return points <= 1 ? k_lo
                     : k_lo + (k_hi - k_lo) * static_cast<double>(i) /
                                  static_cast<double>(points - 1);
}
}  // namespace
double butterfly_g(const SviParams& p, double k) noexcept {
  const double w = svi_w(p, k);
  if (!(w > 0.0) || !std::isfinite(w)) {
    return -std::numeric_limits<double>::infinity();
  }
  const double w1 = svi_dw(p, k);
  const double w2 = svi_d2w(p, k);
  const double t = 1.0 - k * w1 / (2.0 * w);
  return t * t - 0.25 * w1 * w1 * (1.0 / w + 0.25) + 0.5 * w2;
}
SliceCheck butterfly_check(const SviParams& p, double /*tau*/, double k_lo, double k_hi,
                           int points) noexcept {
  // g depends on total variance only, so tau does not enter; it is kept in
  // the signature so callers pass the slice as a whole.
  SliceCheck out{std::numeric_limits<double>::infinity(), k_lo, 0};
  for (int i = 0; i < points; ++i) {
    const double k = grid_k(k_lo, k_hi, points, i);
    const double g = butterfly_g(p, k);
    if (g < 0.0) {
      ++out.violations;
    }
    if (g < out.min_value) {
      out.min_value = g;
      out.k_at_min = k;
    }
  }
  return out;
}
SliceCheck calendar_check(const SviParams& p1, double /*tau1*/, const SviParams& p2,
                          double /*tau2*/, double k_lo, double k_hi, int points) noexcept {
  SliceCheck out{std::numeric_limits<double>::infinity(), k_lo, 0};
  for (int i = 0; i < points; ++i) {
    const double k = grid_k(k_lo, k_hi, points, i);
    const double diff = svi_w(p2, k) - svi_w(p1, k);
    if (diff < 0.0) {
      ++out.violations;
    }
    if (diff < out.min_value) {
      out.min_value = diff;
      out.k_at_min = k;
    }
  }
  return out;
}
ArbitrageReport check_surface(const VolSurface& surface, std::span<const SliceRange> ranges) {
  if (ranges.size() != surface.size()) {
    throw std::invalid_argument("check_surface: one SliceRange per slice is required");
  }
  ArbitrageReport rep;
  rep.slices.reserve(surface.size());
  for (std::size_t i = 0; i < surface.size(); ++i) {
    const auto& s = surface.slice(i);
    SliceArbitrage a;
    a.tau = s.tau;
    a.k_lo = ranges[i].k_min - kArbitrageGridPad;
    a.k_hi = ranges[i].k_max + kArbitrageGridPad;
    a.butterfly = butterfly_check(s.params, s.tau, a.k_lo, a.k_hi, kArbitrageGridPoints);
    a.right_slope = s.params.b * (1.0 + s.params.rho);
    a.left_slope = s.params.b * (1.0 - s.params.rho);
    a.lee_ok = a.right_slope <= kLeeMaxSlope && a.left_slope <= kLeeMaxSlope;
    rep.butterfly_slices_violating += a.butterfly.violations > 0 ? 1 : 0;
    rep.lee_slices_violating += a.lee_ok ? 0 : 1;
    rep.slices.push_back(a);
  }
  for (std::size_t i = 0; i + 1 < surface.size(); ++i) {
    const auto& s1 = surface.slice(i);
    const auto& s2 = surface.slice(i + 1);
    CalendarArbitrage c;
    c.first = i;
    c.tau1 = s1.tau;
    c.tau2 = s2.tau;
    c.k_lo = std::min(ranges[i].k_min, ranges[i + 1].k_min) - kArbitrageGridPad;
    c.k_hi = std::max(ranges[i].k_max, ranges[i + 1].k_max) + kArbitrageGridPad;
    c.check = calendar_check(s1.params, s1.tau, s2.params, s2.tau, c.k_lo, c.k_hi,
                             kArbitrageGridPoints);
    rep.calendar_pairs_violating += c.check.violations > 0 ? 1 : 0;
    rep.calendar.push_back(c);
  }
  return rep;
}
}  // namespace opt
