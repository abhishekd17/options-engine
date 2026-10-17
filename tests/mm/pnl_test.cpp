// PnL attribution (design 8.10 item 5).
#include "opt/mm/pnl.hpp"
#include "opt/mm/portfolio.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <gtest/gtest.h>

namespace {

using opt::mm::Marks;
using opt::mm::PnlAttribution;
using opt::mm::StepAttribution;

Marks make(std::array<double, 2> price, std::array<double, 2> delta, std::array<double, 2> gamma,
           std::array<double, 2> vega, std::array<double, 2> theta, std::array<double, 2> vol) {
  Marks m(2);
  for (std::size_t i = 0; i < 2; ++i) {
    m.price[i] = price[i];
    m.delta[i] = delta[i];
    m.gamma[i] = gamma[i];
    m.vega[i] = vega[i];
    m.theta[i] = theta[i];
    m.vol[i] = vol[i];
  }
  return m;
}

// Two options, two steps, positions and hedge change between the steps.
// Every expected value is computed by hand in the comments.
TEST(Pnl, HandcraftedTwoStepScenario) {
  constexpr double kTol = 1e-12;
  const Marks m0 = make({100, 50}, {0.5, -0.25}, {0.001, 0.002}, {200, 100}, {-36.5, -73},
                        {0.50, 0.60});
  const Marks m1 = make({106, 49}, {0.55, -0.2}, {0.0012, 0.0018}, {210, 95}, {-40, -70},
                        {0.51, 0.59});
  const Marks m2 = make({103, 50.5}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0.52, 0.585});

  // Step 1: q = (2, -3), h = 0.5, S 1000 -> 1010, dt = 1/365.
  const std::array<std::int64_t, 2> q1{2, -3};
  const StepAttribution s1 = opt::mm::attribute_step(q1, 0.5, 1000, 1010, 1.0 / 365, m0, m1);
  // delta = (2*0.5 + (-3)(-0.25) + 0.5) * 10 = 2.25 * 10
  EXPECT_NEAR(s1.delta, 22.5, kTol);
  // gamma = 0.5 (2*0.001 - 3*0.002) * 10^2 = 0.5 (-0.004) 100
  EXPECT_NEAR(s1.gamma, -0.2, kTol);
  // vega = 2*200*(+0.01) + (-3)*100*(-0.01) = 4 + 3
  EXPECT_NEAR(s1.vega, 7.0, kTol);
  // theta = (2*(-36.5) + (-3)(-73)) / 365 = 146 / 365
  EXPECT_NEAR(s1.theta, 0.4, kTol);
  // actual = 2*6 + (-3)(-1) + 0.5*10
  EXPECT_NEAR(s1.actual, 20.0, kTol);
  // residual = 20 - (22.5 - 0.2 + 7 + 0.4)
  EXPECT_NEAR(s1.residual, -9.7, kTol);

  // Step 2: q = (3, -3), h = -1, S 1010 -> 1005, dt = 2/365.
  const std::array<std::int64_t, 2> q2{3, -3};
  const StepAttribution s2 = opt::mm::attribute_step(q2, -1.0, 1010, 1005, 2.0 / 365, m1, m2);
  // delta = (3*0.55 + (-3)(-0.2) - 1) * (-5) = 1.25 * (-5)
  EXPECT_NEAR(s2.delta, -6.25, kTol);
  // gamma = 0.5 (3*0.0012 - 3*0.0018) * 25 = 0.5 (-0.0018) 25
  EXPECT_NEAR(s2.gamma, -0.0225, kTol);
  // vega = 3*210*0.01 + (-3)*95*(-0.005) = 6.3 + 1.425
  EXPECT_NEAR(s2.vega, 7.725, kTol);
  // theta = (3*(-40) + (-3)(-70)) * 2/365 = 180/365
  EXPECT_NEAR(s2.theta, 180.0 / 365.0, kTol);
  // actual = 3*(-3) + (-3)(1.5) + (-1)(-5) = -9 - 4.5 + 5
  EXPECT_NEAR(s2.actual, -8.5, kTol);
  EXPECT_NEAR(s2.residual, -8.5 - (-6.25 - 0.0225 + 7.725 + 180.0 / 365.0), kTol);

