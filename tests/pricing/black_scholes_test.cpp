#include "opt/pricing/black_scholes.hpp"

#include "opt/core/rng.hpp"
#include "opt/pricing/types.hpp"
#include "test_util.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace {

using opt::Greeks;
using opt::OptionSpec;
using opt::OptionType;

OptionSpec spec(OptionType type, double s, double k, double t, double r, double q, double vol) {
  return OptionSpec{type, s, k, t, r, q, vol};
}

bool all_finite(const Greeks& g) {
  return std::isfinite(g.price) && std::isfinite(g.delta) && std::isfinite(g.gamma) &&
         std::isfinite(g.vega) && std::isfinite(g.theta) && std::isfinite(g.rho) &&
         std::isfinite(g.vanna) && std::isfinite(g.volga);
}

// Design 5.8 item 1: c - p = S e^{-qT} - K e^{-rT} within 1e-12 (S + K).
TEST(BlackScholes, PutCallParity) {
  opt::Xoshiro256pp rng{101};
  double worst = 0.0;
  for (int i = 0; i < 10'000; ++i) {
    OptionSpec s = opt::test::draw_spec(rng);
    s.type = OptionType::Call;
    const double c = opt::bsm_price(s);
    s.type = OptionType::Put;
    const double p = opt::bsm_price(s);
    const double rhs =
        s.spot * std::exp(-s.carry_yield * s.time) - s.strike * std::exp(-s.rate * s.time);
    const double err = std::abs((c - p) - rhs) / (s.spot + s.strike);
    worst = std::max(worst, err);
    ASSERT_LE(err, 1e-12) << "S=" << s.spot << " K=" << s.strike << " T=" << s.time;
  }
  RecordProperty("parity_max_rel", std::to_string(worst));
  std::printf(
      "[measure] put-call parity: max |c - p - (S e^-qT - K e^-rT)| / (S + K) = %.3g "
      "(tolerance 1e-12, 10000 points)\n",
      worst);
}

// Design 5.8 item 2a. J. Hull, "Options, Futures, and Other Derivatives",
// 9th ed., Example 15.6: S = 42, K = 40, r = 0.10, sigma = 0.20, T = 0.5
// gives c = 4.76 and p = 0.81. Citation as given in the design; not
// re-checked against a printed copy. The book rounds to cents, so the
// tolerance is 0.005.
TEST(BlackScholes, HullExample15_6) {
  const double c = opt::bsm_price(spec(OptionType::Call, 42, 40, 0.5, 0.10, 0.0, 0.20));
  const double p = opt::bsm_price(spec(OptionType::Put, 42, 40, 0.5, 0.10, 0.0, 0.20));
  EXPECT_NEAR(c, 4.76, 0.005);
  EXPECT_NEAR(p, 0.81, 0.005);
  std::printf("[measure] Hull 15.6: c = %.6f (book 4.76), p = %.6f (book 0.81)\n", c, p);
}

