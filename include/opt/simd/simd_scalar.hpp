#pragma once
// Lane operations for plain `double` (width 1): the fast-scalar engine and the
// fallback backend when no vector backend is compiled in.
//
// This header also declares the per-type traits (LaneTraits) and the generic
// wrappers broadcast<V>, load<V>, load_type<V> that every backend uses, so it is
// always included first (simd.hpp does that).
//
// min/max follow the x86 rule (min(a, b) = a < b ? a : b, max(a, b) =
// a > b ? a : b), so double, Emulated and AVX2 agree bit for bit. NEON's
// vminq/vmaxq agree for every input except NaN (NEON returns NaN; the x86 rule
// returns b) and min(-0, +0) (NEON returns -0). The kernels never feed those
// cases to min, and fast_exp overrides a NaN input at the end.
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace opt::simd {

// Width, integer-lane type, mask type and the broadcast/load operations of a
// lane type V. Specialised by each backend.
template <class V>
struct LaneTraits;

template <class V>
using Bits = typename LaneTraits<V>::Bits;
template <class V>
using Mask = typename LaneTraits<V>::Mask;
template <class V>
inline constexpr std::size_t kWidth = LaneTraits<V>::kWidth;

// A one-byte enum such as opt::OptionType (Call = 0, Put = 1).
template <class E>
concept ByteEnum = std::is_enum_v<E> && sizeof(E) == 1;

template <class V>
[[nodiscard]] inline V broadcast(double x) noexcept {
  return LaneTraits<V>::broadcast(x);
}
// Unaligned load of kWidth<V> doubles.
template <class V>
[[nodiscard]] inline V load(const double* p) noexcept {
  return LaneTraits<V>::load(p);
}
// Loads kWidth<V> option types and returns omega = 1 - 2 * type per lane
// (+1 for a call, -1 for a put).
template <class V, ByteEnum E>
[[nodiscard]] inline V load_type(const E* p) noexcept {
  return LaneTraits<V>::load_type(reinterpret_cast<const std::uint8_t*>(p));
}

template <>
struct LaneTraits<double> {
  static constexpr std::size_t kWidth = 1;
  using Bits = std::uint64_t;
  using Mask = bool;
  static double broadcast(double x) noexcept { return x; }
  static double load(const double* p) noexcept { return *p; }
  static double load_type(const std::uint8_t* p) noexcept {
    return 1.0 - 2.0 * static_cast<double>(*p);
  }
};

inline void store(double* p, double v) noexcept {
  *p = v;
}
[[nodiscard]] inline double fma(double a, double b, double c) noexcept {
  return std::fma(a, b, c);
}
[[nodiscard]] inline double sqrt(double a) noexcept {
  return std::sqrt(a);
}
[[nodiscard]] inline double min(double a, double b) noexcept {
  return a < b ? a : b;
}
[[nodiscard]] inline double max(double a, double b) noexcept {
  return a > b ? a : b;
}
[[nodiscard]] inline double abs(double a) noexcept {
  return std::fabs(a);
}
[[nodiscard]] inline double select(bool m, double a, double b) noexcept {
  return m ? a : b;
}
[[nodiscard]] inline bool any(bool m) noexcept {
  return m;
}
[[nodiscard]] inline bool mask_and(bool a, bool b) noexcept {
  return a && b;
}
[[nodiscard]] inline bool mask_or(bool a, bool b) noexcept {
  return a || b;
}
[[nodiscard]] inline std::uint64_t to_bits(double a) noexcept {
  return std::bit_cast<std::uint64_t>(a);
}
[[nodiscard]] inline double from_bits(std::uint64_t a) noexcept {
  return std::bit_cast<double>(a);
}

}  // namespace opt::simd
