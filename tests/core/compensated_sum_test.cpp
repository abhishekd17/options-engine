#include "opt/core/compensated_sum.hpp"

#include <gtest/gtest.h>

namespace {

TEST(NeumaierSum, StartsAtZeroOrInitial) {
  EXPECT_EQ(opt::NeumaierSum{}.value(), 0.0);
  EXPECT_EQ(opt::NeumaierSum{2.5}.value(), 2.5);
}

TEST(NeumaierSum, RecoversBitsLostToLargeTerms) {
  // Plain summation gives 0; Kahan without Neumaier's branch also gives 0.
  opt::NeumaierSum s;
  for (const double x : {1.0, 1e100, 1.0, -1e100}) {
    s.add(x);
  }
  EXPECT_EQ(s.value(), 2.0);
}

TEST(NeumaierSum, ManySmallTermsAreExactToOneUlp) {
  // 10^7 copies of the double nearest 0.1 sum to 1000000.0000000000555...,
  // whose nearest double is 1e6. Plain summation drifts by about 1.6e-4.
  opt::NeumaierSum s;
  double plain = 0.0;
  for (int i = 0; i < 10'000'000; ++i) {
    s.add(0.1);
    plain += 0.1;
  }
  EXPECT_EQ(s.value(), 1e6);
  EXPECT_NE(plain, 1e6);
}

}  // namespace
