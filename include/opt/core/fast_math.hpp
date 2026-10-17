#pragma once
// Project-owned fast approximations of exp, log and the normal CDF/PDF.
//
// Every function is a template on a lane type V (double or an opt::simd
// vector), so the same source text is the fast-scalar and the SIMD
// implementation. Only IEEE-754 correctly rounded operations are used
// (+ - * / sqrt fma, bit casts, round to nearest even), so the results are
// bit-identical on every backend, platform and compiler, unlike libm.
//
// "Fast math" means these approximations. The compiler flag -ffast-math is
// never used: it breaks NaN checks and makes results depend on the compiler.
//
// Measured accuracy (tools/fast_math_error, docs/accuracy.md) and the bounds
// asserted by tests/core/fast_math_test.cpp are documented there.
#include "opt/core/normal.hpp"
#include "opt/simd/simd.hpp"

#include <bit>
#include <cstdint>
#include <limits>

// The kernels call these several times per option; forcing inlining lets the
// compiler interleave the independent calls.
#if defined(__GNUC__) || defined(__clang__)
#define OPTENG_FM_INLINE inline __attribute__((always_inline))
#else
#define OPTENG_FM_INLINE inline
#endif

namespace opt {

namespace fm {

// 1.5 * 2^52: adding and subtracting it rounds |x| < 2^51 to the nearest
// integer (ties to even under the default rounding mode).
inline constexpr double kShift = 0x1.8p52;
inline constexpr double kInvLn2 = 1.44269504088896338700e+00;
// Cody-Waite split of ln 2 (fdlibm): kLn2Hi has its low 32 mantissa bits
// zero, so n * kLn2Hi is exact for |n| < 2^20.
inline constexpr double kLn2Hi = 6.93147180369123816490e-01;
inline constexpr double kLn2Lo = 1.90821492927058770002e-10;
inline constexpr double kSqrt2 = 1.41421356237309504880;
inline constexpr std::uint64_t kMantissaMask = 0x000F'FFFF'FFFF'FFFFULL;

template <class V>
[[nodiscard]] inline V bcast(double x) noexcept {
  return simd::broadcast<V>(x);
}

// Nearest integer of x, |x| < 2^51.
template <class V>
[[nodiscard]] inline V round_nearest(V x) noexcept {
  return (x + bcast<V>(kShift)) - bcast<V>(kShift);
}

// 2^n for an integer-valued n in [-1022, 1023]. n + kShift + 1023 is an
// integer whose low mantissa bits hold n + 1023; shifting left by 52 moves
// them into the exponent field and drops everything else.
template <class V>
[[nodiscard]] inline V pow2i(V n) noexcept {
  const V t = n + bcast<V>(kShift + 1023.0);
  return simd::from_bits(simd::to_bits(t) << 52);
}

// Unbiased exponent of a positive normal x, as a double.
template <class V>
[[nodiscard]] inline V exponent_of(V x) noexcept {
  const auto e_bits = (simd::to_bits(x) >> 52) | simd::to_bits(bcast<V>(0x1.0p52));
  return simd::from_bits(e_bits) - bcast<V>(0x1.0p52 + 1023.0);
}

}  // namespace fm

// e^x. x >= 709.79 gives +inf, x <= -745.2 gives +0, NaN gives NaN (the
// same as std::exp). Elsewhere: x = n ln2 + r with |r| <= ln2/2 (Cody-Waite),
// e^r by a degree-13 Taylor polynomial (Horner, fma), then two exact scalings
// 2^n1 * 2^n2 so no factor leaves the normal range and a subnormal result is
// rounded once.
template <class V>
[[nodiscard]] OPTENG_FM_INLINE V fast_exp(V x) noexcept {
  using fm::bcast;
  const V xc = simd::min(simd::max(x, bcast<V>(-746.0)), bcast<V>(710.0));
  const V n = fm::round_nearest(xc * bcast<V>(fm::kInvLn2));
  V r = simd::fma(-n, bcast<V>(fm::kLn2Hi), xc);
  r = simd::fma(-n, bcast<V>(fm::kLn2Lo), r);
  V p = bcast<V>(1.0 / 6227020800.0);  // 1/13!
  p = simd::fma(p, r, bcast<V>(1.0 / 479001600.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 39916800.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 3628800.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 362880.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 40320.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 5040.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 720.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 120.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 24.0));
  p = simd::fma(p, r, bcast<V>(1.0 / 6.0));
  p = simd::fma(p, r, bcast<V>(0.5));
  p = simd::fma(p, r, bcast<V>(1.0));
  p = simd::fma(p, r, bcast<V>(1.0));
  const V n1 = fm::round_nearest(n * bcast<V>(0.5));
  const V n2 = n - n1;
  V res = (p * fm::pow2i(n1)) * fm::pow2i(n2);
  res = simd::select(x >= bcast<V>(709.79), bcast<V>(std::numeric_limits<double>::infinity()),
                     res);
  res = simd::select(x <= bcast<V>(-745.2), bcast<V>(0.0), res);
  return simd::select(x != x, x, res);
}

// ln x. Precondition: x positive, finite and normal. x = 2^e m with m in
// [sqrt(1/2), sqrt(2)); ln m = 2 atanh(s), s = (m-1)/(m+1), |s| <= 0.1716,
// as the odd series s + s^3/3 + ... + s^21/21 (first omitted term ~2e-19).
template <class V>
[[nodiscard]] OPTENG_FM_INLINE V fast_log(V x) noexcept {
  using fm::bcast;
  V e = fm::exponent_of(x);
  V m = simd::from_bits((simd::to_bits(x) & simd::to_bits(bcast<V>(std::bit_cast<double>(
                                                fm::kMantissaMask)))) |
                        simd::to_bits(bcast<V>(1.0)));
  const auto big = m >= bcast<V>(fm::kSqrt2);
  m = simd::select(big, m * bcast<V>(0.5), m);
  e = simd::select(big, e + bcast<V>(1.0), e);
  const V s = (m - bcast<V>(1.0)) / (m + bcast<V>(1.0));
  const V z = s * s;
  V p = bcast<V>(1.0 / 21.0);
  p = simd::fma(p, z, bcast<V>(1.0 / 19.0));
  p = simd::fma(p, z, bcast<V>(1.0 / 17.0));
  p = simd::fma(p, z, bcast<V>(1.0 / 15.0));
  p = simd::fma(p, z, bcast<V>(1.0 / 13.0));
  p = simd::fma(p, z, bcast<V>(1.0 / 11.0));
  p = simd::fma(p, z, bcast<V>(1.0 / 9.0));
  p = simd::fma(p, z, bcast<V>(1.0 / 7.0));
  p = simd::fma(p, z, bcast<V>(1.0 / 5.0));
  p = simd::fma(p, z, bcast<V>(1.0 / 3.0));
  const V two_s = s + s;
  const V log_m = simd::fma(two_s, z * p, two_s);
  // e * kLn2Hi is exact, so this is e ln2_hi + (ln m + e ln2_lo) rounded once.
  return simd::fma(e, bcast<V>(fm::kLn2Hi), simd::fma(e, bcast<V>(fm::kLn2Lo), log_m));
}

// Standard normal CDF N(x): Hart (1968) algorithm 5666 as published by
// G. West, "Better approximations to cumulative normal functions", Wilmott
// Magazine, 2005 (figure 2). For |x| < 7.07106781186547 a degree 6/7 rational
// in |x| times e^{-x^2/2}; for 7.07 <= |x| <= 37 West's continued fraction
// (computed only if any lane needs it); beyond 37 the lower tail is 0.
// Reflection N(x) = 1 - N(-x) for x > 0. Coefficients transcribed from the
// paper. This approximation is accurate in absolute terms (~1e-16) but its
// relative error in the lower tail grows to ~1e-8 (docs/accuracy.md).
template <class V>
[[nodiscard]] OPTENG_FM_INLINE V fast_norm_cdf(V x) noexcept {
  using fm::bcast;
  const V ax = simd::abs(x);
  const V e = fast_exp((ax * ax) * bcast<V>(-0.5));
  V num = bcast<V>(3.52624965998911e-02);
  num = simd::fma(num, ax, bcast<V>(0.700383064443688));
  num = simd::fma(num, ax, bcast<V>(6.37396220353165));
  num = simd::fma(num, ax, bcast<V>(33.912866078383));
  num = simd::fma(num, ax, bcast<V>(112.079291497871));
  num = simd::fma(num, ax, bcast<V>(221.213596169931));
  num = simd::fma(num, ax, bcast<V>(220.206867912376));
  V den = bcast<V>(8.83883476483184e-02);
  den = simd::fma(den, ax, bcast<V>(1.75566716318264));
  den = simd::fma(den, ax, bcast<V>(16.064177579207));
  den = simd::fma(den, ax, bcast<V>(86.7807322029461));
  den = simd::fma(den, ax, bcast<V>(296.564248779674));
  den = simd::fma(den, ax, bcast<V>(637.333633378831));
  den = simd::fma(den, ax, bcast<V>(793.826512519948));
  den = simd::fma(den, ax, bcast<V>(440.413735824752));
  V c = (e * num) / den;
  const auto tail = ax >= bcast<V>(7.07106781186547);
  if (simd::any(tail)) {
    V b = ax + bcast<V>(0.65);
    b = ax + bcast<V>(4.0) / b;
    b = ax + bcast<V>(3.0) / b;
    b = ax + bcast<V>(2.0) / b;
    b = ax + bcast<V>(1.0) / b;
    c = simd::select(tail, (e / b) / bcast<V>(2.506628274631), c);
  }
  c = simd::select(ax > bcast<V>(37.0), bcast<V>(0.0), c);
  return simd::select(x > bcast<V>(0.0), bcast<V>(1.0) - c, c);
}

// Standard normal density, kInvSqrt2Pi e^{-x^2/2} (same operation order as
// norm_pdf).
template <class V>
[[nodiscard]] OPTENG_FM_INLINE V fast_norm_pdf(V x) noexcept {
  return fm::bcast<V>(kInvSqrt2Pi) * fast_exp((fm::bcast<V>(-0.5) * x) * x);
}

// Inverse normal CDF: the AS241 body of norm_inv_cdf with fast_log. Not a
// lane template (AS241 branches per value). Precondition: 0 < p < 1 with p
// and 1 - p normal (p >= 2.3e-308).
[[nodiscard]] inline double fast_norm_inv(double p) noexcept {
  return detail::as241(p, [](double v) noexcept { return fast_log(v); });
}

}  // namespace opt
