// Quoter (design 8.10 item 2 and the pre-trade side of item 3).
#include "opt/mm/config.hpp"
#include "opt/mm/portfolio.hpp"
#include "opt/mm/quoter.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

using opt::mm::InstrumentView;
using opt::mm::LimitConfig;
using opt::mm::PortfolioRisk;
using opt::mm::Quote;
using opt::mm::QuoterConfig;

InstrumentView atm_call(std::int64_t position) {
  const opt::OptionSpec spec{opt::OptionType::Call, 80'000.0, 80'000.0, 30.0 / 365.0, 0.0, 0.0,
                             0.5};
  const auto g = opt::bsm_greeks(spec);
  InstrumentView v;
  v.type = spec.type;
  v.forward = spec.spot;
  v.strike = spec.strike;
  v.tau = spec.time;
  v.fair_vol = spec.vol;
  v.delta = g.delta;
  v.gamma = g.gamma;
  v.vega = g.vega;
  v.position = position;
  return v;
}

PortfolioRisk flat_risk() {
  PortfolioRisk r;
  r.spot = 80'000.0;
  return r;
}

Quote quote_one(const InstrumentView& v, const PortfolioRisk& risk, std::uint64_t* blocked = nullptr,
                const LimitConfig& lim = {}) {
  std::array<Quote, 1> out{};
  const std::array<InstrumentView, 1> in{v};
  const auto b = opt::mm::make_quotes(QuoterConfig{}, lim, in, risk, out);
  if (blocked != nullptr) {
    *blocked = b;
  }
  return out[0];
}

TEST(Quoter, FlatBookQuotesSymmetricAroundFair) {
  const Quote q = quote_one(atm_call(0), flat_risk());
  EXPECT_DOUBLE_EQ(q.bid_vol, 0.5 - 0.01);
  EXPECT_DOUBLE_EQ(q.ask_vol, 0.5 + 0.01);
  EXPECT_TRUE(q.bid_active);
  EXPECT_TRUE(q.ask_active);
  const double fair = opt::bsm_price({opt::OptionType::Call, 80'000.0, 80'000.0, 30.0 / 365.0,
                                      0.0, 0.0, 0.5});
  EXPECT_LT(q.bid_price, fair);
  EXPECT_GT(q.ask_price, fair);
}

TEST(Quoter, QuotesWidenWithInventory) {
  const Quote q0 = quote_one(atm_call(0), flat_risk());
  const Quote q5 = quote_one(atm_call(5), flat_risk());
  EXPECT_GT(q5.ask_vol - q5.bid_vol, q0.ask_vol - q0.bid_vol);
  EXPECT_GT(q5.ask_price - q5.bid_price, q0.ask_price - q0.bid_price);
  // 0.5 utilisation: half spread +0.5 vol pt, center -0.25 vol pt.
  EXPECT_NEAR((q5.ask_vol - q5.bid_vol) - (q0.ask_vol - q0.bid_vol), 0.01, 1e-15);
  const Quote qm5 = quote_one(atm_call(-5), flat_risk());
  const double c0 = 0.5 * (q0.bid_vol + q0.ask_vol);
  EXPECT_LT(0.5 * (q5.bid_vol + q5.ask_vol), c0);   // long: lower
  EXPECT_GT(0.5 * (qm5.bid_vol + qm5.ask_vol), c0);  // short: higher
}

TEST(Quoter, MonotoneOverPositions) {
  double prev_center = 1e9;
  for (std::int64_t q = -10; q <= 10; ++q) {
    const Quote cur = quote_one(atm_call(q), flat_risk());
    const Quote mirror = quote_one(atm_call(-q), flat_risk());
    const double center = 0.5 * (cur.bid_vol + cur.ask_vol);
    EXPECT_LT(center, prev_center) << "q " << q;
    prev_center = center;
    // Width depends on |q| only and grows with it.
    EXPECT_NEAR(cur.ask_vol - cur.bid_vol, mirror.ask_vol - mirror.bid_vol, 1e-15);
    if (q > 0) {
      const Quote smaller = quote_one(atm_call(q - 1), flat_risk());
      EXPECT_GT(cur.ask_vol - cur.bid_vol, smaller.ask_vol - smaller.bid_vol) << "q " << q;
    }
  }
}

TEST(Quoter, WidensWithVegaAndGammaExposure) {
  const Quote q0 = quote_one(atm_call(0), flat_risk());
  PortfolioRisk long_vega = flat_risk();
  long_vega.vega_volpt = 1000.0;  // half the limit
  const Quote qv = quote_one(atm_call(0), long_vega);
  EXPECT_GT(qv.ask_vol - qv.bid_vol, q0.ask_vol - q0.bid_vol);
  EXPECT_LT(qv.bid_vol + qv.ask_vol, q0.bid_vol + q0.ask_vol);  // long vega: lower
  PortfolioRisk gamma = flat_risk();
  gamma.gamma_1pct = -0.5;
  const Quote qg = quote_one(atm_call(0), gamma);
  EXPECT_NEAR((qg.ask_vol - qg.bid_vol) - (q0.ask_vol - q0.bid_vol), 2 * 0.5 * 0.5 / 100, 1e-15);
}

TEST(Quoter, SideThatWouldBreachIsInactive) {
  std::uint64_t blocked = 0;
  // Position limit: at +10 a buy breaches, a sell does not.
  Quote q = quote_one(atm_call(10), flat_risk(), &blocked);
  EXPECT_FALSE(q.bid_active);
  EXPECT_TRUE(q.ask_active);
  EXPECT_EQ(blocked, 1U);
  q = quote_one(atm_call(-10), flat_risk(), &blocked);
  EXPECT_TRUE(q.bid_active);
  EXPECT_FALSE(q.ask_active);
  // Pre-hedge net delta: +4.8 BTC and a call delta of about 0.5.
  PortfolioRisk r = flat_risk();
  r.net_delta = 4.8;
  q = quote_one(atm_call(0), r, &blocked);
  EXPECT_FALSE(q.bid_active);
  EXPECT_TRUE(q.ask_active);
  // Vega: the option's vega per vol point is about 92 USD.
  r = flat_risk();
  r.vega_volpt = -1950.0;
  q = quote_one(atm_call(0), r, &blocked);
  EXPECT_TRUE(q.bid_active);
  EXPECT_FALSE(q.ask_active);
  // Gamma: about 0.028 BTC per contract per 1% move.
  r = flat_risk();
  r.gamma_1pct = 0.99;
  q = quote_one(atm_call(0), r, &blocked);
  EXPECT_FALSE(q.bid_active);
  EXPECT_TRUE(q.ask_active);
  EXPECT_EQ(blocked, 1U);
}

TEST(Quoter, NotQuotableAndFloor) {
  InstrumentView v = atm_call(0);
  v.quotable = false;
  std::uint64_t blocked = 7;
  const Quote q = quote_one(v, flat_risk(), &blocked);
  EXPECT_FALSE(q.bid_active);
  EXPECT_FALSE(q.ask_active);
  EXPECT_EQ(blocked, 0U);
  InstrumentView low = atm_call(0);
  low.fair_vol = 0.012;
  EXPECT_DOUBLE_EQ(quote_one(low, flat_risk()).bid_vol, 0.01);
}

TEST(Quoter, SizeMismatchThrows) {
  std::array<Quote, 2> out{};
  const std::array<InstrumentView, 1> in{atm_call(0)};
  EXPECT_THROW((void)opt::mm::make_quotes(QuoterConfig{}, LimitConfig{}, in, flat_risk(), out),
               std::invalid_argument);
}

}  // namespace
