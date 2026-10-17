// Fill model (design 8.10 item 6).
#include "opt/mm/config.hpp"
#include "opt/mm/fill_model.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include <gtest/gtest.h>

namespace {

using opt::mm::FillConfig;
using opt::mm::FillModel;

TEST(FillModel, ProbabilityDecreasesWithDistance) {
  const FillModel m{FillConfig{}, 1};
  double prev = 1.0;
  for (int i = 0; i <= 40; ++i) {
    const double p = m.probability(0.25 * i, 1.0);
    EXPECT_LT(p, prev) << "distance " << 0.25 * i;
    EXPECT_GT(p, 0.0);
    prev = p;
  }
  // Zero distance: 1 - exp(-A dt) with A = 1/120 per second.
  EXPECT_NEAR(m.probability(0.0, 1.0), -std::expm1(-1.0 / 120.0), 1e-15);
  EXPECT_NEAR(m.probability(1.0, 10.0), -std::expm1(-10.0 / 120.0 * std::exp(-1.0)), 1e-15);
  EXPECT_EQ(m.probability(3.0, 0.0), 0.0);
}

TEST(FillModel, EmpiricalRateWithinFiveStandardErrors) {
  constexpr int kDraws = 1'000'000;
  struct Case {
    double distance, dt;
  };
  std::uint64_t seed = 1234;
  for (const Case cs : {Case{0.0, 1.0}, Case{1.0, 1.0}, Case{0.5, 30.0}, Case{2.0, 300.0}}) {
    FillModel m{FillConfig{}, seed++};  // a separate stream per case
    int hits = 0;
    for (int i = 0; i < kDraws; ++i) {
      hits += m.draw(cs.distance, cs.dt) ? 1 : 0;
    }
    const double p = -std::expm1(-(1.0 / 120.0) * std::exp(-cs.distance) * cs.dt);
    const double rate = static_cast<double>(hits) / kDraws;
    const double se = std::sqrt(p * (1.0 - p) / kDraws);
    std::printf("fill rate d=%.1f dt=%.0f: empirical %.6f expected %.6f (%.2f SE)\n",
                cs.distance, cs.dt, rate, p, (rate - p) / se);
    EXPECT_LE(std::abs(rate - p), 5.0 * se);
  }
}

}  // namespace