// Design 5.8 item 3: analytic Greeks against central finite differences.
TEST(BlackScholes, GreeksMatchFiniteDifferences) {
  enum : std::size_t { kDelta, kGamma, kVega, kTheta, kRho, kVanna, kVolga, kCount };
  constexpr std::array<const char*, kCount> kNames = {"delta", "gamma", "vega", "theta",
                                                      "rho",   "vanna", "volga"};
  std::array<double, kCount> worst{};
  opt::Xoshiro256pp rng{103};
  int points = 0;
  int draws = 0;
  while (points < 2000) {
    ++draws;
    const OptionSpec s = opt::test::draw_spec(rng);
    const double sd = s.vol * std::sqrt(s.time);
    const double d1 =
        (std::log(s.spot / s.strike) + (s.rate - s.carry_yield) * s.time + 0.5 * sd * sd) / sd;
    if (s.time < opt::test::kOneDay || sd < 0.005 || std::abs(d1) > 5.0) {
      continue;  // redraw
    }
    ++points;
    const Greeks g = opt::bsm_greeks(s);
    const double hs = 1e-4 * s.spot * sd;
    const double hv = 1e-4 * s.vol;
    const double ht = 1e-4 * s.time;
    const double hr = 1e-4 * std::min(sd, 1.0) / s.time;
    auto bumped = [&](double OptionSpec::* field, double h) {
      OptionSpec up = s;
      OptionSpec dn = s;
      up.*field += h;
      dn.*field -= h;
      return std::array<OptionSpec, 2>{up, dn};
    };
    auto fd_price = [&](double OptionSpec::* field, double h) {
      const auto [up, dn] = bumped(field, h);
      return (opt::bsm_price(up) - opt::bsm_price(dn)) / (2 * h);
    };
    auto fd_greek = [&](double OptionSpec::* field, double h, double Greeks::* out) {
      const auto [up, dn] = bumped(field, h);
      return (opt::bsm_greeks(up).*out - opt::bsm_greeks(dn).*out) / (2 * h);
    };
    const double sqrt_t = std::sqrt(s.time);
    const std::array<double, kCount> analytic = {g.delta, g.gamma, g.vega, g.theta,
                                                 g.rho,   g.vanna, g.volga};
    const std::array<double, kCount> fd = {
        fd_price(&OptionSpec::spot, hs),
        fd_greek(&OptionSpec::spot, hs, &Greeks::delta),
        fd_price(&OptionSpec::vol, hv),
        -fd_price(&OptionSpec::time, ht),
        fd_price(&OptionSpec::rate, hr),
        fd_greek(&OptionSpec::vol, hv, &Greeks::delta),
        fd_greek(&OptionSpec::vol, hv, &Greeks::vega),
    };
    const std::array<double, kCount> atol = {
        1e-8,
        1e-8 / (s.spot * sd),
        1e-8 * s.spot * sqrt_t,
        1e-8 * s.spot * s.vol / sqrt_t,
        1e-8 * s.strike * s.time,
        1e-8 / s.vol,
        1e-8 * s.spot * sqrt_t / s.vol,
    };
    for (std::size_t k = 0; k < kCount; ++k) {
      const double tol = atol[k] + 1e-5 * std::abs(analytic[k]);
      const double err = std::abs(analytic[k] - fd[k]);
      worst[k] = std::max(worst[k], err / tol);
      EXPECT_LE(err, tol) << kNames[k] << ": analytic " << analytic[k] << " fd " << fd[k]
                          << " S=" << s.spot << " K=" << s.strike << " T=" << s.time
                          << " r=" << s.rate << " q=" << s.carry_yield << " vol=" << s.vol;
    }
  }
  std::string line = "Greeks vs FD (2000 points, " + std::to_string(draws) +
                     " draws): max error/tolerance";
  for (std::size_t k = 0; k < kCount; ++k) {
    RecordProperty(std::string{"max_ratio_"} + kNames[k], std::to_string(worst[k]));
    char buf[64];
    std::snprintf(buf, sizeof buf, " %s %.3g", kNames[k], worst[k]);
    line += buf;
  }
  std::printf("[measure] %s\n", line.c_str());
}

