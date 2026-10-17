#include "opt/core/normal.hpp"
#include "opt/core/rng.hpp"
#include "opt/vol/svi.hpp"
#include "opt/vol/svi_fit.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <vector>
namespace {
constexpr double kTau = 1.0;
// The 4 parameter sets of design 7.9, inside the Zeliade domain.
const opt::SviParams kSets[] = {
    {0.04, 0.10, -0.30, 0.00, 0.10},
    {0.02, 0.20, -0.60, 0.10, 0.20},
    {0.01, 0.05, 0.20, -0.10, 0.30},
    {0.10, 0.30, -0.90, 0.20, 0.05},
};
struct Smile {
  std::vector<double> k;
  std::vector<double> w;
};
Smile make_smile(const opt::SviParams& p, int n = 41) {
  Smile s;
  for (int i = 0; i < n; ++i) {
    const double k = -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(n - 1);
    s.k.push_back(k);
    s.w.push_back(opt::svi_w(p, k));
  }
  return s;
}
double clean_rmse_volpts(const opt::SviParams& fit, const opt::SviParams& truth,
                         const Smile& s, std::size_t skip) {
  double sum = 0.0;
  std::size_t n = 0;
  for (std::size_t i = 0; i < s.k.size(); ++i) {
    if (i == skip) {
      continue;
    }
    const double e = (std::sqrt(opt::svi_w(fit, s.k[i]) / kTau) -
                      std::sqrt(opt::svi_w(truth, s.k[i]) / kTau)) *
                     100.0;
    sum += e * e;
    ++n;
  }
  return std::sqrt(sum / static_cast<double>(n));
}
TEST(SviFit, RecoversKnownParametersNoiseFree) {
  for (const auto& p : kSets) {
    const Smile s = make_smile(p);
    const auto r = opt::fit_svi(s.k, s.w, kTau);
    ASSERT_EQ(r.status, opt::SviStatus::Ok);
    std::printf("  truth (%g, %g, %g, %g, %g): err a %.2e b %.2e rho %.2e m %.2e s %.2e, rmse "
                "%.2e vol pts, %d iterations\n",
                p.a, p.b, p.rho, p.m, p.s, r.p.a - p.a, r.p.b - p.b, r.p.rho - p.rho,
                r.p.m - p.m, r.p.s - p.s, r.rmse_vol, r.iterations);
    EXPECT_NEAR(r.p.a, p.a, 1e-6);
    EXPECT_NEAR(r.p.b, p.b, 1e-6);
    EXPECT_NEAR(r.p.rho, p.rho, 1e-6);
    EXPECT_NEAR(r.p.m, p.m, 1e-6);
    EXPECT_NEAR(r.p.s, p.s, 1e-6);
    EXPECT_LE(r.rmse_vol, 1e-8);
    EXPECT_EQ(r.n_points, 41);
  }
}
TEST(SviFit, NoisySmileRmseMatchesNoise) {
  auto rng = opt::make_stream(20261017, 3);
  for (const auto& p : kSets) {
    Smile s = make_smile(p);
    for (double& w : s.w) {
      const double vol = std::sqrt(w / kTau) + 0.001 * opt::norm_inv_cdf(opt::uniform01(rng));
      w = vol * vol * kTau;
    }
    const auto r = opt::fit_svi(s.k, s.w, kTau);
    ASSERT_EQ(r.status, opt::SviStatus::Ok);
    std::printf("  noisy set (%g, %g, %g, %g, %g): rmse %.4f vol pts\n", p.a, p.b, p.rho, p.m,
                p.s, r.rmse_vol);
    EXPECT_GE(r.rmse_vol, 0.05);
    EXPECT_LE(r.rmse_vol, 0.2);
  }
}
// Brute-force minimum of the inner objective over a grid of the Zeliade
// polytope 0 <= c <= 4s, |d| <= c, |d| <= 4s - c, 0 <= a <= a_max.
double brute_force_inner(double m, double s, const Smile& data, double a_max) {
  constexpr int kN = 60;
  double best = std::numeric_limits<double>::infinity();
  for (int ia = 0; ia <= kN; ++ia) {
    const double a = a_max * ia / kN;
    for (int ic = 0; ic <= kN; ++ic) {
      const double c = 4.0 * s * ic / kN;
      const double dmax = std::min(c, 4.0 * s - c);
      for (int id = -kN; id <= kN; ++id) {
        const double d = dmax * id / kN;
        double obj = 0.0;
        for (std::size_t i = 0; i < data.k.size(); ++i) {
          const double y = (data.k[i] - m) / s;
          const double r = a + d * y + c * std::sqrt(y * y + 1.0) - data.w[i];
          obj += r * r;
        }
        best = std::min(best, obj);
      }
    }
  }
  return best;
}
TEST(SviFit, InnerProblemFeasibleOptimumWhenUnconstrainedIsInfeasible) {
  struct Case {
    const char* what;
    opt::SviParams data;  // generates infeasible targets for (m, s) of the data
  };
  // Negative a, and wing slope b(1 + |rho|) = 6 > 4 (c = b s > 4 s).
  const Case cases[] = {{"a < 0", {-0.05, 0.20, -0.20, 0.0, 0.30}},
                        {"c > 4s", {0.02, 6.0, 0.0, 0.0, 0.10}}};
  for (const auto& tc : cases) {
    const Smile s = make_smile(tc.data, 21);
    // The unconstrained solution for this (m, s) is the data itself: infeasible.
    const double c0 = tc.data.b * tc.data.s;
    const double d0 = tc.data.rho * c0;
    const double a_max = *std::max_element(s.w.begin(), s.w.end());
    ASSERT_FALSE(opt::detail::svi_inner_feasible(tc.data.a, c0, d0, tc.data.s, a_max, 0.0))
        << tc.what;
    const auto in = opt::detail::svi_inner_fit(tc.data.m, tc.data.s, s.k, s.w, {}, a_max);
    EXPECT_TRUE(opt::detail::svi_inner_feasible(in.a, in.c, in.d, tc.data.s, a_max, 1e-12))
        << tc.what;
    const double brute = brute_force_inner(tc.data.m, tc.data.s, s, a_max);
    std::printf("  %s: active-set objective %.6e, grid minimum %.6e\n", tc.what, in.objective,
                brute);
    EXPECT_LE(in.objective, brute * (1.0 + 1e-12)) << tc.what;
    EXPECT_GT(in.objective, 0.0) << tc.what;
  }
}
TEST(SviFit, HuberReducesOutlierInfluence) {
  const auto& p = kSets[0];
  Smile s = make_smile(p);
  constexpr std::size_t kOutlier = 30;
  const double vol = std::sqrt(s.w[kOutlier] / kTau) + 0.10;  // +10 vol points
  s.w[kOutlier] = vol * vol * kTau;
  opt::SviFitOptions plain;
  plain.irls_passes = 0;
  const auto r_plain = opt::fit_svi(s.k, s.w, kTau, plain);
  const auto r_huber = opt::fit_svi(s.k, s.w, kTau);
  ASSERT_NE(r_plain.status, opt::SviStatus::Failed);
  ASSERT_NE(r_huber.status, opt::SviStatus::Failed);
  const double e_plain = clean_rmse_volpts(r_plain.p, p, s, kOutlier);
  const double e_huber = clean_rmse_volpts(r_huber.p, p, s, kOutlier);
  std::printf("  error on the 40 clean points: plain LS %.4f vol pts, Huber %.4f vol pts\n",
              e_plain, e_huber);
  EXPECT_LT(e_huber, 0.5 * e_plain);
}
TEST(SviFit, TooFewPoints) {
  const Smile s = make_smile(kSets[0], 4);
  const auto r = opt::fit_svi(s.k, s.w, kTau);
  EXPECT_EQ(r.status, opt::SviStatus::TooFewPoints);
  EXPECT_EQ(r.n_points, 4);
}
TEST(SviFit, RejectsBadInput) {
  const std::vector<double> k = {0.0, 0.1};
  const std::vector<double> w = {0.04};
  EXPECT_THROW((void)opt::fit_svi(k, w, kTau), std::invalid_argument);
  const std::vector<double> w2 = {0.04, -0.01};
  EXPECT_THROW((void)opt::fit_svi(k, w2, kTau), std::invalid_argument);
  const std::vector<double> w3 = {0.04, 0.05};
  EXPECT_THROW((void)opt::fit_svi(k, w3, 0.0), std::invalid_argument);
}
}  // namespace
