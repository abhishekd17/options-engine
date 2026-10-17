#pragma once
// Robust SVI fit of one expiry: Zeliade quasi-explicit calibration
// (Zeliade Systems, "Quasi-explicit calibration of Gatheral's SVI model",
// 2009) with a Huber loss by iteratively reweighted least squares.
//
// For fixed (m, s), with y = (k - m) / s, raw SVI is
//   w = a + d y + c sqrt(y^2 + 1),  c = b s,  d = rho b s,
// linear in (a, c, d). The inner problem is a weighted least squares on the
// Zeliade domain 0 <= c <= 4s, |d| <= c, |d| <= 4s - c, 0 <= a <= max_i w_i
// (8 half-planes), solved exactly: the unconstrained normal equations, else
// every active set of 1, 2 or 3 constraints (92 small KKT solves), keeping the
// feasible candidate with the lowest objective. The outer problem over
// (m, ln s) is a 2-D Nelder-Mead from a deterministic 5 x 4 grid of starts,
// with a quadratic penalty outside m in [k_min - 1, k_max + 1],
// s in [1e-4, 2].
//
// Robustness: an initial fit with unit weights, then 3 reweighting passes
// with weight_i = min(1, delta_i / |r_i|), delta_i = 0.02 sigma_i tau (one
// vol point expressed in total variance, dw = 2 sigma tau dsigma), each a
// single Nelder-Mead from the previous best (m, s).
//
// The domain bound c <= 4s is the wing slope b (1 + |rho|) <= 4
// (Rogers-Tehranchi); Lee's moment formula gives the stricter asymptotic
// bound 2, which the arbitrage report checks (see arbitrage.hpp).
#include "opt/vol/svi.hpp"
#include <cstdint>
#include <limits>
#include <span>
namespace opt {
enum class SviStatus : std::uint8_t { Ok, TooFewPoints, NotConverged, Failed };
[[nodiscard]] constexpr const char* to_string(SviStatus s) noexcept {
  switch (s) {
    case SviStatus::Ok: return "Ok";
    case SviStatus::TooFewPoints: return "TooFewPoints";
    case SviStatus::NotConverged: return "NotConverged";
    case SviStatus::Failed: return "Failed";
  }
  return "?";
}
struct SviFitOptions {
  int min_points = 5;
  int max_iter = 400;      // Nelder-Mead iterations per start
  int irls_passes = 3;     // Huber reweighting passes (0 = plain least squares)
  double huber_volpts = 1.0;  // Huber threshold in vol points
};
struct SviFitResult {
  SviParams p;
  SviStatus status = SviStatus::Failed;
  double rmse_vol = std::numeric_limits<double>::quiet_NaN();         // vol points
  double max_abs_vol_err = std::numeric_limits<double>::quiet_NaN();  // vol points
  int n_points = 0;
  int iterations = 0;  // Nelder-Mead iterations: winning start plus every IRLS pass
};
// Fits raw SVI to total variances w[i] at log-moneyness k[i] for expiry tau
// (years). k and w must have the same length; w[i] > 0. RMSE and max error
// are unweighted over all points, in vol points: sqrt(w_fit / tau) vs
// sqrt(w / tau), times 100.
[[nodiscard]] SviFitResult fit_svi(std::span<const double> k, std::span<const double> w,
                                   double tau, const SviFitOptions& options = {});
namespace detail {
// Result of the inner problem for fixed (m, s): w = a + d y + c sqrt(y^2 + 1).
struct SviInner {
  double a = 0.0;
  double c = 0.0;
  double d = 0.0;
  double objective = std::numeric_limits<double>::infinity();  // sum weight r^2
};
// Exact weighted least squares on the Zeliade domain. weight may be empty
// (all ones). a_max is the upper bound on a (the fitter uses max_i w_i).
[[nodiscard]] SviInner svi_inner_fit(double m, double s, std::span<const double> k,
                                     std::span<const double> w, std::span<const double> weight,
                                     double a_max) noexcept;
// True iff (a, c, d) is in the Zeliade domain for s and a_max, with an
// absolute tolerance tol on each of the 8 constraints.
[[nodiscard]] bool svi_inner_feasible(double a, double c, double d, double s, double a_max,
                                      double tol) noexcept;
[[nodiscard]] SviParams svi_from_inner(const SviInner& in, double m, double s) noexcept;
}  // namespace detail
}  // namespace opt