// Black-76 Greeks (with respect to the forward, F and r held fixed) against
// finite differences, and its price against bsm_price.
TEST(BlackScholes, Black76MatchesBsmAndFiniteDifferences) {
  opt::Xoshiro256pp rng{104};
  for (int i = 0; i < 200; ++i) {
    OptionSpec s = opt::test::draw_spec(rng);
    s.time = std::max(s.time, 0.05);
    const double f = opt::bsm_forward(s);
    const double d = opt::bsm_discount(s);
    const Greeks g = opt::black76(s.type, f, s.strike, s.time, d, s.vol);
    EXPECT_EQ(g.price, opt::bsm_price(s));
    const double r = s.rate;
    auto px = [&](double ff, double t, double rr, double v) {
      return opt::black76(s.type, ff, s.strike, t, std::exp(-rr * t), v).price;
    };
    const double hf = 1e-5 * f;
    const double hv = 1e-5 * s.vol;
    const double ht = 1e-5 * s.time;
    const double hr = 1e-5;
    const double scale = f + s.strike;
    EXPECT_NEAR(g.delta, (px(f + hf, s.time, r, s.vol) - px(f - hf, s.time, r, s.vol)) / (2 * hf),
                1e-6);
    EXPECT_NEAR(g.vega, (px(f, s.time, r, s.vol + hv) - px(f, s.time, r, s.vol - hv)) / (2 * hv),
                1e-6 * scale);
    EXPECT_NEAR(g.theta, -(px(f, s.time + ht, r, s.vol) - px(f, s.time - ht, r, s.vol)) / (2 * ht),
                1e-5 * scale);
    EXPECT_NEAR(g.rho, (px(f, s.time, r + hr, s.vol) - px(f, s.time, r - hr, s.vol)) / (2 * hr),
                1e-5 * scale);
    auto delta_at = [&](double ff, double v) {
      return opt::black76(s.type, ff, s.strike, s.time, d, v).delta;
    };
    EXPECT_NEAR(g.gamma, (delta_at(f + hf, s.vol) - delta_at(f - hf, s.vol)) / (2 * hf),
                1e-6 / f + 1e-4 * g.gamma);
    EXPECT_NEAR(g.vanna, (delta_at(f, s.vol + hv) - delta_at(f, s.vol - hv)) / (2 * hv),
                1e-5 / s.vol + 1e-4 * std::abs(g.vanna));
  }
}

// Design 5.8 item 9: the limits of section 5.2.
TEST(BlackScholes, ZeroTimeGivesIntrinsic) {
  for (const OptionType type : {OptionType::Call, OptionType::Put}) {
    const double w = opt::omega(type);
    for (const double spot : {80.0, 100.0, 120.0}) {
      const Greeks g = opt::bsm_greeks(spec(type, spot, 100, 0.0, 0.05, 0.02, 0.3));
      EXPECT_EQ(g.price, std::max(w * (spot - 100.0), 0.0));
      const double expected_delta = spot == 100.0 ? 0.5 * w : (w * (spot - 100.0) > 0 ? w : 0.0);
      EXPECT_EQ(g.delta, expected_delta);
      EXPECT_EQ(g.gamma, 0.0);
      EXPECT_EQ(g.vega, 0.0);
      EXPECT_EQ(g.theta, 0.0);
      EXPECT_EQ(g.rho, 0.0);
      EXPECT_EQ(g.vanna, 0.0);
      EXPECT_EQ(g.volga, 0.0);
      EXPECT_EQ(opt::bsm_price(spec(type, spot, 100, 0.0, 0.05, 0.02, 0.3)), g.price);
    }
  }
}

TEST(BlackScholes, ZeroVolLimits) {
  const double t = 2.0;
  const double r = 0.05;
  const double q = 0.01;
  const double dq = std::exp(-q * t);
  const double dr = std::exp(-r * t);
  for (const OptionType type : {OptionType::Call, OptionType::Put}) {
    const double w = opt::omega(type);
    for (const double strike : {80.0, 120.0}) {
      const OptionSpec s = spec(type, 100, strike, t, r, q, 0.0);
      const double fwd = opt::bsm_forward(s);
      const Greeks g = opt::bsm_greeks(s);
      ASSERT_TRUE(all_finite(g));
      const bool itm = w * (fwd - strike) > 0;
      EXPECT_DOUBLE_EQ(g.price, dr * std::max(w * (fwd - strike), 0.0));
      EXPECT_DOUBLE_EQ(g.delta, itm ? w * dq : 0.0);
      EXPECT_DOUBLE_EQ(g.rho, itm ? w * strike * t * dr : 0.0);
      EXPECT_DOUBLE_EQ(g.theta, itm ? w * (q * 100 * dq - r * strike * dr) : 0.0);
      EXPECT_EQ(g.gamma, 0.0);
      EXPECT_EQ(g.vega, 0.0);
      EXPECT_EQ(g.vanna, 0.0);
      EXPECT_EQ(g.volga, 0.0);
    }
    // F == K exactly (r == q): half the in-the-money values.
    const OptionSpec atm = spec(type, 100, 100, t, 0.03, 0.03, 0.0);
    const Greeks g = opt::bsm_greeks(atm);
    const double d3 = std::exp(-0.03 * t);
    EXPECT_EQ(g.price, 0.0);
    EXPECT_DOUBLE_EQ(g.delta, 0.5 * w * d3);
    EXPECT_DOUBLE_EQ(g.rho, 0.5 * w * 100 * t * d3);
    EXPECT_DOUBLE_EQ(g.theta, 0.5 * w * (0.03 * 100 * d3 - 0.03 * 100 * d3));
  }
}

