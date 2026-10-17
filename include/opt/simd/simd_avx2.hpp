#pragma once
// AVX2 + FMA backend: F64x4 wraps __m256d (4 doubles), x86-64 with
// __AVX2__ and __FMA__ defined (e.g. -march=native on a Haswell or newer CPU).
//
// min/max use vminpd/vmaxpd, whose rule (min = a < b ? a : b) is the one the
// scalar backend copies, so double, Emulated and AVX2 agree bit for bit.
#include "opt/simd/simd_scalar.hpp"

#include <immintrin.h>

#include <cstddef>
#include <cstdint>

// GCC defines some intrinsics as macros containing C-style casts at -O0,
// which -Wold-style-cast -Werror rejects in project code. Only the intrinsic
// wrappers below are exempt.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"

namespace opt::simd {

struct F64x4 {
  __m256d v;
};
struct U64x4 {
  __m256i v;
};
struct M64x4 {
  __m256d v;
};

template <>
struct LaneTraits<F64x4> {
  static constexpr std::size_t kWidth = 4;
  using Bits = U64x4;
  using Mask = M64x4;
  static F64x4 broadcast(double x) noexcept { return {_mm256_set1_pd(x)}; }
  static F64x4 load(const double* p) noexcept { return {_mm256_loadu_pd(p)}; }
  static F64x4 load_type(const std::uint8_t* p) noexcept {
    return {_mm256_setr_pd(LaneTraits<double>::load_type(p), LaneTraits<double>::load_type(p + 1),
                           LaneTraits<double>::load_type(p + 2),
                           LaneTraits<double>::load_type(p + 3))};
  }
};

inline void store(double* p, F64x4 a) noexcept {
  _mm256_storeu_pd(p, a.v);
}

[[nodiscard]] inline F64x4 operator+(F64x4 a, F64x4 b) noexcept {
  return {_mm256_add_pd(a.v, b.v)};
}
[[nodiscard]] inline F64x4 operator-(F64x4 a, F64x4 b) noexcept {
  return {_mm256_sub_pd(a.v, b.v)};
}
[[nodiscard]] inline F64x4 operator*(F64x4 a, F64x4 b) noexcept {
  return {_mm256_mul_pd(a.v, b.v)};
}
[[nodiscard]] inline F64x4 operator/(F64x4 a, F64x4 b) noexcept {
  return {_mm256_div_pd(a.v, b.v)};
}
[[nodiscard]] inline F64x4 operator-(F64x4 a) noexcept {
  return {_mm256_xor_pd(a.v, _mm256_set1_pd(-0.0))};
}
// Fused a * b + c, one rounding.
[[nodiscard]] inline F64x4 fma(F64x4 a, F64x4 b, F64x4 c) noexcept {
  return {_mm256_fmadd_pd(a.v, b.v, c.v)};
}
[[nodiscard]] inline F64x4 sqrt(F64x4 a) noexcept {
  return {_mm256_sqrt_pd(a.v)};
}
[[nodiscard]] inline F64x4 min(F64x4 a, F64x4 b) noexcept {
  return {_mm256_min_pd(a.v, b.v)};
}
[[nodiscard]] inline F64x4 max(F64x4 a, F64x4 b) noexcept {
  return {_mm256_max_pd(a.v, b.v)};
}
[[nodiscard]] inline F64x4 abs(F64x4 a) noexcept {
  return {_mm256_andnot_pd(_mm256_set1_pd(-0.0), a.v)};
}

[[nodiscard]] inline M64x4 operator<(F64x4 a, F64x4 b) noexcept {
  return {_mm256_cmp_pd(a.v, b.v, _CMP_LT_OQ)};
}
[[nodiscard]] inline M64x4 operator<=(F64x4 a, F64x4 b) noexcept {
  return {_mm256_cmp_pd(a.v, b.v, _CMP_LE_OQ)};
}
[[nodiscard]] inline M64x4 operator>(F64x4 a, F64x4 b) noexcept {
  return {_mm256_cmp_pd(a.v, b.v, _CMP_GT_OQ)};
}
[[nodiscard]] inline M64x4 operator>=(F64x4 a, F64x4 b) noexcept {
  return {_mm256_cmp_pd(a.v, b.v, _CMP_GE_OQ)};
}
[[nodiscard]] inline M64x4 operator==(F64x4 a, F64x4 b) noexcept {
  return {_mm256_cmp_pd(a.v, b.v, _CMP_EQ_OQ)};
}
// Unordered not-equal: true for NaN lanes, like the scalar operator.
[[nodiscard]] inline M64x4 operator!=(F64x4 a, F64x4 b) noexcept {
  return {_mm256_cmp_pd(a.v, b.v, _CMP_NEQ_UQ)};
}

[[nodiscard]] inline F64x4 select(M64x4 m, F64x4 a, F64x4 b) noexcept {
  return {_mm256_blendv_pd(b.v, a.v, m.v)};
}
[[nodiscard]] inline bool any(M64x4 m) noexcept {
  return _mm256_movemask_pd(m.v) != 0;
}
[[nodiscard]] inline M64x4 mask_and(M64x4 a, M64x4 b) noexcept {
  return {_mm256_and_pd(a.v, b.v)};
}
[[nodiscard]] inline M64x4 mask_or(M64x4 a, M64x4 b) noexcept {
  return {_mm256_or_pd(a.v, b.v)};
}

[[nodiscard]] inline U64x4 to_bits(F64x4 a) noexcept {
  return {_mm256_castpd_si256(a.v)};
}
[[nodiscard]] inline F64x4 from_bits(U64x4 a) noexcept {
  return {_mm256_castsi256_pd(a.v)};
}
[[nodiscard]] inline U64x4 operator<<(U64x4 a, int n) noexcept {
  return {_mm256_sll_epi64(a.v, _mm_cvtsi32_si128(n))};
}
[[nodiscard]] inline U64x4 operator>>(U64x4 a, int n) noexcept {
  return {_mm256_srl_epi64(a.v, _mm_cvtsi32_si128(n))};
}
[[nodiscard]] inline U64x4 operator|(U64x4 a, U64x4 b) noexcept {
  return {_mm256_or_si256(a.v, b.v)};
}
[[nodiscard]] inline U64x4 operator&(U64x4 a, U64x4 b) noexcept {
  return {_mm256_and_si256(a.v, b.v)};
}

}  // namespace opt::simd

#pragma GCC diagnostic pop
