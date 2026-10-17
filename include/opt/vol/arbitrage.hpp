#pragma once
// Static-arbitrage checks for SVI slices and surfaces. They only report;
// they never alter a fit.
//
// Butterfly (per slice): the risk-neutral density is non-negative iff
//   g(k) = (1 - k w'/(2w))^2 - (w'^2/4)(1/w + 1/4) + w''/2 >= 0
// with w(k) > 0 (Gatheral and Jacquier, "Arbitrage-free SVI volatility
// surfaces", Quantitative Finance 14(1), 2014; arXiv:1204.0646v1, Lemma 4.1
// and eq. (4.1)).
// Calendar (adjacent slices tau_1 < tau_2): w_2(k) >= w_1(k) at fixed forward
// log-moneyness k.
// Lee wing slopes: b (1 + rho) and b (1 - rho) must not exceed 2 (Lee's
// moment formula); the fitter's domain only bounds them by 4.
#include "opt/vol/surface.hpp"
#include "opt/vol/svi.hpp"
#include <cstddef>
#include <span>
#include <vector>
namespace opt {
struct SliceCheck {
  double min_value = 0.0;  // min g(k) or min (w_2 - w_1)
  double k_at_min = 0.0;
  int violations = 0;      // grid points with g < 0 (or w <= 0), or w_2 < w_1
};
struct SliceRange {
  double k_min = 0.0;  // log-moneyness range of a slice's fit data
  double k_max = 0.0;
};
inline constexpr int kArbitrageGridPoints = 401;
inline constexpr double kArbitrageGridPad = 0.5;
inline constexpr double kLeeMaxSlope = 2.0;
// Butterfly function g(k); -infinity when w(k) <= 0 or w is not finite.
[[nodiscard]] double butterfly_g(const SviParams& p, double k) noexcept;
// Evaluates `points` uniform k values over [k_lo, k_hi] on the fly; allocates
// nothing. min_value = min g(k), a point with w(k) <= 0 counts as a violation.
[[nodiscard]] SliceCheck butterfly_check(const SviParams& p, double tau, double k_lo,
                                         double k_hi, int points) noexcept;
// min_value = min (w_2(k) - w_1(k)); violations = points with w_2 < w_1.
[[nodiscard]] SliceCheck calendar_check(const SviParams& p1, double tau1, const SviParams& p2,
                                        double tau2, double k_lo, double k_hi,
                                        int points) noexcept;
struct SliceArbitrage {
  double tau = 0.0;
  double k_lo = 0.0;
  double k_hi = 0.0;
  SliceCheck butterfly;
  double right_slope = 0.0;  // b (1 + rho)
  double left_slope = 0.0;   // b (1 - rho)
  bool lee_ok = true;
};
struct CalendarArbitrage {
  std::size_t first = 0;  // slices first and first + 1
  double tau1 = 0.0;
  double tau2 = 0.0;
  double k_lo = 0.0;
  double k_hi = 0.0;
  SliceCheck check;
};
struct ArbitrageReport {
  std::vector<SliceArbitrage> slices;
  std::vector<CalendarArbitrage> calendar;
  int butterfly_slices_violating = 0;
  int calendar_pairs_violating = 0;
  int lee_slices_violating = 0;
  [[nodiscard]] bool arbitrage_free() const noexcept {
    return butterfly_slices_violating == 0 && calendar_pairs_violating == 0 &&
           lee_slices_violating == 0;
  }
};
// Butterfly on [k_min - 0.5, k_max + 0.5] of each slice's data, calendar on
// the union range of each adjacent pair padded by 0.5, both on
// kArbitrageGridPoints points. ranges.size() must equal surface.size()
// (std::invalid_argument otherwise). Allocates.
[[nodiscard]] ArbitrageReport check_surface(const VolSurface& surface,
                                            std::span<const SliceRange> ranges);
}  // namespace opt
