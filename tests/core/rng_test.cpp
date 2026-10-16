#include "opt/core/rng.hpp"

#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <random>

#include <gtest/gtest.h>

namespace {

static_assert(std::uniform_random_bit_generator<opt::SplitMix64>);
static_assert(std::uniform_random_bit_generator<opt::Xoshiro256pp>);

// Reference outputs from the authors' C code, not from the code under test.
// Commands used to produce them (run on the dev machine):
//   mkdir -p /tmp/xoshiro-ref && cd /tmp/xoshiro-ref
//   curl -fsSL -o xoshiro256plusplus.c https://prng.di.unimi.it/xoshiro256plusplus.c
//   curl -fsSL -o splitmix64.c https://prng.di.unimi.it/splitmix64.c
//   curl -fsSL -o f2x.c https://prng.di.unimi.it/f2x.c   # included by xoshiro256plusplus.c
//   # sm_wrap.c: #include "splitmix64.c" + void sm_seed(uint64_t v) { x = v; }
//   # xo_wrap.c: #include "xoshiro256plusplus.c" + a setter for the static s[4]
//   # driver.c:  sm_seed(1); s[i] = sm_next() for i = 0..3; print 8 xo_next() outputs
//   cc -O2 -c -Dnext=sm_next sm_wrap.c -o sm.o
//   cc -O2 -c -Dnext=xo_next xo_wrap.c -o xo.o
//   cc -O2 driver.c sm.o xo.o -o ref && ./ref
constexpr std::array<std::uint64_t, 4> kSplitMix64Seed1 = {
    0x910a2dec89025cc1ULL, 0xbeeb8da1658eec67ULL, 0xf893a2eefb32555eULL, 0x71c18690ee42c90bULL};
constexpr std::array<std::uint64_t, 8> kXoshiro256ppSeed1 = {
    0xcfc5d07f6f03c29bULL, 0xbf424132963fe08dULL, 0x19a37d5757aaf520ULL, 0xbf08119f05cd56d6ULL,
    0x2f47184b86186fa4ULL, 0x97299fcae7202345ULL, 0xfca3c79508f41507ULL, 0x85fea5c90363f221ULL};

TEST(Rng, SplitMix64MatchesReference) {
  opt::SplitMix64 sm{1};
  for (const std::uint64_t expected : kSplitMix64Seed1) {
    EXPECT_EQ(sm(), expected);
  }
}

TEST(Rng, Xoshiro256ppMatchesReference) {
  opt::Xoshiro256pp rng{1};
  for (const std::uint64_t expected : kXoshiro256ppSeed1) {
    EXPECT_EQ(rng(), expected);
  }
}

TEST(Rng, StreamsDiffer) {
  auto a = opt::make_stream(42, 0);
  auto b = opt::make_stream(42, 1);
  int equal = 0;
  for (int i = 0; i < 1000; ++i) {
    equal += a() == b() ? 1 : 0;
  }
  EXPECT_EQ(equal, 0);
}

struct ConstantBits {
  using result_type = std::uint64_t;
  std::uint64_t value;
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
  result_type operator()() const { return value; }
};

TEST(Rng, Uniform01EndsAreStrictlyInside) {
  ConstantBits zero{0};
  ConstantBits ones{std::numeric_limits<std::uint64_t>::max()};
  EXPECT_EQ(opt::uniform01(zero), 0x1.0p-53);
  EXPECT_EQ(opt::uniform01(ones), 1.0 - 0x1.0p-53);
  EXPECT_GT(opt::uniform01(zero), 0.0);
  EXPECT_LT(opt::uniform01(ones), 1.0);
}

TEST(Rng, Uniform01MomentsWithinFiveStandardErrors) {
  constexpr int kN = 1'000'000;
  opt::Xoshiro256pp rng{2024};
  double mean = 0.0;
  double m2 = 0.0;  // Welford
  for (int i = 1; i <= kN; ++i) {
    const double u = opt::uniform01(rng);
    ASSERT_GT(u, 0.0);
    ASSERT_LT(u, 1.0);
    const double delta = u - mean;
    mean += delta / i;
    m2 += delta * (u - mean);
  }
  const double var = m2 / (kN - 1);
  // U(0,1): mean 1/2, variance 1/12, fourth central moment 1/80.
  const double se_mean = std::sqrt(1.0 / 12.0 / kN);
  const double se_var = std::sqrt((1.0 / 80.0 - 1.0 / 144.0) / kN);
  EXPECT_LE(std::abs(mean - 0.5), 5 * se_mean) << mean;
  EXPECT_LE(std::abs(var - 1.0 / 12.0), 5 * se_var) << var;
}

TEST(Rng, WorksWithStandardDistributions) {
  opt::Xoshiro256pp rng{7};
  std::uniform_int_distribution<int> dist{1, 6};
  for (int i = 0; i < 1000; ++i) {
    const int v = dist(rng);
    ASSERT_GE(v, 1);
    ASSERT_LE(v, 6);
  }
}

}  // namespace