  PnlAttribution pnl;
  pnl.add(s1);
  pnl.add(s2);
  pnl.add_spread_capture(3.0);
  pnl.add_hedge_cost(0.75);
  const auto r = pnl.report();
  EXPECT_NEAR(r.delta, 16.25, kTol);
  EXPECT_NEAR(r.gamma, -0.2225, kTol);
  EXPECT_NEAR(r.vega, 14.725, kTol);
  EXPECT_NEAR(r.theta, 0.4 + 180.0 / 365.0, kTol);
  EXPECT_NEAR(r.spread_capture, 3.0, kTol);
  EXPECT_NEAR(r.hedge_cost, 0.75, kTol);
  // total = sum of actual moves + spread capture - hedge cost = 20 - 8.5 + 3 - 0.75
  EXPECT_NEAR(r.total, 13.75, kTol);
  EXPECT_NEAR(r.delta + r.gamma + r.vega + r.theta + r.residual + r.spread_capture - r.hedge_cost,
              r.total, kTol);
}

// Ledger: buying 2 at 98 when fair is 100 and selling hedge 1 BTC at 1000
// with a 0.5 cost leaves cash -196 + 1000 - 0.5; marked at fair the ledger is
// the spread capture minus the cost.
TEST(Pnl, LedgerMatchesSpreadCaptureAtFill) {
  opt::mm::Portfolio p(2);
  p.apply_fill(0, 2, 98.0);
  p.apply_hedge(-1.0, 1000.0, 0.5);
  const std::array<double, 2> price{100.0, 0.0};
  EXPECT_DOUBLE_EQ(p.cash(), -196.0 + 1000.0 - 0.5);
  EXPECT_DOUBLE_EQ(p.ledger_value(price, 1000.0), 2 * (100.0 - 98.0) - 0.5);
  EXPECT_DOUBLE_EQ(p.sum_abs_cash_flows(), 196.0 + 1000.0 + 0.5);
}

// Pure delta move: sigma fixed, dt = 0, small dS. The vega and theta buckets
// are exactly 0 and the residual is the third-order term, so it scales like
// dS^3 (ratio 8 when dS doubles).
TEST(Pnl, PureDeltaMove) {
  const opt::OptionSpec base{opt::OptionType::Call, 80'000.0, 82'000.0, 30.0 / 365.0, 0.0, 0.0,
                             0.5};
  auto marks_at = [&](double s) {
    opt::OptionSpec spec = base;
    spec.spot = s;
    const auto g = opt::bsm_greeks(spec);
    Marks m(1);
    m.price[0] = g.price;
    m.delta[0] = g.delta;
    m.gamma[0] = g.gamma;
    m.vega[0] = g.vega;
    m.theta[0] = g.theta;
    m.vol[0] = spec.vol;
    return m;
  };
  const std::array<std::int64_t, 1> q{3};
  const Marks m0 = marks_at(80'000.0);
  double prev = 0.0;
  for (const double ds : {10.0, 20.0, 40.0}) {
    const StepAttribution s =
        opt::mm::attribute_step(q, 0.0, 80'000.0, 80'000.0 + ds, 0.0, m0, marks_at(80'000.0 + ds));
    EXPECT_EQ(s.vega, 0.0);
    EXPECT_EQ(s.theta, 0.0);
    EXPECT_LT(std::abs(s.residual), 1e-6 * std::abs(s.delta)) << "dS " << ds;
    std::printf("pure delta move dS %.0f: delta %.6f gamma %.9f residual %.3e\n", ds, s.delta,
                s.gamma, s.residual);
    if (prev != 0.0) {
      const double ratio = s.residual / prev;
      EXPECT_GT(ratio, 7.0) << "dS " << ds;
      EXPECT_LT(ratio, 9.0) << "dS " << ds;
    }
    prev = s.residual;
  }
}

TEST(Pnl, IdentityTolerance) {
  EXPECT_DOUBLE_EQ(opt::mm::identity_tolerance(0.0), 1e-6);
  EXPECT_DOUBLE_EQ(opt::mm::identity_tolerance(1e9), 1e-6 + 1e-3);
}

}  // namespace
