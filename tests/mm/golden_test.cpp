// Golden synthetic run (design 8.10 item 1): seed 42, 2,000 steps of 1 s,
// default config. Integers are compared exactly, PnL buckets and the final
// spot to 1e-9 relative. The path uses only IEEE-correctly-rounded
// operations (fast_exp, fast_log, fast_norm_inv, sqrt), so the values must
// match on every compiler and optimisation level; a mismatch is a
// determinism bug, not a reason to change the numbers.
//
// Captured with:
//   OPTENG_PRINT_GOLDEN=1 ./build/release/tests/opt_mm_tests --gtest_filter='Golden*'
// on macOS 26.7.1 arm64 (Apple M5 Pro), Apple clang 21.0.0, 2026-10-17, and
// confirmed unchanged under the debug, asan and tsan presets.
#include "opt/mm/config.hpp"
#include "opt/mm/sim.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <gtest/gtest.h>

#include "mm/mm_test_util.hpp"

namespace {

struct Golden {
  std::uint64_t bid_fills, ask_fills, hedges, blocked, rejected;
  std::array<std::int64_t, 10> positions;
  std::uint64_t fill_hash;
  double delta, gamma, vega, theta, residual, spread, hedge_cost, total, final_spot;
};

// Fields: bid fills, ask fills, hedges, limit-blocked sides, limit-rejected
// fills, final positions, fill hash, then the PnL buckets (delta, gamma,
// vega, theta, residual, spread capture, hedge cost, total) and final spot.
constexpr Golden kGolden{52, 48, 6, 0, 0,
  {4, 1, -2, -2, 4, -2, -4, 2, 2, 1},
  0xc351c1a7d762bfe0ULL,
  -110.26842489369244, -10.106817346277898, -71.283404331781696, 10.792731397085117,
  0.15990465913792945, 5168.3927195975057, 298.91821105026526, 4688.7684980317117, 80011.818243312693};

void expect_rel(double actual, double expected, const char* what) {
  EXPECT_LE(std::abs(actual - expected), 1e-9 * std::abs(expected) + 1e-12)
      << what << ": actual " << actual << " expected " << expected;
}

TEST(Golden, SyntheticSeed42) {
  opt::mm::SimConfig c;
  c.seed = 42;
  c.steps = 2000;
  c.dt_sec = 1.0;
  const auto run = opt::mm::test::run_synthetic(c);
  ASSERT_EQ(run.status, opt::mm::SimStatus::Finished);
  const auto& sim = *run.sim;
  const auto& k = sim.counters();
  const auto p = sim.pnl();
  const auto pos = sim.portfolio().positions();
  ASSERT_EQ(pos.size(), 10U);
  EXPECT_EQ(k.states, 2001U);
  EXPECT_TRUE(sim.identity_holds()) << sim.identity_error() << " > " << sim.identity_tol();
  EXPECT_EQ(run.rec->limit_failures, 0U);

  if (std::getenv("OPTENG_PRINT_GOLDEN") != nullptr) {
    std::printf("constexpr Golden kGolden{%llu, %llu, %llu, %llu, %llu,\n  {",
                static_cast<unsigned long long>(k.bid_fills),
                static_cast<unsigned long long>(k.ask_fills),
                static_cast<unsigned long long>(k.hedges),
                static_cast<unsigned long long>(k.limit_blocked_sides),
                static_cast<unsigned long long>(k.limit_rejected_fills));
    for (std::size_t i = 0; i < pos.size(); ++i) {
      std::printf("%s%lld", i == 0 ? "" : ", ", static_cast<long long>(pos[i]));
    }
    std::printf("},\n  0x%016llxULL,\n  %.17g, %.17g, %.17g, %.17g,\n  %.17g, %.17g, %.17g, "
                "%.17g, %.17g};\n",
                static_cast<unsigned long long>(run.rec->hash.value()), p.delta, p.gamma, p.vega,
                p.theta, p.residual, p.spread_capture, p.hedge_cost, p.total, sim.spot());
  }

  EXPECT_EQ(k.bid_fills, kGolden.bid_fills);
  EXPECT_EQ(k.ask_fills, kGolden.ask_fills);
  EXPECT_EQ(k.hedges, kGolden.hedges);
  EXPECT_EQ(k.limit_blocked_sides, kGolden.blocked);
  EXPECT_EQ(k.limit_rejected_fills, kGolden.rejected);
  for (std::size_t i = 0; i < pos.size(); ++i) {
    EXPECT_EQ(pos[i], kGolden.positions[i]) << "instrument " << i;
  }
  EXPECT_EQ(run.rec->hash.value(), kGolden.fill_hash);
  expect_rel(p.delta, kGolden.delta, "delta");
  expect_rel(p.gamma, kGolden.gamma, "gamma");
  expect_rel(p.vega, kGolden.vega, "vega");
  expect_rel(p.theta, kGolden.theta, "theta");
  expect_rel(p.residual, kGolden.residual, "residual");
  expect_rel(p.spread_capture, kGolden.spread, "spread capture");
  expect_rel(p.hedge_cost, kGolden.hedge_cost, "hedge cost");
  expect_rel(p.total, kGolden.total, "total");
  expect_rel(sim.spot(), kGolden.final_spot, "final spot");
}

// Attribution sums to the ledger PnL on the golden run (also checked above)
// and on a full default day.
TEST(Golden, AttributionIdentityFullDay) {
  opt::mm::SimConfig c;  // 86,400 steps of 1 s
  const auto run = opt::mm::test::run_synthetic(c);
  ASSERT_EQ(run.status, opt::mm::SimStatus::Finished);
  const auto p = run.sim->pnl();
  const double buckets =
      p.delta + p.gamma + p.vega + p.theta + p.residual + p.spread_capture - p.hedge_cost;
  std::printf("full day: total %.6f ledger %.6f diff %.3e tol %.3e\n", p.total,
              run.sim->ledger_pnl(), run.sim->identity_error(), run.sim->identity_tol());
  EXPECT_NEAR(buckets, p.total, 1e-9 * std::abs(p.total));
  EXPECT_TRUE(run.sim->identity_holds());
  EXPECT_EQ(run.rec->limit_failures, 0U);
}

}  // namespace
