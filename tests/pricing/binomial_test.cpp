#include "opt/pricing/binomial.hpp"

#include "opt/core/rng.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using opt::Exercise;
using opt::OptionSpec;
using opt::OptionType;
using opt::TreeStatus;

OptionSpec spec(OptionType type, double s, double k, double t, double r, double q, double vol) {
  return OptionSpec{type, s, k, t, r, q, vol};
}

double tree(const OptionSpec& s, Exercise e, int n) {
  const auto r = opt::crr_price(s, e, n);
  EXPECT_EQ(r.status, TreeStatus::Ok);
  return r.price;
}

// Design 5.8 item 2c. J. Hull, "Options, Futures, and Other Derivatives",
// 9th ed., Example 21.1 / Figure 21.3: American put, S = 50, K = 50,
// r = 0.10, sigma = 0.40, T = 5/12, 5-step tree: 4.49. Citation as given in
// the design; not re-checked against a printed copy. Tolerance 0.005.
TEST(Crr, HullExample21_1AmericanPut) {
  const double v = tree(spec(OptionType::Put, 50, 50, 5.0 / 12.0, 0.10, 0, 0.40),
                        Exercise::American, 5);
  EXPECT_NEAR(v, 4.49, 0.005);
  std::printf("[measure] Hull 21.1: 5-step American put = %.6f (book 4.49)\n", v);
}

// Design 5.8 item 6: one shared 200-point grid, S = 100, K in [50, 200],
// T in [0.1, 2], sigma in [0.10, 0.80], N = 200, per-sub-test rates.
struct GridPoint {
  double strike, time, vol, u_rate, u_yield;
};

std::vector<GridPoint> american_grid() {
  opt::Xoshiro256pp rng{601};
  std::vector<GridPoint> g;
  for (int i = 0; i < 200; ++i) {
    GridPoint p{};
    p.strike = opt::uniform(rng, 50, 200);
    p.time = opt::uniform(rng, 0.1, 2.0);
    p.vol = opt::uniform(rng, 0.10, 0.80);
    p.u_rate = opt::uniform01(rng);   // mapped into each sub-test's rate range
    p.u_yield = opt::uniform01(rng);
    g.push_back(p);
  }
  return g;
}

constexpr int kGridSteps = 200;

TEST(Crr, AmericanPutAtLeastEuropean) {
  std::vector<double> ws(kGridSteps + 1);
  double max_premium = 0.0;
  for (const GridPoint& p : american_grid()) {
    const double r = -0.02 + 0.17 * p.u_rate;  // [-0.02, 0.15]
    const double q = 0.05 * p.u_yield;         // [0, 0.05]
    const OptionSpec s = spec(OptionType::Put, 100, p.strike, p.time, r, q, p.vol);
    const auto am = opt::crr_price(s, Exercise::American, kGridSteps, ws);
    const auto eu = opt::crr_price(s, Exercise::European, kGridSteps, ws);
    ASSERT_EQ(am.status, TreeStatus::Ok);
    ASSERT_EQ(eu.status, TreeStatus::Ok);
    EXPECT_GE(am.price, eu.price) << "K=" << p.strike << " T=" << p.time << " r=" << r;
    max_premium = std::max(max_premium, am.price - eu.price);
  }
  std::printf("[measure] American put early-exercise premium: max %.6f over 200 points\n",
              max_premium);
  EXPECT_GT(max_premium, 0.0);  // early exercise is worth something somewhere on the grid
}

TEST(Crr, AmericanCallWithoutDividendEqualsEuropeanBitwise) {
  std::vector<double> ws(kGridSteps + 1);
  for (const GridPoint& p : american_grid()) {
    const double r = 0.01 + 0.14 * p.u_rate;  // [0.01, 0.15]
    const OptionSpec s = spec(OptionType::Call, 100, p.strike, p.time, r, 0.0, p.vol);
    const auto am = opt::crr_price(s, Exercise::American, kGridSteps, ws);
    const auto eu = opt::crr_price(s, Exercise::European, kGridSteps, ws);
    ASSERT_EQ(am.status, TreeStatus::Ok);
    ASSERT_EQ(eu.status, TreeStatus::Ok);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(am.price), std::bit_cast<std::uint64_t>(eu.price))
        << am.price << " vs " << eu.price;
  }
}

