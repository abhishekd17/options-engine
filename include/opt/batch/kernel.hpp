#pragma once
// The shared batch BSM kernel, a template on the lane type V (double, an
// opt::simd vector, or Emulated<W> in tests).
//
// It computes the same formulas as opt::bsm_greeks, in the same operation
// order, with fast_exp / fast_log / fast_norm_cdf / fast_norm_pdf in place
// of libm:
//   F = S e^{(r-q)T}, D = e^{-rT}, Dq = e^{-qT}, sd = sigma sqrt T,
//   d1 = (ln(F/K) + sd^2/2) / sd, d2 = d1 - sd, omega = +1 call / -1 put,
//   price = D max(omega (F N(omega d1) - K N(omega d2)), 0),
//   delta = omega Dq N(omega d1),  gamma = Dq phi(d1) / (S sd),
//   vega  = S Dq phi(d1) sqrt T,   rho = omega K T D N(omega d2),
//   theta = -S Dq phi(d1) sigma / (2 sqrt T)
//           - omega r K D N(omega d2) + omega q S Dq N(omega d1).
// The batch domain (batch.hpp) keeps T > 0 and sigma > 0, so there are no
// special lanes. Price-only skips every Greek term.
//
// Staged tiles: the range is cut into tiles of kTile options, and each tile
// runs the formula in stages (exponentials, then d1/d2, then N(d1), then
// N(d2) and the outputs), keeping intermediates in small stack arrays. Each
// fast_exp / fast_log / fast_norm_cdf is a long chain of dependent fma; a
// stage gives the CPU kTile / W independent chains to overlap, where a
// one-option-at-a-time loop gives it one or two. Within a tile, full V vectors
// run first and the tile's last (size % W) options run the double kernel.
// Every option goes through exactly the same operations whatever the tiling
// or lane type, so results do not depend on either. No heap allocation.
#include "opt/batch/batch.hpp"
#include "opt/core/fast_math.hpp"
#include "opt/simd/simd.hpp"

#include <algorithm>
#include <cstddef>

#if defined(__GNUC__) || defined(__clang__)
#define OPTENG_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define OPTENG_ALWAYS_INLINE inline
#endif

#if defined(__clang__)
#define OPTENG_NO_VECTORIZE _Pragma("clang loop vectorize(disable)")
#elif defined(__GNUC__) && __GNUC__ >= 14
#define OPTENG_NO_VECTORIZE _Pragma("GCC novector")
#else
#define OPTENG_NO_VECTORIZE
#endif

