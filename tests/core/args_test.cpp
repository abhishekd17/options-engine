#include "args.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using opt::tools::ArgIterator;
using opt::tools::parse_double;
using opt::tools::parse_u64;
using opt::tools::UsageError;

TEST(Args, ParseDoubleAcceptsWholeFiniteInRange) {
  EXPECT_EQ(parse_double("--x", "1.5", 0, 10), 1.5);
  EXPECT_EQ(parse_double("--x", "-2e-3", -1, 1), -2e-3);
  EXPECT_EQ(parse_double("--x", "10", 0, 10), 10.0);
}

TEST(Args, ParseDoubleRejectsBadInput) {
  for (const char* bad : {"", " 1", "1.5x", "abc", "nan", "inf", "1e999", "--1"}) {
    EXPECT_THROW((void)parse_double("--x", bad, -1e300, 1e300), UsageError) << bad;
  }
  EXPECT_THROW((void)parse_double("--x", "11", 0, 10), UsageError);
  EXPECT_THROW((void)parse_double("--x", "-0.1", 0, 10), UsageError);
}

TEST(Args, ParseU64) {
  EXPECT_EQ(parse_u64("--n", "0", 0, 5), 0U);
  EXPECT_EQ(parse_u64("--n", "18446744073709551615", 0, UINT64_MAX), UINT64_MAX);
  for (const char* bad : {"", "-1", "+1", "1.0", "12a", " 3", "18446744073709551616"}) {
    EXPECT_THROW((void)parse_u64("--n", bad, 0, UINT64_MAX), UsageError) << bad;
  }
  EXPECT_THROW((void)parse_u64("--n", "6", 0, 5), UsageError);
}

TEST(Args, IteratorWalksFlagsAndValues) {
  const std::array<const char*, 5> argv = {"tool", "--a", "1", "--b", "x"};
  ArgIterator it{static_cast<int>(argv.size()), argv.data()};
  ASSERT_FALSE(it.done());
  EXPECT_EQ(it.next(), "--a");
  EXPECT_EQ(it.value("--a"), "1");
  EXPECT_EQ(it.next(), "--b");
  EXPECT_EQ(it.value("--b"), "x");
  EXPECT_TRUE(it.done());
}

TEST(Args, MissingValueThrows) {
  const std::array<const char*, 2> argv = {"tool", "--a"};
  ArgIterator it{static_cast<int>(argv.size()), argv.data()};
  EXPECT_EQ(it.next(), "--a");
  EXPECT_THROW((void)it.value("--a"), UsageError);
}

TEST(Args, NoArguments) {
  const std::array<const char*, 1> argv = {"tool"};
  EXPECT_TRUE((ArgIterator{1, argv.data()}.done()));
}

}  // namespace
