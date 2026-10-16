#include "opt/pricing/monte_carlo.hpp"

#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

using opt::McConfig;
using opt::OptionSpec;
using opt::OptionType;

OptionSpec spec(OptionType type, double s, double k, double t, double r, double q, double vol) {
  return OptionSpec{type, s, k, t, r, q, vol};
}

McConfig config(std::uint64_t paths, std::uint64_t seed, bool anti = true, bool cv = true) {
  McConfig c;
  c.paths = paths;
  c.seed = seed;
  c.antithetic = anti;
  c.control_variate = cv;
  return c;
}

// Design 5.8 item 8a: |MC - BSM| <= 4 SE on 5 parameter sets (fixed seed).
TEST(MonteCarlo, WithinFourStandardErrorsOfBsm) {
  constexpr std::array<OptionSpec, 5> kSets = {{
      {OptionType::Call, 100, 100, 1.0, 0.05, 0.0, 0.20},
      {OptionType::Put, 100, 110, 0.5, 0.03, 0.01, 0.35},
      {OptionType::Call, 50, 60, 2.0, 0.01, 0.02, 0.50},
      {OptionType::Put, 200, 150, 0.25, 0.08, 0.0, 0.25},
      {OptionType::Call, 100, 80, 5.0, 0.0, 0.03, 0.15},
  }};
  for (const OptionSpec& s : kSets) {
    const auto r = opt::mc_european(s, config(1'000'000, 42));
    const double bsm = opt::bsm_price(s);
    const double z = (r.price - bsm) / r.std_error;
    std::printf("[measure] MC %s S=%g K=%g: mc=%.6f bsm=%.6f se=%.3e (err/se = %+.2f) beta=%.3f\n",
                s.type == OptionType::Call ? "call" : "put", s.spot, s.strike, r.price, bsm,
                r.std_error, z, r.beta);
    EXPECT_LE(std::abs(z), 4.0);
    EXPECT_EQ(r.paths, 1'000'000U);
  }
}

// Design 5.8 item 8b: 50 seeds x 40,000 paths; count |err| > 2 SE <= 8.
// Under the normal approximation each run exceeds 2 SE with probability
// p = 0.0455, so the count is Binomial(50, p) with mean 2.28 and
// P(count > 8) = 3.8e-4 (sum over k = 9..50 of C(50,k) p^k (1-p)^(50-k)).
// The design states "~0.2%"; the exact binomial tail is smaller.
TEST(MonteCarlo, CoverageOverSeeds) {
  const OptionSpec s = spec(OptionType::Call, 100, 105, 1.0, 0.04, 0.01, 0.30);
  const double bsm = opt::bsm_price(s);
  int exceed = 0;
  for (std::uint64_t seed = 1; seed <= 50; ++seed) {
    const auto r = opt::mc_european(s, config(40'000, seed));
    if (std::abs(r.price - bsm) > 2 * r.std_error) {
      ++exceed;
    }
  }
  std::printf("[measure] MC coverage: %d of 50 seeds outside 2 SE (expected ~2.3, limit 8)\n",
              exceed);
  RecordProperty("exceedances_2se", std::to_string(exceed));
  EXPECT_LE(exceed, 8);
}

// Design 5.8 item 8c: SE(4n) / SE(n) in [0.45, 0.55].
TEST(MonteCarlo, StandardErrorShrinksAsOneOverSqrtN) {
  const OptionSpec s = spec(OptionType::Put, 100, 95, 0.75, 0.02, 0.0, 0.25);
  const double se_n = opt::mc_european(s, config(40'000, 7)).std_error;
  const double se_4n = opt::mc_european(s, config(160'000, 7)).std_error;
  const double ratio = se_4n / se_n;
  std::printf("[measure] MC SE(160000)/SE(40000) = %.4f (expected 0.5)\n", ratio);
  EXPECT_GE(ratio, 0.45);
  EXPECT_LE(ratio, 0.55);
}

// Design 5.8 item 8d: each variance reduction lowers SE on an ATM call.
TEST(MonteCarlo, EachVarianceReductionHelps) {
  const OptionSpec s = spec(OptionType::Call, 100, 100, 1.0, 0.05, 0.0, 0.20);
  constexpr std::uint64_t kPaths = 200'000;
  const double plain = opt::mc_european(s, config(kPaths, 3, false, false)).std_error;
  const double anti = opt::mc_european(s, config(kPaths, 3, true, false)).std_error;
  const double cv = opt::mc_european(s, config(kPaths, 3, false, true)).std_error;
  const double both = opt::mc_european(s, config(kPaths, 3, true, true)).std_error;
  std::printf("[measure] MC SE at %llu paths: plain %.3e, antithetic %.3e (x%.2f), control "
              "variate %.3e (x%.2f), both %.3e (x%.2f)\n",
              static_cast<unsigned long long>(kPaths), plain, anti, plain / anti, cv, plain / cv,
              both, plain / both);
  EXPECT_LT(anti, plain);
  EXPECT_LT(cv, plain);
  EXPECT_LT(both, anti);
  EXPECT_LT(both, cv);
}

TEST(MonteCarlo, DeterministicForSeed) {
  const OptionSpec s = spec(OptionType::Call, 100, 100, 1.0, 0.05, 0.0, 0.20);
  const auto a = opt::mc_european(s, config(10'000, 9));
  const auto b = opt::mc_european(s, config(10'000, 9));
  const auto c = opt::mc_european(s, config(10'000, 10));
  EXPECT_EQ(a.price, b.price);
  EXPECT_EQ(a.std_error, b.std_error);
  EXPECT_NE(a.price, c.price);
}

TEST(MonteCarlo, DegenerateInputs) {
  // sigma == 0: every path is the forward; price exact, SE 0, beta 0.
  const OptionSpec s0 = spec(OptionType::Call, 100, 90, 1.0, 0.05, 0.0, 0.0);
  const auto r0 = opt::mc_european(s0, config(1000, 1));
  EXPECT_DOUBLE_EQ(r0.price, opt::bsm_price(s0));
  EXPECT_EQ(r0.std_error, 0.0);
  EXPECT_EQ(r0.beta, 0.0);
  // One sample: SE undefined.
  const auto r1 = opt::mc_european(spec(OptionType::Call, 100, 90, 1.0, 0.05, 0.0, 0.2),
                                   config(1, 1, false, false));
  EXPECT_TRUE(std::isnan(r1.std_error));
}

TEST(MonteCarlo, InvalidConfigThrows) {
  const OptionSpec s = spec(OptionType::Call, 100, 100, 1.0, 0.05, 0.0, 0.20);
  EXPECT_THROW((void)opt::mc_european(s, config(0, 1)), std::invalid_argument);
  EXPECT_THROW((void)opt::mc_european(s, config(0, 1, false, false)), std::invalid_argument);
  EXPECT_THROW((void)opt::mc_european(s, config(1001, 1, true, true)), std::invalid_argument);
  EXPECT_NO_THROW((void)opt::mc_european(s, config(1001, 1, false, true)));
  OptionSpec bad = s;
  bad.vol = -1;
  EXPECT_THROW((void)opt::mc_european(bad, config(100, 1)), std::invalid_argument);
}

}  // namespace
