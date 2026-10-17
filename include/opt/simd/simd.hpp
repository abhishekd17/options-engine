#pragma once
// Portable SIMD layer: one small value type per backend, all with the same
// free-function interface, selected at compile time.
//
//   __AVX2__ && __FMA__  -> F64x4 (simd_avx2.hpp), width 4
//   __aarch64__          -> F64x2 (simd_neon.hpp), width 2
//   otherwise            -> double (simd_scalar.hpp), width 1
//
// Emulated<W> (simd_emulated.hpp) is a test-only backend so width 4 and 8 code
// paths run on every machine.
//
// There is deliberately no rounding intrinsic and no double <-> int64 lane
// conversion: AVX2 has no int64 conversion, so fast_math.hpp builds rounding,
// 2^n and exponent extraction from bit operations, identically for every
// backend.
#include "opt/simd/simd_emulated.hpp"
#include "opt/simd/simd_scalar.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

#if defined(__AVX2__) && defined(__FMA__)
#include "opt/simd/simd_avx2.hpp"
#elif defined(__aarch64__)
#include "opt/simd/simd_neon.hpp"
#endif

namespace opt::simd {

#if defined(__AVX2__) && defined(__FMA__)
using Native = F64x4;
inline constexpr std::string_view kNativeName = "avx2+fma";
#elif defined(__aarch64__)
using Native = F64x2;
inline constexpr std::string_view kNativeName = "neon";
#else
using Native = double;
inline constexpr std::string_view kNativeName = "scalar";
#endif
inline constexpr std::size_t kNativeWidth = kWidth<Native>;

enum class TestByteEnum : std::uint8_t {};

// Every operation the kernels and fast_math.hpp use.
template <class V>
concept Lane = requires(V a, V b, V c, double* out, const double* in, Mask<V> m, Bits<V> u,
                        const TestByteEnum* types) {
  { kWidth<V> } -> std::convertible_to<std::size_t>;
  { broadcast<V>(1.0) } -> std::same_as<V>;
  { load<V>(in) } -> std::same_as<V>;
  { load_type<V>(types) } -> std::same_as<V>;
  store(out, a);
  { a + b } -> std::same_as<V>;
  { a - b } -> std::same_as<V>;
  { a * b } -> std::same_as<V>;
  { a / b } -> std::same_as<V>;
  { -a } -> std::same_as<V>;
  { fma(a, b, c) } -> std::same_as<V>;
  { sqrt(a) } -> std::same_as<V>;
  { min(a, b) } -> std::same_as<V>;
  { max(a, b) } -> std::same_as<V>;
  { abs(a) } -> std::same_as<V>;
  { a < b } -> std::same_as<Mask<V>>;
  { a <= b } -> std::same_as<Mask<V>>;
  { a > b } -> std::same_as<Mask<V>>;
  { a >= b } -> std::same_as<Mask<V>>;
  { a == b } -> std::same_as<Mask<V>>;
  { a != b } -> std::same_as<Mask<V>>;
  { select(m, a, b) } -> std::same_as<V>;
  { any(m) } -> std::same_as<bool>;
  { mask_and(m, m) } -> std::same_as<Mask<V>>;
  { mask_or(m, m) } -> std::same_as<Mask<V>>;
  { to_bits(a) } -> std::same_as<Bits<V>>;
  { from_bits(u) } -> std::same_as<V>;
  { u << 52 } -> std::same_as<Bits<V>>;
  { u >> 52 } -> std::same_as<Bits<V>>;
  { u | u } -> std::same_as<Bits<V>>;
  { u & u } -> std::same_as<Bits<V>>;
};

static_assert(Lane<double>);
static_assert(Lane<Emulated<4>>);
static_assert(Lane<Emulated<8>>);
static_assert(Lane<Native>);

}  // namespace opt::simd
