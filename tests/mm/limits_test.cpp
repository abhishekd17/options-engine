// Limits respected (design 8.10 item 3): after every fill |q_i| <= q_max,
// |vega| <= limit, |gamma_1pct| <= limit and pre-hedge |net delta| <= limit,
// on the golden run and on a 20,000-step run with tight limits.
#include "opt/mm/config.hpp"
#include "opt/mm/sim.hpp"

#include <cstdio>

#include <gtest/gtest.h>

#include "mm/mm_test_util.hpp"

namespace {

using opt::mm::SimConfig;
using opt::mm::SimStatus;

TEST(Limits, GoldenRunRespectsLimits) {
  SimConfig c;
  c.steps = 2000;
  const auto run = opt::mm::test::run_synthetic(c);
  ASSERT_EQ(run.status, SimStatus::Finished);
  EXPECT_GT(run.rec->fills, 0U);
  EXPECT_EQ(run.rec->limit_failures, 0U);
}

TEST(Limits, TightLimitsRespectedOver20000Steps) {
  SimConfig c;
  c.steps = 20'000;
  c.limits.max_position = 2;
  c.limits.max_net_delta = 1.5;
  c.limits.max_vega = 100.0;
  c.limits.max_gamma_1pct = 0.1;
  const auto run = opt::mm::test::run_synthetic(c);
  ASSERT_EQ(run.status, SimStatus::Finished);
  const auto& r = *run.rec;
  std::printf("tight limits: fills %llu  blocked sides %llu  rejected fills %llu  max |q| %.0f  "
              "max |vega| %.3f  max |gamma_1pct| %.5f  max |delta| %.4f\n",
              static_cast<unsigned long long>(r.fills),
              static_cast<unsigned long long>(run.sim->counters().limit_blocked_sides),
              static_cast<unsigned long long>(run.sim->counters().limit_rejected_fills),
              r.max_abs_position, r.max_abs_vega, r.max_abs_gamma, r.max_abs_delta);
  EXPECT_GT(r.fills, 0U);
  EXPECT_GT(run.sim->counters().limit_blocked_sides, 0U);  // the limits bind
  EXPECT_EQ(r.limit_failures, 0U);
  EXPECT_LE(r.max_abs_position, 2.0);
  EXPECT_EQ(r.hedge_failures, 0U);
  EXPECT_TRUE(run.sim->identity_holds());
}

}  // namespace
