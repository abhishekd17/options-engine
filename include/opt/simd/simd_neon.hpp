#pragma once
// NEON backend: F64x2 wraps float64x2_t (2 doubles), aarch64 only.
//
// Shift counts are passed as a register (vshlq_u64 with a duplicated count;
// a negative count shifts right, logically for unsigned lanes), so the
// operators accept a plain int. With a constant count the compiler emits the
// immediate form.
#include "opt/simd/simd_scalar.hpp"

#include <arm_neon.h>

#include <cstddef>
#include <cstdint>

// GCC defines some NEON intrinsics as macros containing C-style casts at -O0,
// which -Wold-style-cast -Werror rejects in project code. Only the intrinsic
// wrappers below are exempt.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"

namespace opt::simd {

struct F64x2 {
  float64x2_t v;
};
struct U64x2 {
  uint64x2_t v;
};
struct M64x2 {
  uint64x2_t v;
};

template <>
struct LaneTraits<F64x2> {
  static constexpr std::size_t kWidth = 2;
  using Bits = U64x2;
  using Mask = M64x2;
  static F64x2 broadcast(double x) noexcept { return {vdupq_n_f64(x)}; }
  static F64x2 load(const double* p) noexcept { return {vld1q_f64(p)}; }
  static F64x2 load_type(const std::uint8_t* p) noexcept {
    float64x2_t r = vdupq_n_f64(LaneTraits<double>::load_type(p));
    r = vsetq_lane_f64(LaneTraits<double>::load_type(p + 1), r, 1);
    return {r};
  }
};

inline void store(double* p, F64x2 a) noexcept {
  vst1q_f64(p, a.v);
}

[[nodiscard]] inline F64x2 operator+(F64x2 a, F64x2 b) noexcept {
  return {vaddq_f64(a.v, b.v)};
}
[[nodiscard]] inline F64x2 operator-(F64x2 a, F64x2 b) noexcept {
  return {vsubq_f64(a.v, b.v)};
}
[[nodiscard]] inline F64x2 operator*(F64x2 a, F64x2 b) noexcept {
  return {vmulq_f64(a.v, b.v)};
}
[[nodiscard]] inline F64x2 operator/(F64x2 a, F64x2 b) noexcept {
  return {vdivq_f64(a.v, b.v)};
}
[[nodiscard]] inline F64x2 operator-(F64x2 a) noexcept {
  return {vnegq_f64(a.v)};
}
// Fused a * b + c, one rounding.
[[nodiscard]] inline F64x2 fma(F64x2 a, F64x2 b, F64x2 c) noexcept {
  return {vfmaq_f64(c.v, a.v, b.v)};
}
[[nodiscard]] inline F64x2 sqrt(F64x2 a) noexcept {
  return {vsqrtq_f64(a.v)};
}
[[nodiscard]] inline F64x2 min(F64x2 a, F64x2 b) noexcept {
  return {vminq_f64(a.v, b.v)};
}
[[nodiscard]] inline F64x2 max(F64x2 a, F64x2 b) noexcept {
  return {vmaxq_f64(a.v, b.v)};
}
[[nodiscard]] inline F64x2 abs(F64x2 a) noexcept {
  return {vabsq_f64(a.v)};
}

[[nodiscard]] inline M64x2 operator<(F64x2 a, F64x2 b) noexcept {
  return {vcltq_f64(a.v, b.v)};
}
[[nodiscard]] inline M64x2 operator<=(F64x2 a, F64x2 b) noexcept {
  return {vcleq_f64(a.v, b.v)};
}
[[nodiscard]] inline M64x2 operator>(F64x2 a, F64x2 b) noexcept {
  return {vcgtq_f64(a.v, b.v)};
}
[[nodiscard]] inline M64x2 operator>=(F64x2 a, F64x2 b) noexcept {
  return {vcgeq_f64(a.v, b.v)};
}
[[nodiscard]] inline M64x2 operator==(F64x2 a, F64x2 b) noexcept {
  return {vceqq_f64(a.v, b.v)};
}
// True where a != b, including NaN lanes (same as the scalar operator).
[[nodiscard]] inline M64x2 operator!=(F64x2 a, F64x2 b) noexcept {
  return {vreinterpretq_u64_u32(vmvnq_u32(vreinterpretq_u32_u64(vceqq_f64(a.v, b.v))))};
}

[[nodiscard]] inline F64x2 select(M64x2 m, F64x2 a, F64x2 b) noexcept {
  return {vbslq_f64(m.v, a.v, b.v)};
}
[[nodiscard]] inline bool any(M64x2 m) noexcept {
  return vmaxvq_u32(vreinterpretq_u32_u64(m.v)) != 0;
}
[[nodiscard]] inline M64x2 mask_and(M64x2 a, M64x2 b) noexcept {
  return {vandq_u64(a.v, b.v)};
}
[[nodiscard]] inline M64x2 mask_or(M64x2 a, M64x2 b) noexcept {
  return {vorrq_u64(a.v, b.v)};
}

[[nodiscard]] inline U64x2 to_bits(F64x2 a) noexcept {
  return {vreinterpretq_u64_f64(a.v)};
}
[[nodiscard]] inline F64x2 from_bits(U64x2 a) noexcept {
  return {vreinterpretq_f64_u64(a.v)};
}
[[nodiscard]] inline U64x2 operator<<(U64x2 a, int n) noexcept {
  return {vshlq_u64(a.v, vdupq_n_s64(n))};
}
[[nodiscard]] inline U64x2 operator>>(U64x2 a, int n) noexcept {
  return {vshlq_u64(a.v, vdupq_n_s64(-n))};
}
[[nodiscard]] inline U64x2 operator|(U64x2 a, U64x2 b) noexcept {
  return {vorrq_u64(a.v, b.v)};
}
[[nodiscard]] inline U64x2 operator&(U64x2 a, U64x2 b) noexcept {
  return {vandq_u64(a.v, b.v)};
}

}  // namespace opt::simd

#pragma GCC diagnostic pop
