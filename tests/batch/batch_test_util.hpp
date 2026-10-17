#pragma once
// Seeded structure-of-arrays option sets for the batch tests and benchmarks.
#include "opt/batch/batch.hpp"
#include "opt/core/rng.hpp"
#include "opt/pricing/types.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace opt::test {

struct SoaOptions {
  std::vector<OptionType> type;
  std::vector<double> spot, strike, time, rate, carry_yield, vol;

  void push(OptionType t, double s, double k, double tt, double r, double q, double v) {
    type.push_back(t);
    spot.push_back(s);
    strike.push_back(k);
    time.push_back(tt);
    rate.push_back(r);
    carry_yield.push_back(q);
    vol.push_back(v);
  }
  [[nodiscard]] std::size_t size() const noexcept { return spot.size(); }
  // View of options [offset, offset + count).
  [[nodiscard]] OptionBatch view(std::size_t offset, std::size_t count) const {
    return {std::span(type).subspan(offset, count),        std::span(spot).subspan(offset, count),
            std::span(strike).subspan(offset, count),      std::span(time).subspan(offset, count),
            std::span(rate).subspan(offset, count),        std::span(carry_yield).subspan(offset, count),
            std::span(vol).subspan(offset, count)};
  }
  [[nodiscard]] OptionBatch view() const { return view(0, size()); }
};

struct SoaGreeks {
  std::vector<double> price, delta, gamma, vega, theta, rho;
  explicit SoaGreeks(std::size_t n)
      : price(n, -1.0), delta(n, -1.0), gamma(n, -1.0), vega(n, -1.0), theta(n, -1.0),
        rho(n, -1.0) {}
  [[nodiscard]] GreeksBatch view(std::size_t offset, std::size_t count) {
    return {std::span(price).subspan(offset, count), std::span(delta).subspan(offset, count),
            std::span(gamma).subspan(offset, count), std::span(vega).subspan(offset, count),
            std::span(theta).subspan(offset, count), std::span(rho).subspan(offset, count)};
  }
  [[nodiscard]] GreeksBatch view() { return view(0, price.size()); }
};

inline double log_uniform(Xoshiro256pp& rng, double lo, double hi) {
  return std::exp(uniform(rng, std::log(lo), std::log(hi)));
}

// n seeded options over the batch domain (batch.hpp): S log-uniform on
// [1e-8, 1e12]; K = S e^u with u uniform on [-3, 3] (redrawn if K leaves the
// domain); T log-uniform on [1e-8, 100]; sigma log-uniform on [1e-6, 10];
// r and q uniform on [-0.05, 0.15] for 80% of the options and on the whole
// domain [-1, 1] for the rest; call or put by coin flip.
inline SoaOptions random_options(std::size_t n, std::uint64_t seed) {
  Xoshiro256pp rng(seed);
  SoaOptions o;
  for (std::size_t i = 0; i < n; ++i) {
    const OptionType t = (rng() & 1U) != 0 ? OptionType::Put : OptionType::Call;
    const double s = log_uniform(rng, 1e-8, 1e12);
    double k = 0.0;
    do {
      k = s * std::exp(uniform(rng, -3.0, 3.0));
    } while (!(k >= 1e-8 && k <= 1e12));
    const double tt = log_uniform(rng, 1e-8, 100.0);
    const double v = log_uniform(rng, 1e-6, 10.0);
    const bool wide = uniform01(rng) < 0.2;
    const double r = wide ? uniform(rng, -1.0, 1.0) : uniform(rng, -0.05, 0.15);
    const double q = wide ? uniform(rng, -1.0, 1.0) : uniform(rng, -0.05, 0.15);
    o.push(t, s, k, tt, r, q, v);
  }
  return o;
}

// Edge set (design 6.6): T = 1e-8 and 1e-6, sigma = 1e-6 and 10,
// z = ln(F/K)/(sigma sqrt T) = +-40, F == K exactly, S and K at the domain
// ends, and call/put patterns that change from lane to lane.
inline SoaOptions edge_options() {
  SoaOptions o;
  const OptionType kTypes[] = {OptionType::Call, OptionType::Put};
  for (const OptionType t : kTypes) {
    for (const double tt : {1e-8, 1e-6, 1.0, 100.0}) {
      for (const double v : {1e-6, 10.0, 0.2}) {
        for (const double k : {50.0, 100.0, 150.0}) {
          o.push(t, 100.0, k, tt, 0.03, 0.01, v);
          o.push(t, 100.0, k, tt, 0.02, 0.02, v);  // r == q: F == S exactly
        }
      }
    }
    // z = +-40 at sd = 0.2 (sigma 0.2, T 1): K = F e^{-z sd}.
    for (const double z : {-40.0, 40.0, -38.5, 38.5, -37.0, 37.0, -8.0, 8.0}) {
      const double fwd = 100.0 * std::exp((0.03 - 0.01) * 1.0);
      o.push(t, 100.0, fwd * std::exp(-z * 0.2), 1.0, 0.03, 0.01, 0.2);
    }
    // F == K exactly (r == q, K == S) at several sd.
    for (const double tt : {1e-8, 1e-4, 1.0, 100.0}) {
      for (const double v : {1e-6, 1e-3, 0.3, 10.0}) {
        o.push(t, 123.456, 123.456, tt, 0.05, 0.05, v);
      }
    }
    // Domain ends.
    for (const double s : {1e-8, 1e12}) {
      for (const double k : {1e-8, 1e12}) {
        for (const double tt : {1e-8, 100.0}) {
          o.push(t, s, k, tt, 1.0, -1.0, 10.0);
          o.push(t, s, k, tt, -1.0, 1.0, 1e-6);
          o.push(t, s, k, tt, 0.0, 0.0, 0.5);
        }
      }
    }
  }
  // Alternating and irregular call/put patterns.
  for (int i = 0; i < 64; ++i) {
    const OptionType t = (i % 3 == 0 || i % 5 == 0) ? OptionType::Put : OptionType::Call;
    o.push(t, 100.0, 80.0 + i, 0.5, 0.04, 0.0, 0.25);
  }
  return o;
}

inline void append(SoaOptions& dst, const SoaOptions& src) {
  for (std::size_t i = 0; i < src.size(); ++i) {
    dst.push(src.type[i], src.spot[i], src.strike[i], src.time[i], src.rate[i],
             src.carry_yield[i], src.vol[i]);
  }
}

}  // namespace opt::test
