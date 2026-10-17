// Hedge trigger (design 8.10 item 4).
#include "opt/mm/config.hpp"
#include "opt/mm/hedger.hpp"

#include <cmath>

#include <gtest/gtest.h>

#include "mm/mm_test_util.hpp"

namespace {

using opt::mm::HedgeConfig;
using opt::mm::Hedger;

TEST(Hedger, StrictThreshold) {
  const Hedger h{HedgeConfig{}};  // threshold 1 BTC, 5 bps
  constexpr double kSpot = 80'000.0;
  EXPECT_FALSE(h.maybe_hedge(1.0 - 1e-9, kSpot).has_value());
  EXPECT_FALSE(h.maybe_hedge(1.0, kSpot).has_value());  // exactly at threshold: no hedge
  EXPECT_FALSE(h.maybe_hedge(-1.0, kSpot).has_value());
  EXPECT_FALSE(h.maybe_hedge(0.0, kSpot).has_value());
  const auto up = h.maybe_hedge(1.0 + 1e-9, kSpot);
  ASSERT_TRUE(up.has_value());
  EXPECT_EQ(up->qty, -(1.0 + 1e-9));  // hedges to net delta 0
  EXPECT_EQ(up->price, kSpot);
  EXPECT_DOUBLE_EQ(up->cost, (1.0 + 1e-9) * kSpot * 5.0 / 1e4);
  const auto down = h.maybe_hedge(-2.5, kSpot);
  ASSERT_TRUE(down.has_value());
  EXPECT_EQ(down->qty, 2.5);
  EXPECT_DOUBLE_EQ(down->cost, 2.5 * kSpot * 5.0 / 1e4);
}

TEST(Hedger, ZeroThresholdHedgesAnyDelta) {
  HedgeConfig c;
  c.threshold = 0.0;
  c.cost_bps = 0.0;
  const Hedger h{c};
  EXPECT_FALSE(h.maybe_hedge(0.0, 100.0).has_value());
  const auto t = h.maybe_hedge(1e-12, 100.0);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->cost, 0.0);
}

// In the simulator every hedge happens only when the pre-hedge |net delta|
// exceeds the threshold, it brings net delta to ~0, and after every state
// |net delta| <= threshold.
TEST(Hedger, SimulatorHedgesExactlyAboveThreshold) {
  opt::mm::SimConfig c;
  c.steps = 5000;
  opt::mm::SyntheticMarket market(c);
  opt::mm::Simulator sim(c, opt::mm::synthetic_instruments(c));
  opt::mm::test::Recorder rec;
  rec.sim = &sim;
  rec.limits = c.limits;
  rec.threshold = c.hedge.threshold;
  sim.set_event_sink(&opt::mm::test::Recorder::on_event, &rec);
  int above_after = 0;
  while (sim.step(market) == opt::mm::SimStatus::Running) {
    above_after += std::abs(sim.risk().net_delta) > c.hedge.threshold ? 1 : 0;
  }
  EXPECT_EQ(above_after, 0);
  EXPECT_GT(rec.hedges, 0U);
  EXPECT_EQ(rec.hedge_failures, 0U);
  EXPECT_EQ(rec.hedges, sim.counters().hedges);
}

}  // namespace
