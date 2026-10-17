// Batch engines against the scalar reference (design 6.6).
//
// Tolerance per output: |fast - ref| <= atol + 1e-12 |ref|, with atol in that
// output's own units:
//   price 1e-14 (S + K)        delta 1e-14
//   vega  1e-14 (S + K) sqrt T rho   1e-14 K max(T, 1)
//   theta 1e-12 S sigma / sqrt T
//   gamma 1e-12 / (S sigma sqrt T)
// The tests print the worst error / tolerance ratio per output and the count
// of Simd outputs that are not bit-identical to FastScalar.
#include "opt/batch/batch.hpp"
#include "opt/batch/kernel.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"
#include "opt/simd/simd.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "batch_test_util.hpp"

namespace {

using opt::test::SoaGreeks;
using opt::test::SoaOptions;

constexpr double kRtol = 1e-12;
constexpr std::size_t kGridSize = 200'003;  // not a multiple of 2, 4 or 8
constexpr std::array<const char*, 6> kNames = {"price", "delta", "gamma", "vega", "theta", "rho"};

const SoaOptions& grid() {
  static const SoaOptions g = [] {
    SoaOptions o = opt::test::random_options(kGridSize, 2026);
    opt::test::append(o, opt::test::edge_options());
    return o;
  }();
  return g;
}

// The design 6.6 atol with S replaced by the discounted spot S e^{-qT} and K
// by the discounted strike K e^{-rT} (identical when r = q = 0). Theta also
// gets 1e-14 of its two carry terms |r| K e^{-rT} + |q| S e^{-qT}.
std::array<double, 6> atols(const SoaOptions& o, std::size_t i) {
  const double t = o.time[i];
  const double s = o.spot[i] * std::exp(-o.carry_yield[i] * t);
  const double k = o.strike[i] * std::exp(-o.rate[i] * t);
  const double v = o.vol[i];
  const double sqrt_t = std::sqrt(t);
  const double dq = std::exp(-o.carry_yield[i] * t);
  const double carry = std::fabs(o.rate[i]) * k + std::fabs(o.carry_yield[i]) * s;
  return {1e-14 * (s + k),
          1e-14 * dq,
          1e-12 * dq / (o.spot[i] * v * sqrt_t),
          1e-14 * (s + k) * sqrt_t,
          1e-12 * s * v / sqrt_t + 1e-14 * carry,
          1e-14 * k * std::max(t, 1.0)};
}

std::array<const std::vector<double>*, 6> columns(const SoaGreeks& g) {
  return {&g.price, &g.delta, &g.gamma, &g.vega, &g.theta, &g.rho};
}

struct Comparison {
  std::array<double, 6> max_ratio{};
  std::array<std::size_t, 6> arg{};
  std::array<std::size_t, 6> failures{};
};

// Compares `got` with `ref` on options [offset, offset + count) of `o`.
// `price_only` compares only the price column.
Comparison compare(const SoaOptions& o, const SoaGreeks& got, const SoaGreeks& ref,
                   std::size_t offset, std::size_t count, bool price_only = false) {
  Comparison c;
  const auto g = columns(got);
  const auto r = columns(ref);
  const std::size_t outputs = price_only ? 1 : 6;
  for (std::size_t i = offset; i < offset + count; ++i) {
    const auto atol = atols(o, i);
    for (std::size_t j = 0; j < outputs; ++j) {
      const double a = (*g[j])[i];
      const double b = (*r[j])[i];
      const double ratio = std::fabs(a - b) / (atol[j] + kRtol * std::fabs(b));
      if (!(ratio <= 1.0)) {
        ++c.failures[j];
      }
      if (ratio > c.max_ratio[j] || std::isnan(ratio)) {
        c.max_ratio[j] = std::isnan(ratio) ? std::numeric_limits<double>::infinity() : ratio;
        c.arg[j] = i;
      }
    }
  }
  return c;
}

void expect_within(const Comparison& c, const SoaOptions& o, const std::string& label,
                   bool print = false, bool price_only = false) {
  const std::size_t outputs = price_only ? 1 : 6;
  for (std::size_t j = 0; j < outputs; ++j) {
    const std::size_t i = c.arg[j];
    if (print) {
      std::printf("[batch] %-28s %-5s max err/tol %.3e (option %zu)\n", label.c_str(), kNames[j],
                  c.max_ratio[j], i);
      ::testing::Test::RecordProperty(label + " " + kNames[j] + " max_err_over_tol",
                                      std::to_string(c.max_ratio[j]));
    }
    EXPECT_EQ(c.failures[j], 0U) << label << " " << kNames[j] << ": worst option " << i
                                 << " (type " << static_cast<int>(o.type[i]) << ", S "
                                 << o.spot[i] << ", K " << o.strike[i] << ", T " << o.time[i]
                                 << ", r " << o.rate[i] << ", q " << o.carry_yield[i]
                                 << ", sigma " << o.vol[i] << "), err/tol " << c.max_ratio[j];
  }
}

const SoaGreeks& reference() {
  static const SoaGreeks ref = [] {
    SoaGreeks g(grid().size());
    opt::greeks_batch(opt::Engine::Reference, grid().view(), g.view());
    return g;
  }();
  return ref;
}

SoaGreeks run_engine(opt::Engine e, const SoaOptions& o) {
  SoaGreeks g(o.size());
  opt::greeks_batch(e, o.view(), g.view());
  return g;
}

SoaGreeks run_prices(opt::Engine e, const SoaOptions& o) {
  SoaGreeks g(o.size());
  opt::price_batch(e, o.view(), g.price);
  return g;
}

template <class V>
SoaGreeks run_kernel(const SoaOptions& o) {
  SoaGreeks g(o.size());
  const auto out = g.view();
  opt::kernel::greeks_range<V>(o.view(), out, 0, o.size());
  return g;
}

std::size_t bit_mismatches(const SoaGreeks& a, const SoaGreeks& b, bool price_only = false) {
  const auto ca = columns(a);
  const auto cb = columns(b);
  std::size_t n = 0;
  for (std::size_t j = 0; j < (price_only ? 1U : 6U); ++j) {
    for (std::size_t i = 0; i < ca[j]->size(); ++i) {
      n += std::bit_cast<std::uint64_t>((*ca[j])[i]) != std::bit_cast<std::uint64_t>((*cb[j])[i])
               ? 1U
               : 0U;
    }
  }
  return n;
}

TEST(BatchEngine, GridIsInDomain) {
  EXPECT_EQ(opt::validate_batch(grid().view()), grid().size());
  EXPECT_GT(grid().size(), kGridSize);
}

TEST(BatchEngine, ReferenceEqualsScalarBsm) {
  const auto& o = grid();
  const auto& ref = reference();
  for (std::size_t i = 0; i < o.size(); i += 997) {
    const opt::Greeks g = opt::bsm_greeks(
        {o.type[i], o.spot[i], o.strike[i], o.time[i], o.rate[i], o.carry_yield[i], o.vol[i]});
    EXPECT_EQ(ref.price[i], g.price);
    EXPECT_EQ(ref.delta[i], g.delta);
    EXPECT_EQ(ref.theta[i], g.theta);
  }
  const auto prices = run_prices(opt::Engine::Reference, o);
  for (std::size_t i = 0; i < o.size(); i += 997) {
    EXPECT_EQ(prices.price[i], opt::bsm_price({o.type[i], o.spot[i], o.strike[i], o.time[i],
                                               o.rate[i], o.carry_yield[i], o.vol[i]}));
  }
}

TEST(BatchEngine, FastScalarMatchesReference) {
  const auto got = run_engine(opt::Engine::FastScalar, grid());
  expect_within(compare(grid(), got, reference(), 0, grid().size()), grid(), "FastScalar greeks",
                true);
  const auto prices = run_prices(opt::Engine::FastScalar, grid());
  expect_within(compare(grid(), prices, reference(), 0, grid().size(), true), grid(),
                "FastScalar price-only", true, true);
}

TEST(BatchEngine, SimdMatchesReferenceAndFastScalar) {
  const auto simd = run_engine(opt::Engine::Simd, grid());
  expect_within(compare(grid(), simd, reference(), 0, grid().size()), grid(), "Simd greeks", true);
  const auto prices = run_prices(opt::Engine::Simd, grid());
  expect_within(compare(grid(), prices, reference(), 0, grid().size(), true), grid(),
                "Simd price-only", true, true);
  const auto fast = run_engine(opt::Engine::FastScalar, grid());
  const auto fast_prices = run_prices(opt::Engine::FastScalar, grid());
  const std::size_t mismatched = bit_mismatches(simd, fast) + bit_mismatches(prices, fast_prices, true);
  std::printf("[batch] Simd (%s, width %zu) vs FastScalar: %zu of %zu outputs not bit-identical\n",
              std::string(opt::simd::kNativeName).c_str(), opt::simd::kNativeWidth, mismatched,
              7 * grid().size());
  RecordProperty("simd_vs_fastscalar_non_identical", std::to_string(mismatched));
  EXPECT_EQ(mismatched, 0U);
  // Price-only and price+Greeks compute the price the same way.
  EXPECT_EQ(bit_mismatches(prices, simd, true), 0U);
}

template <class V>
class EmulatedKernel : public ::testing::Test {};
using EmulatedWidths = ::testing::Types<opt::simd::Emulated<4>, opt::simd::Emulated<8>>;
TYPED_TEST_SUITE(EmulatedKernel, EmulatedWidths);

TYPED_TEST(EmulatedKernel, MatchesReferenceAndFastScalarBitwise) {
  using V = TypeParam;
  const auto got = run_kernel<V>(grid());
  const std::string label = "Emulated<" + std::to_string(opt::simd::kWidth<V>) + ">";
  expect_within(compare(grid(), got, reference(), 0, grid().size()), grid(), label, true);
  EXPECT_EQ(bit_mismatches(got, run_engine(opt::Engine::FastScalar, grid())), 0U);
  SoaGreeks prices(grid().size());
  opt::kernel::price_range<V>(grid().view(), prices.price.data(), 0, grid().size());
  EXPECT_EQ(bit_mismatches(prices, got, true), 0U);
}

// Sizes 0, 1, W-1, W, W+1, 2W+3 for the native width and 4 and 8, each at
// offset 0 and at odd offsets (unaligned sub-spans); only the requested
// range may be written.
TEST(BatchEngine, SizesAndUnalignedOffsets) {
  const auto& o = grid();
  const auto& ref = reference();
  std::vector<std::size_t> sizes;
  for (const std::size_t w : {opt::simd::kNativeWidth, std::size_t{4}, std::size_t{8}}) {
    for (const std::size_t n : {std::size_t{0}, std::size_t{1}, w - 1, w, w + 1, 2 * w + 3}) {
      sizes.push_back(n);
    }
  }
  for (const std::size_t offset : {std::size_t{0}, std::size_t{1}, std::size_t{3},
                                   std::size_t{1001}}) {
    for (const std::size_t n : sizes) {
      for (const auto engine :
           {opt::Engine::Reference, opt::Engine::FastScalar, opt::Engine::Simd}) {
        SoaGreeks g(o.size());
        opt::greeks_batch(engine, o.view(offset, n), g.view(offset, n));
        SoaGreeks p(o.size());
        opt::price_batch(engine, o.view(offset, n), std::span(p.price).subspan(offset, n));
        const auto label = "engine " + std::to_string(static_cast<int>(engine)) + " offset " +
                           std::to_string(offset) + " n " + std::to_string(n);
        expect_within(compare(o, g, ref, offset, n), o, label);
        expect_within(compare(o, p, ref, offset, n, true), o, label + " price-only", false, true);
        // Neighbours untouched.
        if (offset > 0) {
          EXPECT_EQ(g.price[offset - 1], -1.0) << label;
          EXPECT_EQ(p.price[offset - 1], -1.0) << label;
        }
        EXPECT_EQ(g.rho[offset + n], -1.0) << label;
        EXPECT_EQ(p.price[offset + n], -1.0) << label;
      }
      for (const std::size_t w : {std::size_t{4}, std::size_t{8}}) {
        SoaGreeks g(o.size());
        const auto out = g.view(offset, n);
        const auto in = o.view(offset, n);
        if (w == 4) {
          opt::kernel::greeks_range<opt::simd::Emulated<4>>(in, out, 0, n);
        } else {
          opt::kernel::greeks_range<opt::simd::Emulated<8>>(in, out, 0, n);
        }
        expect_within(compare(o, g, ref, offset, n), o,
                      "Emulated<" + std::to_string(w) + "> offset " + std::to_string(offset) +
                          " n " + std::to_string(n));
        EXPECT_EQ(g.vega[offset + n], -1.0);
      }
    }
  }
}

TEST(BatchEngine, ValidateBatchReturnsFirstInvalidIndex) {
  SoaOptions o = opt::test::random_options(50, 9);
  EXPECT_EQ(opt::validate_batch(o.view()), 50U);
  struct Case {
    std::size_t index;
    void (*apply)(SoaOptions&, std::size_t);
  };
  const Case cases[] = {
      {7, [](SoaOptions& s, std::size_t i) { s.spot[i] = 0.0; }},
      {8, [](SoaOptions& s, std::size_t i) { s.strike[i] = 2e12; }},
      {9, [](SoaOptions& s, std::size_t i) { s.time[i] = 0.0; }},
      {10, [](SoaOptions& s, std::size_t i) { s.time[i] = 101.0; }},
      {11, [](SoaOptions& s, std::size_t i) { s.vol[i] = 0.0; }},
      {12, [](SoaOptions& s, std::size_t i) { s.vol[i] = 11.0; }},
      {13, [](SoaOptions& s, std::size_t i) { s.rate[i] = 1.5; }},
      {14, [](SoaOptions& s, std::size_t i) { s.carry_yield[i] = -1.5; }},
      {15, [](SoaOptions& s, std::size_t i) { s.spot[i] = std::numeric_limits<double>::quiet_NaN(); }},
      {16, [](SoaOptions& s, std::size_t i) { s.vol[i] = std::numeric_limits<double>::infinity(); }},
      {17, [](SoaOptions& s, std::size_t i) { s.type[i] = static_cast<opt::OptionType>(2); }},
  };
  for (const auto& c : cases) {
    SoaOptions bad = o;
    c.apply(bad, c.index);
    c.apply(bad, c.index + 20);  // a later bad option must not be reported first
    EXPECT_EQ(opt::validate_batch(bad.view()), c.index);
  }
  // Domain ends are valid.
  SoaOptions ends;
  ends.push(opt::OptionType::Call, 1e-8, 1e12, 1e-8, -1.0, 1.0, 1e-6);
  ends.push(opt::OptionType::Put, 1e12, 1e-8, 100.0, 1.0, -1.0, 10.0);
  EXPECT_EQ(opt::validate_batch(ends.view()), 2U);
}

TEST(BatchEngine, SizeMismatchThrowsBeforeWork) {
  const SoaOptions o = opt::test::random_options(10, 3);
  std::vector<double> short_out(9, -1.0);
  for (const auto e : {opt::Engine::Reference, opt::Engine::FastScalar, opt::Engine::Simd}) {
    EXPECT_THROW(opt::price_batch(e, o.view(), short_out), std::invalid_argument);
    SoaGreeks g(10);
    auto view = g.view();
    view.gamma = view.gamma.first(9);
    EXPECT_THROW(opt::greeks_batch(e, o.view(), view), std::invalid_argument);
    EXPECT_EQ(g.price[0], -1.0);  // nothing written
  }
  EXPECT_EQ(short_out[0], -1.0);
  opt::OptionBatch bad = o.view();
  bad.vol = bad.vol.first(5);
  EXPECT_THROW((void)opt::validate_batch(bad), std::invalid_argument);
  opt::ThreadedPricer pricer(2);
  EXPECT_THROW(pricer.price(o.view(), short_out), std::invalid_argument);
  try {
    std::vector<double> out(10);
    opt::price_batch(opt::Engine::Simd, bad, out);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find("vol"), std::string::npos) << e.what();
  }
}

}  // namespace
