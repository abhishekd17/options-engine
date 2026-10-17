#include "opt/vol/svi.hpp"
#include <gtest/gtest.h>
#include <cmath>
namespace {
TEST(Svi, DerivativesMatchFiniteDifferences) {
  const opt::SviParams sets[] = {{0.04, 0.10, -0.30, 0.00, 0.10},
                                 {0.02, 0.20, -0.60, 0.10, 0.20},
                                 {-0.0410, 0.1331, 0.3060, 0.3586, 0.4153}};
  for (const auto& p : sets) {
    for (double k = -1.5; k <= 1.5; k += 0.05) {
      // Central differences: truncation h^2 w'''/6, rounding eps w / h.
      const double h1 = 1e-6;
      const double fd1 = (opt::svi_w(p, k + h1) - opt::svi_w(p, k - h1)) / (2.0 * h1);
      const double h = 1e-4;
      const double fd2 =
          (opt::svi_w(p, k + h) - 2.0 * opt::svi_w(p, k) + opt::svi_w(p, k - h)) / (h * h);
      EXPECT_NEAR(opt::svi_dw(p, k), fd1, 1e-8) << "k=" << k;
      EXPECT_NEAR(opt::svi_d2w(p, k), fd2, 1e-5 * std::max(1.0, std::abs(fd2))) << "k=" << k;
    }
  }
}
TEST(Svi, MinimumAndValidity) {
  const opt::SviParams p{0.04, 0.10, -0.30, 0.05, 0.10};
  // The minimum is at k* = m - rho s / sqrt(1 - rho^2).
  const double k_star = p.m - p.rho * p.s / std::sqrt(1.0 - p.rho * p.rho);
  EXPECT_NEAR(opt::svi_w(p, k_star), opt::svi_min_w(p), 1e-15);
  EXPECT_TRUE(opt::svi_valid(p));
  EXPECT_FALSE(opt::svi_valid({0.04, -0.1, 0.0, 0.0, 0.1}));
  EXPECT_FALSE(opt::svi_valid({0.04, 0.1, 1.0, 0.0, 0.1}));
  EXPECT_FALSE(opt::svi_valid({0.04, 0.1, 0.0, 0.0, 0.0}));
  EXPECT_FALSE(opt::svi_valid({-0.5, 0.1, 0.0, 0.0, 0.1}));  // negative minimum variance
}
}  // namespace
