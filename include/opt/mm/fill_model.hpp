#pragma once
// Fill model (design 8.5, docs/mm_model.md).
//
// Per active quote side per step: distance d = |quote_vol - fair_vol| in vol
// points, intensity lambda = A exp(-kappa d) per second, and
// P(fill in dt) = 1 - exp(-lambda dt) (fast_exp). The side fills one quote
// size if u < P, with u from the "fill" xoshiro stream (separate from the
// market stream, so strategy changes never change the market path).
//
// Biases (stated in docs/mm_model.md): fills do not depend on where the price
// goes next (no adverse selection), arrivals are independent across options
// and constant in time, and the fair value is the maker's own surface. The
// simulated spread capture is therefore an upper bound.
#include "opt/core/rng.hpp"
#include "opt/mm/config.hpp"
#include <cstdint>

namespace opt::mm {

class FillModel {
 public:
  FillModel(const FillConfig& config, std::uint64_t seed) noexcept;

  // P(fill in dt_sec) for a side at distance_volpts from fair.
  [[nodiscard]] double probability(double distance_volpts, double dt_sec) const noexcept;
  // One draw from the fill stream.
  [[nodiscard]] bool draw(double distance_volpts, double dt_sec) noexcept;

 private:
  FillConfig c_;
  Xoshiro256pp rng_;
};

}  // namespace opt::mm
