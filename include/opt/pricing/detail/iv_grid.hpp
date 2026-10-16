#pragma once
// The implied-vol round-trip grid of design 5.8 item 4, shared by
// tests/pricing/implied_vol_test.cpp and tools/iv_accuracy.cpp so both run
// exactly the same points.
//
// sigma: 40 log-spaced values in [0.01, 3]; T in {1 h, 1 d, 1 w, 1 m, 3 m,
// 1 y, 5 y}; standardised moneyness z = ln(K/F) / (sigma sqrt T) in 25 values
// in [-6, 6]; call and put; r in {0, 0.05}; q in {0, 0.03}; S = 100.
// 40 * 7 * 25 * 2 * 2 * 2 = 56,000 points.
#include "opt/core/normal.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace opt::detail {

inline constexpr std::size_t kIvGridVols = 40;
inline constexpr std::array<double, 7> kIvGridTimes = {
    1.0 / (365.0 * 24.0), 1.0 / 365.0, 7.0 / 365.0, 1.0 / 12.0, 0.25, 1.0, 5.0};
inline constexpr std::size_t kIvGridMoneyness = 25;
inline constexpr std::array<double, 2> kIvGridRates = {0.0, 0.05};
inline constexpr std::array<double, 2> kIvGridYields = {0.0, 0.03};
inline constexpr std::size_t kIvGridPoints = kIvGridVols * kIvGridTimes.size() *
                                             kIvGridMoneyness * 2 * kIvGridRates.size() *
                                             kIvGridYields.size();

struct IvGridPoint {
  OptionSpec spec;  // spec.vol is the true sigma
  double z;         // ln(K/F) / (sigma sqrt T)
  double price;     // bsm_price(spec)
  // Rounding scale of the price: D (F N(omega d1) + K N(omega d2)). The
  // computed price is the difference of these two terms, so its absolute
  // rounding error is a few eps times this.
  double price_scale;
  double vega;  // dV/dsigma = D F phi(d1) sqrt(T) (D F = S e^{-qT})
  // Well-conditioned: vega / price_scale >= kIvWellVegaRatio and
  // price / (D F) >= 1e-12. See kIvWellVegaRatio.
  bool well_conditioned;
};

// Design 5.8 item 4 defined "well-conditioned" as vega / (D F) >= 1e-6. That
// normalisation is wrong for in-the-money puts with K >> F: their price is
// about D K, carries a rounding error of about eps D K, and on this grid K / F
// reaches 1e17 (z = 6, sigma = 3, T = 5), so vega / F said "well-conditioned"
// while vega / K was 1e-20, and the round trip missed by 0.23 vol. A price
// error dP moves sigma by dP / vega; the solver stops once |f| is below
// 4 eps x price_scale (rounding noise), so requiring
// vega / price_scale >= 4 eps / 1e-10 = 8.9e-6, rounded up to 1e-5, makes
// the 1e-10 vol tolerance a consequence of the price precision rather than
// luck. Measured values for both definitions are in docs/accuracy.md.
inline constexpr double kIvWellVegaRatio = 1e-5;

// Calls fn(const IvGridPoint&) for every grid point, in a fixed order.
template <class Fn>
void for_each_iv_grid_point(Fn&& fn) {
  constexpr double kSpot = 100.0;
  for (std::size_t iv = 0; iv < kIvGridVols; ++iv) {
    const double vol =
        0.01 * std::pow(300.0, static_cast<double>(iv) / static_cast<double>(kIvGridVols - 1));
    for (const double time : kIvGridTimes) {
      for (std::size_t iz = 0; iz < kIvGridMoneyness; ++iz) {
        const double z = -6.0 + 0.5 * static_cast<double>(iz);
        for (const OptionType type : {OptionType::Call, OptionType::Put}) {
          for (const double rate : kIvGridRates) {
            for (const double yield : kIvGridYields) {
              OptionSpec s{type, kSpot, 0.0, time, rate, yield, vol};
              const double forward = bsm_forward(s);
              const double sd = vol * std::sqrt(time);
              s.strike = forward * std::exp(z * sd);
              const double price = bsm_price(s);
              const double disc = bsm_discount(s);
              const double w = omega(type);
              const double d1 = (std::log(forward / s.strike) + 0.5 * sd * sd) / sd;
              const double d2 = d1 - sd;
              const double scale =
                  disc * (forward * norm_cdf(w * d1) + s.strike * norm_cdf(w * d2));
              const double vega = disc * forward * norm_pdf(d1) * std::sqrt(time);
              const bool well = vega >= kIvWellVegaRatio * scale && price / (disc * forward) >= 1e-12;
              fn(IvGridPoint{s, z, price, scale, vega, well});
            }
          }
        }
      }
    }
  }
}

}  // namespace opt::detail
