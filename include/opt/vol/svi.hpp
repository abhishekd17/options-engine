#pragma once
// Raw SVI total-variance smile (Gatheral 2004).
//
// For forward log-moneyness k = ln(K/F) and total variance w = sigma^2 tau:
//   w(k)   = a + b (rho (k - m) + R),  R = sqrt((k - m)^2 + s^2)
//   w'(k)  = b (rho + (k - m) / R)
//   w''(k) = b s^2 / R^3
// Valid parameters: b >= 0, |rho| < 1, s > 0, and the minimum total
// variance a + b s sqrt(1 - rho^2) >= 0.
#include <cmath>
namespace opt {
struct SviParams {
  double a = 0.0;
  double b = 0.0;
  double rho = 0.0;
  double m = 0.0;
  double s = 0.0;  // sigma in Gatheral's notation
};
[[nodiscard]] inline double svi_w(const SviParams& p, double k) noexcept {
  const double x = k - p.m;
  return p.a + p.b * (p.rho * x + std::sqrt(x * x + p.s * p.s));
}
[[nodiscard]] inline double svi_dw(const SviParams& p, double k) noexcept {
  const double x = k - p.m;
  return p.b * (p.rho + x / std::sqrt(x * x + p.s * p.s));
}
[[nodiscard]] inline double svi_d2w(const SviParams& p, double k) noexcept {
  const double x = k - p.m;
  const double r = std::sqrt(x * x + p.s * p.s);
  return p.b * p.s * p.s / (r * r * r);
}
// Minimum of w over k: a + b s sqrt(1 - rho^2).
[[nodiscard]] inline double svi_min_w(const SviParams& p) noexcept {
  return p.a + p.b * p.s * std::sqrt(1.0 - p.rho * p.rho);
}
[[nodiscard]] inline bool svi_valid(const SviParams& p) noexcept {
  return std::isfinite(p.a) && std::isfinite(p.b) && std::isfinite(p.rho) && std::isfinite(p.m) &&
         std::isfinite(p.s) && p.b >= 0.0 && std::abs(p.rho) < 1.0 && p.s > 0.0 &&
         svi_min_w(p) >= 0.0;
}
}  // namespace opt