TEST(BlackScholes, ExtremeMoneynessIsFiniteAndHitsLimits) {
  for (const OptionType type : {OptionType::Call, OptionType::Put}) {
    const double w = opt::omega(type);
    for (const double ratio : {1e-8, 1e-3, 1e3, 1e8}) {
      for (const double vol : {1e-300, 1e-8, 0.2}) {
        const OptionSpec s = spec(type, 100.0 * ratio, 100.0, 1.0, 0.05, 0.02, vol);
        const Greeks g = opt::bsm_greeks(s);
        ASSERT_TRUE(all_finite(g)) << ratio << " " << vol;
        const double fwd = opt::bsm_forward(s);
        const double intrinsic = std::exp(-0.05) * std::max(w * (fwd - 100.0), 0.0);
        EXPECT_NEAR(g.price, intrinsic, 1e-12 * (fwd + 100.0)) << ratio << " " << vol;
        const bool itm = w * (fwd - 100.0) > 0;
        EXPECT_NEAR(g.delta, itm ? w * std::exp(-0.02) : 0.0, 1e-12) << ratio << " " << vol;
        EXPECT_GE(g.gamma, 0.0);
        EXPECT_GE(g.vega, 0.0);
      }
    }
  }
}

// Price within the no-arbitrage bounds D max(omega(F-K), 0) <= V <= S e^{-qT}
// (call) or K e^{-rT} (put), up to a rounding allowance of 1e-14 (S + K).
TEST(BlackScholes, PriceWithinNoArbitrageBounds) {
  opt::Xoshiro256pp rng{109};
  double worst = 0.0;
  for (int i = 0; i < 20'000; ++i) {
    const OptionSpec s = opt::test::draw_spec(rng);
    const double v = opt::bsm_price(s);
    const double w = opt::omega(s.type);
    const double fwd = opt::bsm_forward(s);
    const double disc = opt::bsm_discount(s);
    const double lower = disc * std::max(w * (fwd - s.strike), 0.0);
    const double upper = s.type == OptionType::Call ? s.spot * std::exp(-s.carry_yield * s.time)
                                                    : s.strike * disc;
    const double slack = 1e-14 * (s.spot + s.strike);
    worst = std::max({worst, (lower - v) / (s.spot + s.strike), (v - upper) / (s.spot + s.strike)});
    ASSERT_GE(v, 0.0);
    ASSERT_GE(v, lower - slack);
    ASSERT_LE(v, upper + slack);
  }
  std::printf("[measure] bounds: max violation / (S + K) = %.3g (allowance 1e-14)\n", worst);
}

TEST(BlackScholes, ValidateReportsFirstProblem) {
  using opt::InputError;
  const OptionSpec ok = spec(OptionType::Call, 100, 100, 1, 0.05, 0, 0.2);
  EXPECT_EQ(opt::validate(ok), InputError::Ok);
  OptionSpec s = ok;
  s.spot = 0;
  EXPECT_EQ(opt::validate(s), InputError::NonPositiveSpot);
  s = ok;
  s.strike = -1;
  EXPECT_EQ(opt::validate(s), InputError::NonPositiveStrike);
  s = ok;
  s.time = -1e-9;
  EXPECT_EQ(opt::validate(s), InputError::NegativeTime);
  s = ok;
  s.vol = -0.1;
  EXPECT_EQ(opt::validate(s), InputError::NegativeVol);
  s = ok;
  s.rate = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(opt::validate(s), InputError::NonFinite);
  s = ok;
  s.spot = std::numeric_limits<double>::infinity();
  EXPECT_EQ(opt::validate(s), InputError::NonFinite);
  s = ok;
  s.time = 0;
  s.vol = 0;
  EXPECT_EQ(opt::validate(s), InputError::Ok);
}

}  // namespace
