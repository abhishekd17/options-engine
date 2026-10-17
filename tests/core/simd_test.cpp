// opt::simd: every backend gives, lane by lane, exactly the scalar result.
#include "opt/core/rng.hpp"
#include "opt/simd/simd.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace simd = opt::simd;

static_assert(simd::Lane<double>);
static_assert(simd::Lane<simd::Emulated<4>>);
static_assert(simd::Lane<simd::Emulated<8>>);
static_assert(simd::Lane<simd::Native>);
static_assert(simd::kWidth<double> == 1);
static_assert(simd::kWidth<simd::Emulated<4>> == 4);
static_assert(simd::kWidth<simd::Emulated<8>> == 8);

enum class Kind : std::uint8_t { A = 0, B = 1 };

template <class V>
std::array<double, simd::kWidth<V>> lanes(V v) {
  std::array<double, simd::kWidth<V>> out{};
  simd::store(out.data(), v);
  return out;
}

template <class V>
std::array<bool, simd::kWidth<V>> mask_lanes(simd::Mask<V> m) {
  // select(m, 1, 0) turns a mask into lanes for inspection.
  const auto v = lanes(simd::select(m, simd::broadcast<V>(1.0), simd::broadcast<V>(0.0)));
  std::array<bool, simd::kWidth<V>> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = v[i] == 1.0;
  }
  return out;
}

std::uint64_t bits(double x) {
  return std::bit_cast<std::uint64_t>(x);
}

// Inputs: random values of both signs and magnitudes plus special values.
std::vector<double> sample_values() {
  opt::Xoshiro256pp rng(42);
  std::vector<double> v = {0.0,  -0.0, 1.0,   -1.0, 0.5,  2.0,    1e-300, -1e300,
                           3.25, -7.5, 1e-10, 1e10, 709., -745., 1.5,    -2.25};
  for (int i = 0; i < 512; ++i) {
    const double mag = std::exp(opt::uniform(rng, -50.0, 50.0));
    v.push_back((rng() & 1U) != 0 ? mag : -mag);
  }
  return v;
}

template <class V>
class SimdBackend : public ::testing::Test {};

using Backends = ::testing::Types<simd::Emulated<4>, simd::Emulated<8>, simd::Native>;
TYPED_TEST_SUITE(SimdBackend, Backends);

TYPED_TEST(SimdBackend, LaneOpsEqualScalarBitwise) {
  using V = TypeParam;
  constexpr std::size_t kW = simd::kWidth<V>;
  const auto vals = sample_values();
  std::size_t checked = 0;
  for (std::size_t start = 0; start + 3 * kW <= vals.size(); start += kW) {
    // a, b, c are three disjoint windows of the sample (rotated so every
    // value meets several partners).
    std::vector<double> a(kW), b(kW), c(kW);
    for (std::size_t i = 0; i < kW; ++i) {
      a[i] = vals[start + i];
      b[i] = vals[(start + kW + 3 * i + 1) % vals.size()];
      c[i] = vals[(start + 2 * kW + 5 * i + 2) % vals.size()];
    }
    const V va = simd::load<V>(a.data());
    const V vb = simd::load<V>(b.data());
    const V vc = simd::load<V>(c.data());
    const auto add = lanes(va + vb);
    const auto sub = lanes(va - vb);
    const auto mul = lanes(va * vb);
    const auto div = lanes(va / vb);
    const auto neg = lanes(-va);
    const auto fma = lanes(simd::fma(va, vb, vc));
    const auto sq = lanes(simd::sqrt(simd::abs(va)));
    const auto mn = lanes(simd::min(va, vb));
    const auto mx = lanes(simd::max(va, vb));
    const auto ab = lanes(simd::abs(va));
    const auto lt = mask_lanes<V>(va < vb);
    const auto le = mask_lanes<V>(va <= vb);
    const auto gt = mask_lanes<V>(va > vb);
    const auto ge = mask_lanes<V>(va >= vb);
    const auto eq = mask_lanes<V>(va == va);
    const auto ne = mask_lanes<V>(va != vb);
    const auto sel = lanes(simd::select(va < vb, va, vc));
    const auto rt = lanes(simd::from_bits(simd::to_bits(va)));
    const auto sh = lanes(simd::from_bits((simd::to_bits(va) >> 52) << 52));
    const auto orr = lanes(simd::from_bits(simd::to_bits(va) | simd::to_bits(vb)));
    const auto andd = lanes(simd::from_bits(simd::to_bits(va) & simd::to_bits(vb)));
    for (std::size_t i = 0; i < kW; ++i) {
      SCOPED_TRACE(::testing::Message() << "a=" << a[i] << " b=" << b[i] << " c=" << c[i]);
      EXPECT_EQ(bits(add[i]), bits(a[i] + b[i]));
      EXPECT_EQ(bits(sub[i]), bits(a[i] - b[i]));
      EXPECT_EQ(bits(mul[i]), bits(a[i] * b[i]));
      EXPECT_EQ(bits(div[i]), bits(a[i] / b[i]));
      EXPECT_EQ(bits(neg[i]), bits(-a[i]));
      EXPECT_EQ(bits(fma[i]), bits(std::fma(a[i], b[i], c[i])));
      EXPECT_EQ(bits(sq[i]), bits(std::sqrt(std::fabs(a[i]))));
      // min/max: equal for non-NaN inputs that are not both zero (see
      // simd_scalar.hpp).
      if (!(a[i] == 0.0 && b[i] == 0.0)) {
        EXPECT_EQ(bits(mn[i]), bits(simd::min(a[i], b[i])));
        EXPECT_EQ(bits(mx[i]), bits(simd::max(a[i], b[i])));
      }
      EXPECT_EQ(bits(ab[i]), bits(std::fabs(a[i])));
      EXPECT_EQ(lt[i], a[i] < b[i]);
      EXPECT_EQ(le[i], a[i] <= b[i]);
      EXPECT_EQ(gt[i], a[i] > b[i]);
      EXPECT_EQ(ge[i], a[i] >= b[i]);
      EXPECT_TRUE(eq[i]);
      EXPECT_EQ(ne[i], a[i] != b[i]);
      EXPECT_EQ(bits(sel[i]), bits(a[i] < b[i] ? a[i] : c[i]));
      EXPECT_EQ(bits(rt[i]), bits(a[i]));
      EXPECT_EQ(bits(sh[i]), (bits(a[i]) >> 52) << 52);
      EXPECT_EQ(bits(orr[i]), bits(a[i]) | bits(b[i]));
      EXPECT_EQ(bits(andd[i]), bits(a[i]) & bits(b[i]));
      ++checked;
    }
  }
  EXPECT_GT(checked, 400U);
}

