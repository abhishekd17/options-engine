#pragma once
// Seeded parameter grids shared by the pricing tests.
#include "opt/core/rng.hpp"
#include "opt/pricing/types.hpp"

namespace opt::test {

inline constexpr double kOneDay = 1.0 / 365.0;

// Design 5.8 item 1 ranges: S, K in [1, 1000], T in [1 day, 10 y],
// r, q in [-0.02, 0.15], sigma in [0.01, 3]; type alternates by coin flip.
inline OptionSpec draw_spec(Xoshiro256pp& rng) {
  OptionSpec s;
  s.type = (rng() & 1U) != 0 ? OptionType::Put : OptionType::Call;
  s.spot = uniform(rng, 1.0, 1000.0);
  s.strike = uniform(rng, 1.0, 1000.0);
  s.time = uniform(rng, kOneDay, 10.0);
  s.rate = uniform(rng, -0.02, 0.15);
  s.carry_yield = uniform(rng, -0.02, 0.15);
  s.vol = uniform(rng, 0.01, 3.0);
  return s;
}

}  // namespace opt::test
