#pragma once
// Volatility surface from fitted SVI slices.
//
// Slices are sorted by tau (strictly increasing). Interpolation is in total
// variance at fixed forward log-moneyness k:
//   - at a slice tau: that slice's w(k);
//   - between slices: linear in tau (keeps calendar monotonicity when the
//     slices satisfy it);
//   - before the first slice: w_1(k) tau / tau_1 (constant vol);
//   - after the last slice:   w_N(k) tau / tau_N.
// Forward: ln F linear in tau between slices, flat outside.
// vol(K, tau) = sqrt(w(ln(K / F(tau)), tau) / tau). Evaluation uses fast_log
// and std::sqrt only, so results are bit-identical across platforms.
#include "opt/vol/svi.hpp"
#include <cstddef>
#include <span>
#include <vector>
namespace opt {
struct SurfaceSlice {
  double tau = 0.0;      // years
  double forward = 0.0;  // forward price for this expiry
  SviParams params;
};
class VolSurface {
 public:
  // Throws std::invalid_argument when slices is empty, taus are not strictly
  // increasing, or a tau or forward is not positive and finite.
  explicit VolSurface(std::vector<SurfaceSlice> slices);
  [[nodiscard]] std::size_t size() const noexcept { return slices_.size(); }
  [[nodiscard]] const SurfaceSlice& slice(std::size_t i) const noexcept { return slices_[i]; }
  [[nodiscard]] std::span<const SurfaceSlice> slices() const noexcept { return slices_; }
  // Total variance at log-moneyness k = ln(K / F(tau)); tau > 0.
  [[nodiscard]] double total_variance(double k, double tau) const noexcept;
  [[nodiscard]] double forward(double tau) const noexcept;
  // Black vol at strike K and expiry tau > 0.
  [[nodiscard]] double vol(double strike, double tau) const noexcept;
  // Replace slice i's tau, forward and SVI parameters in place. No allocation.
  // Preconditions: i < size(); tau > 0, forward > 0, both finite; the order of
  // slices by tau is unchanged (tau_{i-1} < tau < tau_{i+1}). Asserted in
  // debug builds.
  void update_slice(std::size_t i, double tau, double forward, const SviParams& p) noexcept;
  // True iff slice taus are strictly increasing and every tau, forward is
  // positive and finite. No allocation.
  [[nodiscard]] bool validate_order() const noexcept;

 private:
  [[nodiscard]] double log_forward(double tau) const noexcept;
  std::vector<SurfaceSlice> slices_;
  std::vector<double> ln_forward_;  // fast_log of each slice forward
};
}  // namespace opt
