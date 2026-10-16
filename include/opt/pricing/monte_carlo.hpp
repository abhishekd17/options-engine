#pragma once
// Monte Carlo pricing of European options, with antithetic variates and a
// control variate, reporting the standard error.
//
// Exact terminal sampling: S_T = F exp(-sigma^2 T / 2 + sigma sqrt(T) Z), the
// payoff discounted by e^{-rT}. Z = norm_inv_cdf(uniform01(rng)) with the
// xoshiro256++ generator seeded from `seed`, so a run is deterministic.
// Why the inverse CDF (AS241): one uniform per normal, so antithetic pairs are
// exact (Phi^{-1}(1 - u) = -Phi^{-1}(u)) and the stream position is
// deterministic. Box-Muller needs pairs and trig calls; ziggurat is faster
// but uses rejection and tables, which makes the stream data-dependent.
//
// Antithetic: each sample is the pair average Y = (f(Z) + f(-Z)) / 2, so
// `paths` must be even and the sample count is n = paths / 2. Without it,
// n = paths.
// Control variate: X = (pair average of) the discounted S_T, whose mean
// S e^{-qT} is known. Estimator mean(Y) - beta (mean(X) - S e^{-qT}) with
// beta = cov(X, Y) / var(X) estimated from the same sample (bias O(1/n),
// negligible next to the standard error). Moments are accumulated with a
// one-pass Welford update in co-moment form (raw sums of X Y and X^2 cancel
// catastrophically when var(X) is small relative to mean(X)^2).
// std_error = sqrt(max(var(Y) - cov(X,Y)^2 / var(X), 0)) / sqrt(n); beta = 0
// if var(X) == 0; without the control variate std_error = sd(Y) / sqrt(n).
// With n == 1 the standard error is undefined and reported as NaN.
//
// Errors: paths == 0, an odd path count with antithetic sampling, or an
// invalid OptionSpec throw std::invalid_argument.
#include "opt/pricing/types.hpp"

#include <cstdint>
#include <limits>

namespace opt {

struct McConfig {
  std::uint64_t paths = 1'000'000;
  std::uint64_t seed = 1;
  bool antithetic = true;
  bool control_variate = true;
};

struct McResult {
  double price = std::numeric_limits<double>::quiet_NaN();
  double std_error = std::numeric_limits<double>::quiet_NaN();
  std::uint64_t paths = 0;  // simulated terminal values (both legs of each antithetic pair)
  double beta = 0.0;        // control-variate coefficient (0 when not used)
};

[[nodiscard]] McResult mc_european(const OptionSpec& spec, const McConfig& config);

}  // namespace opt