namespace opt::kernel {

// Options per tile: a multiple of every lane width (1, 2, 4, 8).
inline constexpr std::size_t kTile = 64;

// Calls fn.template operator()<V>(j) for j = 0, W, 2W, ... over the full
// vectors of [0, m), then fn.template operator()<double>(j) for the rest.
template <class V, class Fn>
OPTENG_ALWAYS_INLINE void for_lanes(std::size_t m, Fn&& fn) {
  constexpr std::size_t kW = simd::kWidth<V>;
  std::size_t j = 0;
  if constexpr (kW > 1) {
    for (; j + kW <= m; j += kW) {
      fn.template operator()<V>(j);
    }
  }
  // The double path stays scalar: without this the compiler auto-vectorizes
  // the FastScalar stages, and the benchmark would no longer show the cost
  // of one option at a time. (Results are the same either way.)
  OPTENG_NO_VECTORIZE
  for (; j < m; ++j) {
    fn.template operator()<double>(j);
  }
}

template <class L>
[[nodiscard]] OPTENG_ALWAYS_INLINE L ld(const double* p) noexcept {
  return simd::load<L>(p);
}

// Per-tile intermediates (stack, about 5 KB).
struct Scratch {
  alignas(64) double fwd[kTile];
  alignas(64) double disc[kTile];
  alignas(64) double disc_q[kTile];
  alignas(64) double sqrt_t[kTile];
  alignas(64) double sd[kTile];
  alignas(64) double d1[kTile];
  alignas(64) double x1[kTile];  // omega d1
  alignas(64) double x2[kTile];  // omega d2
  alignas(64) double nd1[kTile];
};

// Stages shared by price-only and price+Greeks for the tile starting at
// option i0 with m options. `greeks` adds Dq and sqrt T.
template <class V, bool kGreeks>
OPTENG_ALWAYS_INLINE void common_stages(const OptionBatch& in, std::size_t i0, std::size_t m,
                                        Scratch& s) noexcept {
  // Stage 1: forward, discount factors, sd.
  for_lanes<V>(m, [&]<class L>(std::size_t j) {
    const std::size_t i = i0 + j;
    const L t = ld<L>(in.time.data() + i);
    const L r = ld<L>(in.rate.data() + i);
    const L q = ld<L>(in.carry_yield.data() + i);
    simd::store(s.fwd + j, ld<L>(in.spot.data() + i) * fast_exp((r - q) * t));
    simd::store(s.disc + j, fast_exp(-r * t));
    const L sqrt_t = simd::sqrt(t);
    if constexpr (kGreeks) {
      simd::store(s.disc_q + j, fast_exp(-q * t));
      simd::store(s.sqrt_t + j, sqrt_t);
    }
    simd::store(s.sd + j, ld<L>(in.vol.data() + i) * sqrt_t);
  });
  // Stage 2: d1, d2 (times omega).
  for_lanes<V>(m, [&]<class L>(std::size_t j) {
    const std::size_t i = i0 + j;
    const L w = simd::load_type<L>(in.type.data() + i);
    const L sd = ld<L>(s.sd + j);
    const L d1 = (fast_log(ld<L>(s.fwd + j) / ld<L>(in.strike.data() + i)) +
                  (fm::bcast<L>(0.5) * sd) * sd) /
                 sd;
    if constexpr (kGreeks) {
      simd::store(s.d1 + j, d1);
    }
    simd::store(s.x1 + j, w * d1);
    simd::store(s.x2 + j, w * (d1 - sd));
  });
  // Stage 3: N(omega d1).
  for_lanes<V>(m, [&]<class L>(std::size_t j) {
    simd::store(s.nd1 + j, fast_norm_cdf(ld<L>(s.x1 + j)));
  });
}

template <class V>
OPTENG_ALWAYS_INLINE void price_tile(const OptionBatch& in, double* out, std::size_t i0,
                                     std::size_t m) noexcept {
  Scratch s;
  common_stages<V, false>(in, i0, m, s);
  // Stage 4: N(omega d2) and the price.
  for_lanes<V>(m, [&]<class L>(std::size_t j) {
    const std::size_t i = i0 + j;
    const L nd2 = fast_norm_cdf(ld<L>(s.x2 + j));
    const L w = simd::load_type<L>(in.type.data() + i);
    const L price = ld<L>(s.disc + j) *
                    simd::max(w * (ld<L>(s.fwd + j) * ld<L>(s.nd1 + j) -
                                   ld<L>(in.strike.data() + i) * nd2),
                              fm::bcast<L>(0.0));
    simd::store(out + i, price);
  });
}

template <class V>
OPTENG_ALWAYS_INLINE void greeks_tile(const OptionBatch& in, const GreeksBatch& out,
                                      std::size_t i0, std::size_t m) noexcept {
  Scratch s;
  common_stages<V, true>(in, i0, m, s);
  // Stage 4: N(omega d2), phi(d1) and every output.
  for_lanes<V>(m, [&]<class L>(std::size_t j) {
    const std::size_t i = i0 + j;
    const L w = simd::load_type<L>(in.type.data() + i);
    const L spot = ld<L>(in.spot.data() + i);
    const L strike = ld<L>(in.strike.data() + i);
    const L time = ld<L>(in.time.data() + i);
    const L rate = ld<L>(in.rate.data() + i);
    const L carry = ld<L>(in.carry_yield.data() + i);
    const L vol = ld<L>(in.vol.data() + i);
    const L disc = ld<L>(s.disc + j);
    const L disc_q = ld<L>(s.disc_q + j);
    const L sqrt_t = ld<L>(s.sqrt_t + j);
    const L sd = ld<L>(s.sd + j);
    const L nd1 = ld<L>(s.nd1 + j);
    const L nd2 = fast_norm_cdf(ld<L>(s.x2 + j));
    const L pdf = fast_norm_pdf(ld<L>(s.d1 + j));
    const L price =
        disc * simd::max(w * (ld<L>(s.fwd + j) * nd1 - strike * nd2), fm::bcast<L>(0.0));
    const L delta = (w * disc_q) * nd1;
    const L rho = (((w * strike) * time) * disc) * nd2;
    const L carry_terms =
        ((((-w) * rate) * strike) * disc) * nd2 + ((((w * carry) * spot) * disc_q) * nd1);
    const L gamma = (disc_q * pdf) / (spot * sd);
    const L vega = ((spot * disc_q) * pdf) * sqrt_t;
    const L theta = ((((-spot) * disc_q) * pdf) * vol) / (fm::bcast<L>(2.0) * sqrt_t) +
                    carry_terms;
    simd::store(out.price.data() + i, price);
    simd::store(out.delta.data() + i, delta);
    simd::store(out.gamma.data() + i, gamma);
    simd::store(out.vega.data() + i, vega);
    simd::store(out.theta.data() + i, theta);
    simd::store(out.rho.data() + i, rho);
  });
}

// Prices options [begin, end) of `in` into out[begin, end).
template <class V>
inline void price_range(const OptionBatch& in, double* out, std::size_t begin,
                        std::size_t end) noexcept {
  for (std::size_t i0 = begin; i0 < end; i0 += kTile) {
    price_tile<V>(in, out, i0, std::min(kTile, end - i0));
  }
}

// Prices and Greeks for options [begin, end) of `in` into out.*[begin, end).
template <class V>
inline void greeks_range(const OptionBatch& in, const GreeksBatch& out, std::size_t begin,
                         std::size_t end) noexcept {
  for (std::size_t i0 = begin; i0 < end; i0 += kTile) {
    greeks_tile<V>(in, out, i0, std::min(kTile, end - i0));
  }
}

}  // namespace opt::kernel
