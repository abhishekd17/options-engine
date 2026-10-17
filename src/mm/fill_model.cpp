#include "opt/mm/fill_model.hpp"

#include "opt/core/fast_math.hpp"
#include "opt/core/rng.hpp"
#include "opt/mm/synthetic.hpp"

#include <cstdint>

namespace opt::mm {

FillModel::FillModel(const FillConfig& config, std::uint64_t seed) noexcept
    : c_(config), rng_(make_stream(seed, kFillStream)) {}

double FillModel::probability(double distance_volpts, double dt_sec) const noexcept {
  const double lambda = c_.intensity * fast_exp(-c_.kappa * distance_volpts);
  return 1.0 - fast_exp(-lambda * dt_sec);
}

bool FillModel::draw(double distance_volpts, double dt_sec) noexcept {
  return uniform01(rng_) < probability(distance_volpts, dt_sec);
}

}  // namespace opt::mm
