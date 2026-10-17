#pragma once
// Synthetic market generator (design 8.2, docs/mm_model.md).
//
// Spot: zero-drift GBM with the current ATM vol v_t,
//   S_{t+dt} = S_t exp(-v_t^2 dt / 2 + v_t sqrt(dt) Z1).
// ATM vol: v_t = exp(x_t), x log-OU,
//   x_{t+dt} = x_t + kappa (theta - x_t) dt + xi sqrt(dt) (rho Z1 + sqrt(1 - rho^2) Z2),
//   theta = ln(vol_mean).
// Smile per fixed calendar expiry: raw SVI with m, s, rho fixed,
//   b_tau = beta v_t sqrt(tau),  a = v_t^2 tau - b_tau s,
// so the ATM total variance is v_t^2 tau. Forward = S_t (r = q = 0).
// Every step updates every slice in place (VolSurface::update_slice) with
// tau_i = expiry_i - t. Z1, Z2 = fast_norm_inv(uniform01) on the "market"
// xoshiro stream; only fast_exp, fast_log, fast_norm_inv and std::sqrt are
// used, so the path is bit-reproducible on every platform.
//
// Arbitrage guard: butterfly_check per slice and calendar_check per adjacent
// pair over k in [-1.5, 1.5] (401 points) at construction (violation:
// std::invalid_argument) and every 1,000 steps (violation: the state is
// returned with arbitrage_ok = false). Both checks allocate nothing.
#include "opt/core/rng.hpp"
#include "opt/mm/config.hpp"
#include "opt/mm/market.hpp"
#include "opt/vol/surface.hpp"
#include "opt/vol/svi.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace opt::mm {

inline constexpr std::uint64_t kMarketStream = 1;
inline constexpr std::uint64_t kFillStream = 2;
inline constexpr double kGuardKLo = -1.5;
inline constexpr double kGuardKHi = 1.5;
inline constexpr int kGuardPoints = 401;
inline constexpr std::uint64_t kGuardEvery = 1000;

// SVI parameters of the synthetic smile for ATM vol v and expiry tau.
[[nodiscard]] SviParams synthetic_svi(const SyntheticConfig& c, double v, double tau) noexcept;

// True iff every slice passes butterfly_check and every adjacent pair passes
// calendar_check on the guard grid. No allocation.
[[nodiscard]] bool surface_arbitrage_free(const VolSurface& surface) noexcept;

class SyntheticMarket {
 public:
  // Throws std::invalid_argument if expiries are not positive and strictly
  // increasing or the initial surface fails the arbitrage guard.
  explicit SyntheticMarket(const SimConfig& config);

  // The first call returns the state at t = 0; each later call advances one
  // step; returns false after config.steps steps.
  bool next(MarketState& m) noexcept;

  [[nodiscard]] double atm_vol() const noexcept { return v_; }
  [[nodiscard]] double spot() const noexcept { return s_; }
  [[nodiscard]] double t_years() const noexcept { return t_; }
  [[nodiscard]] std::uint64_t step_index() const noexcept { return n_; }
  [[nodiscard]] const VolSurface& surface() const noexcept { return surface_; }
  // Expiries in years from the start.
  [[nodiscard]] std::span<const double> expiries() const noexcept { return expiry_; }

 private:
  void update_slices() noexcept;

  SyntheticConfig c_;
  Xoshiro256pp rng_;
  std::uint64_t steps_;
  double dt_;  // years
  double sqrt_dt_;
  double theta_;
  double rho_c_;  // sqrt(1 - rho_sv^2)
  std::vector<double> expiry_;
  VolSurface surface_;
  double s_;
  double x_;
  double v_;
  double t_ = 0.0;
  std::uint64_t n_ = 0;
  bool started_ = false;
  bool arbitrage_ok_ = true;
};

// The quoted instruments of a synthetic run: for each of the first
// config.quoted_expiries expiries and each target k, strike =
// F_0 e^k rounded to strike_round; puts for k < 0, calls for k >= 0.
[[nodiscard]] std::vector<Instrument> synthetic_instruments(const SimConfig& config);

}  // namespace opt::mm
