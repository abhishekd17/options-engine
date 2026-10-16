#pragma once
// Random number generation for Monte Carlo and seeded test grids.
//
// SplitMix64 and xoshiro256++ follow the public-domain reference code by
// D. Blackman and S. Vigna (https://prng.di.unimi.it/). xoshiro256++ is fast
// (a few adds, shifts and rotates per output), has a 2^256 - 1 period and
// passes BigCrush; SplitMix64 is used only to expand a 64-bit seed into the
// four state words, as the reference recommends.
#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace opt {

class SplitMix64 {
 public:
  using result_type = std::uint64_t;
  explicit constexpr SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}
  static constexpr result_type min() noexcept { return 0; }
  static constexpr result_type max() noexcept { return std::numeric_limits<result_type>::max(); }
  constexpr result_type operator()() noexcept {
    std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
  }

 private:
  std::uint64_t state_;
};

class Xoshiro256pp {
 public:
  using result_type = std::uint64_t;
  // State seeded from four consecutive SplitMix64(seed) outputs.
  explicit constexpr Xoshiro256pp(std::uint64_t seed) noexcept {
    SplitMix64 sm{seed};
    for (auto& word : s_) {
      word = sm();
    }
  }
  static constexpr result_type min() noexcept { return 0; }
  static constexpr result_type max() noexcept { return std::numeric_limits<result_type>::max(); }
  constexpr result_type operator()() noexcept {
    const std::uint64_t result = std::rotl(s_[0] + s_[3], 23) + s_[0];
    const std::uint64_t t = s_[1] << 17U;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = std::rotl(s_[3], 45);
    return result;
  }

 private:
  std::array<std::uint64_t, 4> s_{};
};

// Independent stream `stream_id` of a seeded family: a generator seeded with
// SplitMix64-expanded `seed ^ stream_id`.
[[nodiscard]] constexpr Xoshiro256pp make_stream(std::uint64_t seed,
                                                 std::uint64_t stream_id) noexcept {
  return Xoshiro256pp{seed ^ stream_id};
}

// Uniform double strictly inside (0, 1): the top 52 bits plus one half, times
// 2^-52. The smallest value is 2^-53 and the largest 1 - 2^-53, so log() and
// the inverse normal CDF never see 0 or 1. Every step is exact.
//
// Why 52 bits, not 53: with 53 bits, (2^53 - 1) + 0.5 needs 54 significant
// bits, rounds (ties to even) to 2^53, and the result is exactly 1.0 for the
// largest input. With 52 bits, 2^52 - 1 + 0.5 fits in 53 bits.
template <class Rng>
[[nodiscard]] constexpr double uniform01(Rng& rng) noexcept {
  return (static_cast<double>(rng() >> 12U) + 0.5) * 0x1.0p-52;
}

// Uniform double in (lo, hi).
template <class Rng>
[[nodiscard]] constexpr double uniform(Rng& rng, double lo, double hi) noexcept {
  return lo + (hi - lo) * uniform01(rng);
}

}  // namespace opt
