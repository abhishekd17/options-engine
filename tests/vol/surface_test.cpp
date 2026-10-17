#include "opt/vol/surface.hpp"
#include "opt/vol/svi.hpp"
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace {
constexpr opt::SviParams kP1{0.01, 0.10, -0.30, 0.00, 0.10};
constexpr opt::SviParams kP2{0.02, 0.15, -0.40, 0.02, 0.15};
constexpr opt::SviParams kP3{0.04, 0.20, -0.50, 0.05, 0.20};
opt::VolSurface make_surface() {
  return opt::VolSurface{{{0.1, 100.0, kP1}, {0.5, 101.0, kP2}, {1.0, 103.0, kP3}}};
}
TEST(VolSurface, ExactAtSlices) {
  const auto s = make_surface();
  for (double k = -1.0; k <= 1.0; k += 0.1) {
    EXPECT_EQ(s.total_variance(k, 0.1), opt::svi_w(kP1, k));
    EXPECT_EQ(s.total_variance(k, 0.5), opt::svi_w(kP2, k));
    EXPECT_EQ(s.total_variance(k, 1.0), opt::svi_w(kP3, k));
  }
  EXPECT_EQ(s.forward(0.5), 101.0);
}
TEST(VolSurface, LinearInTauBetweenSlicesAndMonotone) {
  const auto s = make_surface();
  for (double k = -1.0; k <= 1.0; k += 0.1) {
    const double w1 = opt::svi_w(kP1, k);
    const double w2 = opt::svi_w(kP2, k);
    EXPECT_NEAR(s.total_variance(k, 0.2), w1 + (w2 - w1) * 0.25, 1e-15);
    double prev = 0.0;
    for (double t = 0.02; t <= 1.5; t += 0.02) {
      const double w = s.total_variance(k, t);
      EXPECT_GE(w, prev) << "k=" << k << " t=" << t;
      prev = w;
    }
  }
}
TEST(VolSurface, ExtrapolatesWithConstantVol) {
  const auto s = make_surface();
  EXPECT_NEAR(s.total_variance(0.1, 0.05), opt::svi_w(kP1, 0.1) * 0.5, 1e-16);
  EXPECT_NEAR(s.total_variance(0.1, 2.0), opt::svi_w(kP3, 0.1) * 2.0, 1e-15);
  EXPECT_EQ(s.forward(0.01), 100.0);
  EXPECT_EQ(s.forward(5.0), 103.0);
}
TEST(VolSurface, ForwardLogLinearAndVol) {
  const auto s = make_surface();
  // ln F linear: the midpoint in tau gives the geometric mean.
  EXPECT_NEAR(s.forward(0.3), std::sqrt(100.0 * 101.0), 1e-12);
  const double tau = 0.3;
  const double strike = 95.0;
  const double k = std::log(strike / s.forward(tau));
  EXPECT_NEAR(s.vol(strike, tau), std::sqrt(s.total_variance(k, tau) / tau), 1e-12);
}
std::uint64_t bits(double x) { return std::bit_cast<std::uint64_t>(x); }
TEST(VolSurface, UpdateSliceMatchesFreshSurfaceBitwise) {
  auto s = make_surface();
  const opt::SviParams p{0.025, 0.17, -0.35, 0.01, 0.12};
  s.update_slice(1, 0.6, 101.5, p);
  const opt::VolSurface fresh{{{0.1, 100.0, kP1}, {0.6, 101.5, p}, {1.0, 103.0, kP3}}};
  EXPECT_EQ(s.slice(1).tau, 0.6);
  EXPECT_EQ(s.slice(1).forward, 101.5);
  EXPECT_TRUE(s.validate_order());
  for (const double tau : {0.05, 0.1, 0.3, 0.6, 0.8, 1.0, 2.0}) {
    EXPECT_EQ(bits(s.forward(tau)), bits(fresh.forward(tau))) << tau;
    for (const double strike : {70.0, 95.0, 101.0, 130.0}) {
      EXPECT_EQ(bits(s.vol(strike, tau)), bits(fresh.vol(strike, tau))) << tau << " " << strike;
    }
  }
}
TEST(VolSurface, ConstructorValidates) {
  EXPECT_THROW(opt::VolSurface{std::vector<opt::SurfaceSlice>{}}, std::invalid_argument);
  EXPECT_THROW((opt::VolSurface{{{0.5, 100.0, kP1}, {0.1, 100.0, kP2}}}), std::invalid_argument);
  EXPECT_THROW((opt::VolSurface{{{0.5, 100.0, kP1}, {0.5, 100.0, kP2}}}), std::invalid_argument);
  EXPECT_THROW((opt::VolSurface{{{0.5, -1.0, kP1}}}), std::invalid_argument);
  EXPECT_THROW((opt::VolSurface{{{0.0, 100.0, kP1}}}), std::invalid_argument);
  EXPECT_TRUE(make_surface().validate_order());
}
// The "validate_order() true after 10,000 synthetic-generator steps" check of
// design 7.9 needs the Part 4 generator and is added with it.
}  // namespace