TEST(Crr, AmericanCallWithDividendAtLeastEuropean) {
  std::vector<double> ws(kGridSteps + 1);
  double max_premium = 0.0;
  for (const GridPoint& p : american_grid()) {
    const double r = 0.01 + 0.14 * p.u_rate;   // [0.01, 0.15]
    const double q = 0.01 + 0.09 * p.u_yield;  // [0.01, 0.10]
    const OptionSpec s = spec(OptionType::Call, 100, p.strike, p.time, r, q, p.vol);
    const auto am = opt::crr_price(s, Exercise::American, kGridSteps, ws);
    const auto eu = opt::crr_price(s, Exercise::European, kGridSteps, ws);
    ASSERT_EQ(am.status, TreeStatus::Ok);
    ASSERT_EQ(eu.status, TreeStatus::Ok);
    EXPECT_GE(am.price, eu.price);
    max_premium = std::max(max_premium, am.price - eu.price);
  }
  std::printf("[measure] American call (q > 0) early-exercise premium: max %.6f\n", max_premium);
}

TEST(Crr, DeepInTheMoneyAmericanPutIsExercisedImmediately) {
  const double v = tree(spec(OptionType::Put, 50, 100, 1.0, 0.10, 0, 0.20), Exercise::American,
                        500);
  EXPECT_EQ(v, 50.0);
}

// Design 5.8 item 7: convergence to BSM.
TEST(Crr, ConvergesToBsmAtTheMoney) {
  struct Set {
    OptionType type;
    double r, q, vol, t;
  };
  constexpr std::array<Set, 5> kSets = {{{OptionType::Call, 0.05, 0.0, 0.20, 1.0},
                                         {OptionType::Put, 0.05, 0.0, 0.20, 1.0},
                                         {OptionType::Call, 0.02, 0.01, 0.40, 0.5},
                                         {OptionType::Put, 0.10, 0.03, 0.30, 2.0},
                                         {OptionType::Call, 0.0, 0.0, 0.80, 0.25}}};
  constexpr std::array<int, 4> kSteps = {100, 400, 1600, 6400};
  std::vector<double> ws(kSteps.back() + 1);
  double worst_ratio = 0.0;
  for (const Set& set : kSets) {
    const OptionSpec s = spec(set.type, 100, 100, set.t, set.r, set.q, set.vol);
    const double bsm = opt::bsm_price(s);
    std::array<double, kSteps.size()> err{};
    for (std::size_t i = 0; i < kSteps.size(); ++i) {
      const auto r = opt::crr_price(s, Exercise::European, kSteps[i], ws);
      ASSERT_EQ(r.status, TreeStatus::Ok);
      err[i] = std::abs(r.price - bsm);
    }
    std::printf("[measure] CRR ATM %s r=%.2f q=%.2f vol=%.2f T=%.2f: e(100)=%.3e e(400)=%.3e "
                "e(1600)=%.3e e(6400)=%.3e ratios %.4f %.4f %.4f\n",
                set.type == OptionType::Call ? "call" : "put", set.r, set.q, set.vol, set.t,
                err[0], err[1], err[2], err[3], err[1] / err[0], err[2] / err[1],
                err[3] / err[2]);
    for (std::size_t i = 0; i + 1 < kSteps.size(); ++i) {
      EXPECT_LE(err[i + 1], 0.30 * err[i]) << "N=" << kSteps[i];
      worst_ratio = std::max(worst_ratio, err[i + 1] / err[i]);
    }
    EXPECT_LE(err.back(), 1e-3 * 100);
  }
  RecordProperty("worst_ratio", std::to_string(worst_ratio));
}

TEST(Crr, NonAtmErrorBounded) {
  const OptionSpec s = spec(OptionType::Call, 100, 110, 1.0, 0.05, 0.0, 0.25);
  const double bsm = opt::bsm_price(s);
  std::vector<double> ws(6411);
  double worst = 0.0;
  for (int n = 6400; n <= 6410; ++n) {
    const auto r = opt::crr_price(s, Exercise::European, n, ws);
    ASSERT_EQ(r.status, TreeStatus::Ok);
    worst = std::max(worst, std::abs(r.price - bsm));
  }
  std::printf("[measure] CRR non-ATM (K=110): max |CRR(N) - BSM| over N in [6400, 6410] = %.3e "
              "(bound 0.2)\n",
              worst);
  EXPECT_LE(worst, 2e-3 * 100);
}

