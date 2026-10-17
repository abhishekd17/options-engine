#include "opt/vol/surface.hpp"
#include "opt/core/fast_math.hpp"
#include "opt/vol/svi.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>
namespace opt {
namespace {
bool positive_finite(double x) noexcept { return x > 0.0 && std::isfinite(x); }
}  // namespace
VolSurface::VolSurface(std::vector<SurfaceSlice> slices) : slices_(std::move(slices)) {
  if (slices_.empty()) {
    throw std::invalid_argument("VolSurface: at least one slice is required");
  }
  if (!validate_order()) {
    throw std::invalid_argument(
        "VolSurface: slice taus must be strictly increasing, taus and forwards positive and "
        "finite");
  }
  ln_forward_.reserve(slices_.size());
  for (const auto& s : slices_) {
    ln_forward_.push_back(fast_log(s.forward));
  }
}
bool VolSurface::validate_order() const noexcept {
  for (std::size_t i = 0; i < slices_.size(); ++i) {
    if (!positive_finite(slices_[i].tau) || !positive_finite(slices_[i].forward)) {
      return false;
    }
    if (i > 0 && !(slices_[i - 1].tau < slices_[i].tau)) {
      return false;
    }
  }
  return true;
}
void VolSurface::update_slice(std::size_t i, double tau, double forward,
                              const SviParams& p) noexcept {
  assert(i < slices_.size());
  assert(positive_finite(tau) && positive_finite(forward));
  assert(i == 0 || slices_[i - 1].tau < tau);
  assert(i + 1 == slices_.size() || tau < slices_[i + 1].tau);
  slices_[i].tau = tau;
  slices_[i].forward = forward;
  slices_[i].params = p;
  ln_forward_[i] = fast_log(forward);
}
double VolSurface::total_variance(double k, double tau) const noexcept {
  const std::size_t n = slices_.size();
  if (tau <= slices_.front().tau) {
    const auto& s = slices_.front();
    return tau == s.tau ? svi_w(s.params, k) : svi_w(s.params, k) * (tau / s.tau);
  }
  if (tau >= slices_.back().tau) {
    const auto& s = slices_.back();
    return tau == s.tau ? svi_w(s.params, k) : svi_w(s.params, k) * (tau / s.tau);
  }
  // First slice with tau_j >= tau; j >= 1 here.
  const auto it = std::lower_bound(slices_.begin(), slices_.end(), tau,
                                   [](const SurfaceSlice& s, double t) { return s.tau < t; });
  const auto j = static_cast<std::size_t>(it - slices_.begin());
  const auto& hi = slices_[j];
  if (hi.tau == tau || j == 0 || j >= n) {
    return svi_w(hi.params, k);
  }
  const auto& lo = slices_[j - 1];
  const double w1 = svi_w(lo.params, k);
  const double w2 = svi_w(hi.params, k);
  const double x = (tau - lo.tau) / (hi.tau - lo.tau);
  return w1 + (w2 - w1) * x;
}
double VolSurface::log_forward(double tau) const noexcept {
  if (tau <= slices_.front().tau) {
    return ln_forward_.front();
  }
  if (tau >= slices_.back().tau) {
    return ln_forward_.back();
  }
  const auto it = std::lower_bound(slices_.begin(), slices_.end(), tau,
                                   [](const SurfaceSlice& s, double t) { return s.tau < t; });
  const auto j = static_cast<std::size_t>(it - slices_.begin());
  if (slices_[j].tau == tau) {
    return ln_forward_[j];
  }
  const double x = (tau - slices_[j - 1].tau) / (slices_[j].tau - slices_[j - 1].tau);
  return ln_forward_[j - 1] + (ln_forward_[j] - ln_forward_[j - 1]) * x;
}
double VolSurface::forward(double tau) const noexcept {
  if (tau <= slices_.front().tau) {
    return slices_.front().forward;
  }
  if (tau >= slices_.back().tau) {
    return slices_.back().forward;
  }
  const auto it = std::lower_bound(slices_.begin(), slices_.end(), tau,
                                   [](const SurfaceSlice& s, double t) { return s.tau < t; });
  if (it->tau == tau) {
    return it->forward;
  }
  return fast_exp(log_forward(tau));
}
double VolSurface::vol(double strike, double tau) const noexcept {
  const double k = fast_log(strike) - log_forward(tau);
  return std::sqrt(total_variance(k, tau) / tau);
}
}  // namespace opt