TYPED_TEST(SimdBackend, BroadcastLoadStoreUnaligned) {
  using V = TypeParam;
  constexpr std::size_t kW = simd::kWidth<V>;
  std::vector<double> buf(2 * kW + 3);
  for (std::size_t i = 0; i < buf.size(); ++i) {
    buf[i] = static_cast<double>(i) + 0.25;
  }
  // Offset 1: not 16- or 32-byte aligned for any backend.
  const V v = simd::load<V>(buf.data() + 1);
  std::vector<double> out(2 * kW + 3, -1.0);
  simd::store(out.data() + 1, v);
  EXPECT_EQ(out[0], -1.0);
  for (std::size_t i = 0; i < kW; ++i) {
    EXPECT_EQ(out[i + 1], buf[i + 1]);
  }
  EXPECT_EQ(out[kW + 1], -1.0);
  for (const double x : lanes(simd::broadcast<V>(3.5))) {
    EXPECT_EQ(x, 3.5);
  }
}

TYPED_TEST(SimdBackend, MasksSelectAny) {
  using V = TypeParam;
  constexpr std::size_t kW = simd::kWidth<V>;
  std::vector<double> x(kW, 0.0);
  const V zero = simd::broadcast<V>(0.0);
  EXPECT_FALSE(simd::any(simd::load<V>(x.data()) > zero));
  for (std::size_t hot = 0; hot < kW; ++hot) {
    std::fill(x.begin(), x.end(), 0.0);
    x[hot] = 1.0;
    const auto m = simd::load<V>(x.data()) > zero;
    EXPECT_TRUE(simd::any(m)) << "lane " << hot;
    const auto picked = lanes(simd::select(m, simd::broadcast<V>(7.0), simd::broadcast<V>(9.0)));
    for (std::size_t i = 0; i < kW; ++i) {
      EXPECT_EQ(picked[i], i == hot ? 7.0 : 9.0);
    }
    const auto both = mask_lanes<V>(simd::mask_and(m, simd::load<V>(x.data()) >= zero));
    const auto either = mask_lanes<V>(simd::mask_or(m, simd::load<V>(x.data()) < zero));
    for (std::size_t i = 0; i < kW; ++i) {
      EXPECT_EQ(both[i], i == hot);
      EXPECT_EQ(either[i], i == hot);
    }
  }
  // NaN: != is true, every ordered comparison false.
  const V nan = simd::broadcast<V>(std::numeric_limits<double>::quiet_NaN());
  EXPECT_TRUE(simd::any(nan != nan));
  EXPECT_FALSE(simd::any(nan == nan));
  EXPECT_FALSE(simd::any(nan < zero));
  EXPECT_FALSE(simd::any(nan >= zero));
}

TYPED_TEST(SimdBackend, LoadTypeGivesOmega) {
  using V = TypeParam;
  constexpr std::size_t kW = simd::kWidth<V>;
  std::vector<Kind> types(kW + 1);
  for (std::size_t i = 0; i < types.size(); ++i) {
    types[i] = (i % 3 == 1) ? Kind::B : Kind::A;
  }
  const auto w = lanes(simd::load_type<V>(types.data() + 1));  // unaligned start
  for (std::size_t i = 0; i < kW; ++i) {
    EXPECT_EQ(w[i], types[i + 1] == Kind::A ? 1.0 : -1.0);
  }
}

TEST(SimdScalar, LoadTypeAndBits) {
  const Kind put = Kind::B;
  EXPECT_EQ(simd::load_type<double>(&put), -1.0);
  EXPECT_EQ(simd::to_bits(1.0), 0x3FF0000000000000ULL);
  EXPECT_EQ(simd::from_bits(0x4000000000000000ULL), 2.0);
  EXPECT_EQ(simd::min(1.0, 2.0), 1.0);
  EXPECT_EQ(simd::max(1.0, 2.0), 2.0);
}

}  // namespace
