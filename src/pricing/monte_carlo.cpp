#include "opt/pricing/monte_carlo.hpp"

#include "opt/core/normal.hpp"
#include "opt/core/rng.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace opt {

McResult mc_european(const OptionSpec& s, const McConfig& config) {
  if (config.paths == 0) {
    throw std::invalid_argument("mc_european: paths must be > 0");
  }
  if (config.antithetic && config.paths % 2 != 0) {
    throw std::invalid_argument("mc_european: paths = " + std::to_string(config.paths) +
                                " must be even with antithetic sampling");
  }
  if (validate(s) != InputError::Ok) {
    throw std::invalid_argument("mc_european: invalid OptionSpec");
  }
  const double w = omega(s.type);
  const double fwd = bsm_forward(s);
  const double disc = bsm_discount(s);
  const double drift = -0.5 * s.vol * s.vol * s.time;
  const double diffusion = s.vol * std::sqrt(s.time);
  const double strike = s.strike;
  const double expected_x = s.spot * std::exp(-s.carry_yield * s.time);  // E[D S_T]

  const std::uint64_t n = config.antithetic ? config.paths / 2 : config.paths;
  Xoshiro256pp rng{config.seed};
  // Welford co-moments of (X, Y).
  double mean_x = 0.0;
  double mean_y = 0.0;
  double m2_x = 0.0;
  double m2_y = 0.0;
  double c_xy = 0.0;
  for (std::uint64_t i = 1; i <= n; ++i) {
    const double z = norm_inv_cdf(uniform01(rng));
    const double st = fwd * std::exp(drift + diffusion * z);
    double y = disc * std::max(w * (st - strike), 0.0);
    double x = disc * st;
    if (config.antithetic) {
      const double st_anti = fwd * std::exp(drift - diffusion * z);
      y = 0.5 * (y + disc * std::max(w * (st_anti - strike), 0.0));
      x = 0.5 * (x + disc * st_anti);
    }
    const auto count = static_cast<double>(i);
    const double dx = x - mean_x;
    mean_x += dx / count;
    const double dy = y - mean_y;
    mean_y += dy / count;
    m2_x += dx * (x - mean_x);
    m2_y += dy * (y - mean_y);
    c_xy += dx * (y - mean_y);
  }

  const auto nd = static_cast<double>(n);
  McResult result{mean_y, std::numeric_limits<double>::quiet_NaN(), config.paths, 0.0};
  if (n < 2) {
    return result;
  }
  const double var_y = m2_y / (nd - 1.0);
  if (!config.control_variate) {
    result.std_error = std::sqrt(var_y / nd);
    return result;
  }
  const double var_x = m2_x / (nd - 1.0);
  if (var_x == 0.0) {
    result.std_error = std::sqrt(var_y / nd);
    return result;
  }
  const double cov = c_xy / (nd - 1.0);
  result.beta = cov / var_x;
  result.price = mean_y - result.beta * (mean_x - expected_x);
  result.std_error = std::sqrt(std::max(var_y - cov * cov / var_x, 0.0) / nd);
  return result;
}

}  // namespace opt
