#include "opt/vol/arbitrage.hpp"
#include "opt/vol/surface.hpp"
#include "opt/vol/svi.hpp"
#include <gtest/gtest.h>
#include <cstdio>
#include <vector>
namespace {
// Axel Vogt's example: Gatheral and Jacquier, "Arbitrage-free SVI volatility
// surfaces" (arXiv:1204.0646v1, Example 4.1, Section 4; published in
// Quantitative Finance 14(1), 2014). Its density is negative.
constexpr opt::SviParams kVogt{-0.0410, 0.1331, 0.3060, 0.3586, 0.4153};
constexpr opt::SviParams kGood{0.04, 0.10, -0.30, 0.00, 0.10};
TEST(Arbitrage, ButterflyFlagsVogtExample) {
  const auto c = opt::butterfly_check(kVogt, 1.0, -1.5, 1.5, opt::kArbitrageGridPoints);
  std::printf("  Vogt: min g %.5f at k %.3f, %d of %d points negative\n", c.min_value,
              c.k_at_min, c.violations, opt::kArbitrageGridPoints);
  EXPECT_LT(c.min_value, 0.0);
  EXPECT_GT(c.violations, 0);
}
TEST(Arbitrage, ButterflyPassesKnownGoodSet) {
  const auto c = opt::butterfly_check(kGood, 1.0, -1.5, 1.5, opt::kArbitrageGridPoints);
  EXPECT_GT(c.min_value, 0.0);
  EXPECT_EQ(c.violations, 0);
}
TEST(Arbitrage, ButterflyCountsNonPositiveVariance) {
  const opt::SviParams neg{-0.5, 0.1, 0.0, 0.0, 0.1};  // w < 0 everywhere near m
  const auto c = opt::butterfly_check(neg, 1.0, -0.1, 0.1, 11);
  EXPECT_EQ(c.violations, 11);
}
opt::SviParams scaled(const opt::SviParams& p, double f) {
  // w is linear in (a, b): scaling both scales w.
  return {p.a * f, p.b * f, p.rho, p.m, p.s};
}
TEST(Arbitrage, CalendarFlagsDecreasingVariance) {
  const auto bad = opt::calendar_check(kGood, 0.5, scaled(kGood, 0.8), 1.0, -1.5, 1.5,
                                       opt::kArbitrageGridPoints);
  EXPECT_LT(bad.min_value, 0.0);
  EXPECT_EQ(bad.violations, opt::kArbitrageGridPoints);
  const auto ok = opt::calendar_check(kGood, 0.5, scaled(kGood, 1.2), 1.0, -1.5, 1.5,
                                      opt::kArbitrageGridPoints);
  EXPECT_GT(ok.min_value, 0.0);
  EXPECT_EQ(ok.violations, 0);
}
TEST(Arbitrage, NoexceptChecksEqualSurfaceReport) {
  const std::vector<opt::SurfaceSlice> slices = {
      {0.25, 100.0, kGood}, {0.5, 101.0, scaled(kGood, 0.8)}, {1.0, 102.0, kVogt}};
  const opt::VolSurface surface{slices};
  const std::vector<opt::SliceRange> ranges = {{-0.4, 0.3}, {-0.6, 0.5}, {-0.8, 0.7}};
  const auto rep = opt::check_surface(surface, ranges);
  ASSERT_EQ(rep.slices.size(), 3U);
  ASSERT_EQ(rep.calendar.size(), 2U);
  for (std::size_t i = 0; i < slices.size(); ++i) {
    const auto c = opt::butterfly_check(slices[i].params, slices[i].tau, ranges[i].k_min - 0.5,
                                        ranges[i].k_max + 0.5, opt::kArbitrageGridPoints);
    EXPECT_EQ(c.min_value, rep.slices[i].butterfly.min_value);
    EXPECT_EQ(c.k_at_min, rep.slices[i].butterfly.k_at_min);
    EXPECT_EQ(c.violations, rep.slices[i].butterfly.violations);
  }
  for (std::size_t i = 0; i + 1 < slices.size(); ++i) {
    const double lo = std::min(ranges[i].k_min, ranges[i + 1].k_min) - 0.5;
    const double hi = std::max(ranges[i].k_max, ranges[i + 1].k_max) + 0.5;
    const auto c = opt::calendar_check(slices[i].params, slices[i].tau, slices[i + 1].params,
                                       slices[i + 1].tau, lo, hi, opt::kArbitrageGridPoints);
    EXPECT_EQ(c.min_value, rep.calendar[i].check.min_value);
    EXPECT_EQ(c.k_at_min, rep.calendar[i].check.k_at_min);
    EXPECT_EQ(c.violations, rep.calendar[i].check.violations);
  }
  // Slice 1 is below slice 0 (calendar), slice 2 is Vogt (butterfly).
  EXPECT_EQ(rep.butterfly_slices_violating, 1);
  EXPECT_GE(rep.calendar_pairs_violating, 1);
  EXPECT_FALSE(rep.arbitrage_free());
  EXPECT_NEAR(rep.slices[0].right_slope, 0.07, 1e-15);
  EXPECT_NEAR(rep.slices[0].left_slope, 0.13, 1e-15);
  EXPECT_TRUE(rep.slices[0].lee_ok);
}
TEST(Arbitrage, LeeSlopeAboveTwoIsFlagged) {
  const std::vector<opt::SurfaceSlice> slices = {{1.0, 100.0, {0.01, 1.5, 0.5, 0.0, 0.1}}};
  const opt::VolSurface surface{slices};
  const std::vector<opt::SliceRange> ranges = {{-0.5, 0.5}};
  const auto rep = opt::check_surface(surface, ranges);
  EXPECT_FALSE(rep.slices[0].lee_ok);  // b (1 + rho) = 2.25
  EXPECT_EQ(rep.lee_slices_violating, 1);
  const std::vector<opt::SliceRange> wrong(2);
  EXPECT_THROW((void)opt::check_surface(surface, wrong), std::invalid_argument);
}
}  // namespace
