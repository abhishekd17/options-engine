#pragma once
// Test-only backend: Emulated<W> holds W doubles in a std::array and applies
// every operation lane by lane with the scalar rules of simd_scalar.hpp. It
// lets the tests run the kernels at width 4 and 8 (and their n % W tails) on
// any machine, including arm64 where the native width is 2.
#include "opt/simd/simd_scalar.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opt::simd {

template <std::size_t W>
struct Emulated {
  std::array<double, W> v;
};
template <std::size_t W>
struct EmulatedBits {
  std::array<std::uint64_t, W> v;
};
template <std::size_t W>
struct EmulatedMask {
  std::array<bool, W> v;
};

template <std::size_t W>
struct LaneTraits<Emulated<W>> {
  static constexpr std::size_t kWidth = W;
  using Bits = EmulatedBits<W>;
  using Mask = EmulatedMask<W>;
  static Emulated<W> broadcast(double x) noexcept {
    Emulated<W> r;
    r.v.fill(x);
    return r;
  }
  static Emulated<W> load(const double* p) noexcept {
    Emulated<W> r;
    std::memcpy(r.v.data(), p, W * sizeof(double));
    return r;
  }
  static Emulated<W> load_type(const std::uint8_t* p) noexcept {
    Emulated<W> r;
    for (std::size_t i = 0; i < W; ++i) {
      r.v[i] = LaneTraits<double>::load_type(p + i);
    }
    return r;
  }
};

namespace detail {
template <std::size_t W, class Fn>
[[nodiscard]] inline Emulated<W> map(Fn fn) noexcept {
  Emulated<W> r;
  for (std::size_t i = 0; i < W; ++i) {
    r.v[i] = fn(i);
  }
  return r;
}
template <std::size_t W, class Fn>
[[nodiscard]] inline EmulatedMask<W> map_mask(Fn fn) noexcept {
  EmulatedMask<W> r;
  for (std::size_t i = 0; i < W; ++i) {
    r.v[i] = fn(i);
  }
  return r;
}
template <std::size_t W, class Fn>
[[nodiscard]] inline EmulatedBits<W> map_bits(Fn fn) noexcept {
  EmulatedBits<W> r;
  for (std::size_t i = 0; i < W; ++i) {
    r.v[i] = fn(i);
  }
  return r;
}
}  // namespace detail

template <std::size_t W>
inline void store(double* p, Emulated<W> a) noexcept {
  std::memcpy(p, a.v.data(), W * sizeof(double));
}

// Arithmetic.
template <std::size_t W>
[[nodiscard]] inline Emulated<W> operator+(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map<W>([&](std::size_t i) { return a.v[i] + b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> operator-(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map<W>([&](std::size_t i) { return a.v[i] - b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> operator*(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map<W>([&](std::size_t i) { return a.v[i] * b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> operator/(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map<W>([&](std::size_t i) { return a.v[i] / b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> operator-(Emulated<W> a) noexcept {
  return detail::map<W>([&](std::size_t i) { return -a.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> fma(Emulated<W> a, Emulated<W> b, Emulated<W> c) noexcept {
  return detail::map<W>([&](std::size_t i) { return simd::fma(a.v[i], b.v[i], c.v[i]); });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> sqrt(Emulated<W> a) noexcept {
  return detail::map<W>([&](std::size_t i) { return simd::sqrt(a.v[i]); });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> min(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map<W>([&](std::size_t i) { return simd::min(a.v[i], b.v[i]); });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> max(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map<W>([&](std::size_t i) { return simd::max(a.v[i], b.v[i]); });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> abs(Emulated<W> a) noexcept {
  return detail::map<W>([&](std::size_t i) { return simd::abs(a.v[i]); });
}

// Comparisons.
template <std::size_t W>
[[nodiscard]] inline EmulatedMask<W> operator<(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map_mask<W>([&](std::size_t i) { return a.v[i] < b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedMask<W> operator<=(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map_mask<W>([&](std::size_t i) { return a.v[i] <= b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedMask<W> operator>(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map_mask<W>([&](std::size_t i) { return a.v[i] > b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedMask<W> operator>=(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map_mask<W>([&](std::size_t i) { return a.v[i] >= b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedMask<W> operator==(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map_mask<W>([&](std::size_t i) { return a.v[i] == b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedMask<W> operator!=(Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map_mask<W>([&](std::size_t i) { return a.v[i] != b.v[i]; });
}

// Masks.
template <std::size_t W>
[[nodiscard]] inline Emulated<W> select(EmulatedMask<W> m, Emulated<W> a, Emulated<W> b) noexcept {
  return detail::map<W>([&](std::size_t i) { return m.v[i] ? a.v[i] : b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline bool any(EmulatedMask<W> m) noexcept {
  for (const bool b : m.v) {
    if (b) {
      return true;
    }
  }
  return false;
}
template <std::size_t W>
[[nodiscard]] inline EmulatedMask<W> mask_and(EmulatedMask<W> a, EmulatedMask<W> b) noexcept {
  return detail::map_mask<W>([&](std::size_t i) { return a.v[i] && b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedMask<W> mask_or(EmulatedMask<W> a, EmulatedMask<W> b) noexcept {
  return detail::map_mask<W>([&](std::size_t i) { return a.v[i] || b.v[i]; });
}

// Bit-level helpers.
template <std::size_t W>
[[nodiscard]] inline EmulatedBits<W> to_bits(Emulated<W> a) noexcept {
  return detail::map_bits<W>([&](std::size_t i) { return simd::to_bits(a.v[i]); });
}
template <std::size_t W>
[[nodiscard]] inline Emulated<W> from_bits(EmulatedBits<W> a) noexcept {
  return detail::map<W>([&](std::size_t i) { return simd::from_bits(a.v[i]); });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedBits<W> operator<<(EmulatedBits<W> a, int n) noexcept {
  return detail::map_bits<W>([&](std::size_t i) { return a.v[i] << n; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedBits<W> operator>>(EmulatedBits<W> a, int n) noexcept {
  return detail::map_bits<W>([&](std::size_t i) { return a.v[i] >> n; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedBits<W> operator|(EmulatedBits<W> a, EmulatedBits<W> b) noexcept {
  return detail::map_bits<W>([&](std::size_t i) { return a.v[i] | b.v[i]; });
}
template <std::size_t W>
[[nodiscard]] inline EmulatedBits<W> operator&(EmulatedBits<W> a, EmulatedBits<W> b) noexcept {
  return detail::map_bits<W>([&](std::size_t i) { return a.v[i] & b.v[i]; });
}

}  // namespace opt::simd
