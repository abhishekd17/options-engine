#include "opt/core/version.hpp"

#include <algorithm>
#include <string_view>

#include <gtest/gtest.h>

namespace {

TEST(Version, IsSemver) {
  const auto v = opt::version();
  EXPECT_FALSE(v.empty());
  EXPECT_EQ(std::count(v.begin(), v.end(), '.'), 2);
}

TEST(Version, SimdBackendMatchesWidth) {
  const std::string_view name = opt::simd_backend_name();
  if (name == "neon") {
    EXPECT_EQ(opt::simd_width(), 2U);
  } else if (name == "avx2+fma") {
    EXPECT_EQ(opt::simd_width(), 4U);
  } else {
    EXPECT_EQ(name, "scalar");
    EXPECT_EQ(opt::simd_width(), 1U);
  }
}

TEST(Version, CompilerIdIsNonEmpty) {
  EXPECT_FALSE(opt::compiler_id().empty());
}

}  // namespace