TEST(Crr, ArgumentValidation) {
  const OptionSpec s = spec(OptionType::Call, 100, 100, 1, 0.05, 0, 0.2);
  std::vector<double> ws(10);
  EXPECT_THROW((void)opt::crr_price(s, Exercise::European, 0, ws), std::invalid_argument);
  EXPECT_THROW((void)opt::crr_price(s, Exercise::European, -5), std::invalid_argument);
  EXPECT_THROW((void)opt::crr_price(s, Exercise::European, opt::kMaxTreeSteps + 1),
               std::invalid_argument);
  EXPECT_THROW((void)opt::crr_price(s, Exercise::European, 10, ws), std::invalid_argument);
  EXPECT_NO_THROW((void)opt::crr_price(s, Exercise::European, 9, ws));
  OptionSpec bad = s;
  bad.spot = -1;
  EXPECT_EQ(opt::crr_price(bad, Exercise::European, 9, ws).status, TreeStatus::InvalidInput);
  EXPECT_TRUE(std::isnan(opt::crr_price(bad, Exercise::European, 9, ws).price));
}

TEST(Crr, ProbabilityOutOfRange) {
  // |r - q| sqrt(dt) = 0.5 * 1 > sigma = 0.1 with one step.
  const auto r = opt::crr_price(spec(OptionType::Call, 100, 100, 1, 0.5, 0, 0.1),
                                Exercise::European, 1);
  EXPECT_EQ(r.status, TreeStatus::ProbabilityOutOfRange);
  EXPECT_TRUE(std::isnan(r.price));
}

TEST(Crr, Limits) {
  for (const Exercise e : {Exercise::European, Exercise::American}) {
    EXPECT_EQ(tree(spec(OptionType::Put, 90, 100, 0.0, 0.05, 0, 0.3), e, 10), 10.0);
    EXPECT_EQ(tree(spec(OptionType::Call, 90, 100, 0.0, 0.05, 0, 0.3), e, 10), 0.0);
  }
  // sigma == 0, European: the discounted forward intrinsic.
  const OptionSpec call0 = spec(OptionType::Call, 100, 90, 1.0, 0.05, 0.0, 0.0);
  EXPECT_EQ(tree(call0, Exercise::European, 10), opt::bsm_price(call0));
  // sigma == 0, American put with r > 0: exercise now (K - S) beats waiting
  // (D (K - F) = 100 e^{-0.05} - 90 < 10).
  const OptionSpec put0 = spec(OptionType::Put, 90, 100, 1.0, 0.05, 0.0, 0.0);
  EXPECT_EQ(tree(put0, Exercise::American, 10), 10.0);
  EXPECT_LT(tree(put0, Exercise::European, 10), 10.0);
  // sigma == 0 with q > r > 0: the put's value K e^{-rt} - S e^{-qt} first
  // rises, then falls, so the best exercise time is the interior
  // t* = ln(qS / (rK)) / (q - r) = ln(10 / 2.4) / 0.08 = 17.8 years.
  const double r = 0.02;
  const double q = 0.10;
  const OptionSpec put1 = spec(OptionType::Put, 100, 120, 25.0, r, q, 0.0);
  const double t_star = std::log((q * 100) / (r * 120)) / (q - r);
  ASSERT_GT(t_star, 0.0);
  ASSERT_LT(t_star, 25.0);
  const double expect = 120 * std::exp(-r * t_star) - 100 * std::exp(-q * t_star);
  EXPECT_NEAR(tree(put1, Exercise::American, 10), expect, 1e-12 * 120);
  EXPECT_GT(expect, 20.0);                                    // beats exercising now
  EXPECT_GT(expect, 120 * std::exp(-r * 25) - 100 * std::exp(-q * 25));  // and at expiry
}

TEST(Crr, WorkspaceAndAllocatingOverloadsAgree) {
  const OptionSpec s = spec(OptionType::Put, 100, 105, 0.7, 0.03, 0.01, 0.3);
  std::vector<double> ws(1000, 123.0);  // stale contents must not matter
  for (const Exercise e : {Exercise::European, Exercise::American}) {
    EXPECT_EQ(opt::crr_price(s, e, 300, ws).price, opt::crr_price(s, e, 300).price);
  }
}

}  // namespace
