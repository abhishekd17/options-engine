// Synthetic generator (design 8.10 item 8 and the 7.9 check deferred from
// Part 3: validate_order() stays true after 10,000 generator steps).
#include "opt/mm/config.hpp"
#include "opt/mm/market.hpp"
#include "opt/mm/synthetic.hpp"
#include "opt/vol/arbitrage.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

using opt::mm::MarketState;
using opt::mm::SimConfig;
using opt::mm::SyntheticMarket;

TEST(Synthetic, DefaultSmileHasNoStaticArbitrage) {
  SimConfig c;
  c.steps = 10'000;
  SyntheticMarket m(c);
  MarketState s;
  int checked = 0;
  while (m.next(s)) {
    EXPECT_TRUE(s.arbitrage_ok);
    if (m.step_index() % 500 == 0) {
      EXPECT_TRUE(opt::mm::surface_arbitrage_free(m.surface())) << "step " << m.step_index();
      ++checked;
    }
  }
  EXPECT_EQ(checked, 21);
  EXPECT_EQ(m.step_index(), 10'000U);
  EXPECT_TRUE(m.surface().validate_order());  // the deferred 7.9 check
}

TEST(Synthetic, SameSeedSamePath) {
  SimConfig c;
  c.steps = 3000;
  SyntheticMarket a(c);
  SyntheticMarket b(c);
  c.seed = 43;
  SyntheticMarket other(c);
  MarketState sa;
  MarketState sb;
  MarketState so;
  int differ = 0;
  while (a.next(sa)) {
    ASSERT_TRUE(b.next(sb));
    ASSERT_TRUE(other.next(so));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(sa.spot), std::bit_cast<std::uint64_t>(sb.spot));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a.atm_vol()), std::bit_cast<std::uint64_t>(b.atm_vol()));
    differ += sa.spot != so.spot ? 1 : 0;
  }
  EXPECT_FALSE(b.next(sb));
  EXPECT_EQ(differ, 3000);  // every state after t = 0 differs for another seed
}

TEST(Synthetic, AtmTotalVarianceIsVolSquaredTau) {
  SimConfig c;
  c.steps = 2000;
  SyntheticMarket m(c);
  MarketState s;
  while (m.next(s)) {
    const double v = m.atm_vol();
    for (std::size_t i = 0; i < m.surface().size(); ++i) {
      const auto& sl = m.surface().slice(i);
      const double expect = v * v * sl.tau;
      EXPECT_NEAR(m.surface().total_variance(0.0, sl.tau), expect, 1e-14 * expect);
      EXPECT_EQ(sl.forward, s.spot);
      EXPECT_EQ(sl.tau, m.expiries()[i] - s.t_years);
    }
  }
}

TEST(Synthetic, StateSequenceAndInstruments) {
  SimConfig c;
  c.steps = 3;
  SyntheticMarket m(c);
  MarketState s;
  ASSERT_TRUE(m.next(s));
  EXPECT_EQ(s.t_years, 0.0);
  EXPECT_EQ(s.spot, 80'000.0);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(m.next(s));
  }
  EXPECT_EQ(s.t_years, 3.0 / opt::mm::kSecondsPerYear);
  EXPECT_FALSE(m.next(s));
  const auto inst = opt::mm::synthetic_instruments(c);
  ASSERT_EQ(inst.size(), 10U);
  // F_0 e^k rounded to 1,000 USD: 65,000 72,000 80,000 88,000 98,000.
  EXPECT_EQ(inst[0].strike, 65'000.0);
  EXPECT_EQ(inst[1].strike, 72'000.0);
  EXPECT_EQ(inst[2].strike, 80'000.0);
  EXPECT_EQ(inst[3].strike, 88'000.0);
  EXPECT_EQ(inst[4].strike, 98'000.0);
  EXPECT_EQ(inst[0].type, opt::OptionType::Put);
  EXPECT_EQ(inst[2].type, opt::OptionType::Call);
  EXPECT_EQ(inst[5].expiry, 30.0 / 365.0);
}

TEST(Synthetic, ConfigValidation) {
  SimConfig c;
  EXPECT_EQ(opt::mm::validate(c), "");
  c.steps = 7 * 86'400;  // horizon == first expiry
  EXPECT_NE(opt::mm::validate(c).find("horizon"), std::string::npos);
  c = SimConfig{};
  c.synthetic.expiry_days = {30.0, 7.0};
  EXPECT_NE(opt::mm::validate(c), "");
  EXPECT_THROW(SyntheticMarket{c}, std::invalid_argument);
  c = SimConfig{};
  c.synthetic.svi_beta = 20.0;  // wings far too steep: butterfly arbitrage
  EXPECT_NE(opt::mm::validate(c).find("arbitrage"), std::string::npos);
  EXPECT_THROW(SyntheticMarket{c}, std::invalid_argument);
  c = SimConfig{};
  c.quoter.size = 0;
  EXPECT_NE(opt::mm::validate(c), "");
}

// A vol path that drives the smile out of the arbitrage-free region stops
// the run at a guard step (every 1,000 steps).
TEST(Synthetic, InRunGuardReportsViolation) {
  // ATM vol decays fast towards 1e-4 while the wing slope stays b = beta v
  // sqrt(tau); once v sqrt(tau) is small enough the SVI minimum total
  // variance a + b s sqrt(1 - rho^2) turns negative.
  SimConfig c;
  c.steps = 5000;
  c.dt_sec = 60.0;
  c.synthetic.vol_mean = 1e-4;
  c.synthetic.vol_kappa = 2000.0;
  c.synthetic.vol_xi = 0.0;
  ASSERT_EQ(opt::mm::validate(c), "");
  SyntheticMarket m(c);
  MarketState s;
  bool violated = false;
  while (m.next(s)) {
    if (!s.arbitrage_ok) {
      violated = true;
      EXPECT_EQ(m.step_index() % opt::mm::kGuardEvery, 0U);
      break;
    }
  }
  EXPECT_TRUE(violated);
}

}  // namespace
