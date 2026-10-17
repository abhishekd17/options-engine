#pragma once
// Configuration of the market-making simulator (design 8.2, 8.3).
//
// Every default is documented with its unit in docs/mm_model.md; a change of
// a default must be reflected there and in tests/mm/golden_test.cpp.
#include "opt/pricing/types.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opt::mm {

enum class Mode : std::uint8_t { Synthetic, Recorded };

struct QuoterConfig {
  std::int64_t size = 1;   // contracts per quote side (1 contract = 1 BTC notional)
  double half_spread = 1.0;  // h0, vol points
  double w_inv = 1.0;        // vol points of extra half spread at full inventory utilisation
  double w_vega = 1.0;       // vol points at full vega utilisation
  double w_gamma = 0.5;      // vol points at full gamma utilisation
  double k_inv = 0.5;        // vol points of skew at full inventory utilisation
  double k_vega = 0.5;       // vol points of skew at full vega utilisation
  double min_vol = 0.01;     // bid vol floor, decimal (1 vol point)
};

struct LimitConfig {
  std::int64_t max_position = 10;  // |contracts| per option
  double max_net_delta = 5.0;      // |BTC|, pre-hedge, after a fill
  double max_vega = 2000.0;        // |USD per vol point| of the portfolio
  double max_gamma_1pct = 1.0;     // |BTC of delta per 1% spot move| (Gamma S 0.01)
};

struct HedgeConfig {
  double threshold = 1.0;  // BTC; hedge iff |net delta| > threshold (strict)
  double cost_bps = 5.0;   // cost per hedge, bps of |qty| x spot
};

struct FillConfig {
  double intensity = 1.0 / 120.0;  // A: fills per second per side at zero distance
  double kappa = 1.0;              // decay per vol point of distance from fair vol
};

struct SyntheticConfig {
  double spot0 = 80'000.0;  // USD
  double vol0 = 0.50;       // initial ATM vol v_0, decimal
  double vol_mean = 0.50;   // exp(theta): long-run ATM vol level, decimal
  double vol_kappa = 5.0;   // mean reversion of ln v, per year
  double vol_xi = 1.0;      // vol of ln v, per sqrt(year)
  double rho_sv = -0.3;     // correlation of spot and ln v shocks
  std::vector<double> expiry_days{7.0, 30.0, 90.0};  // fixed calendar expiries, days from start
  double svi_beta = 0.4;   // b_tau = beta v_t sqrt(tau)
  double svi_rho = -0.2;
  double svi_m = 0.0;
  double svi_s = 0.1;
  double strike_round = 1000.0;  // USD; strikes are F_0 e^k rounded to this
};

struct SimConfig {
  Mode mode = Mode::Synthetic;
  std::uint64_t seed = 42;
  std::uint64_t steps = 86'400;  // synthetic: number of dt steps (horizon = steps x dt)
  double dt_sec = 1.0;           // synthetic step, seconds
  std::size_t quoted_expiries = 2;                     // the first N expiries are quoted
  std::vector<double> target_k{-0.2, -0.1, 0.0, 0.1, 0.2};  // ln(K / F_0); puts below 0
  QuoterConfig quoter;
  LimitConfig limits;
  HedgeConfig hedge;
  FillConfig fill;
  SyntheticConfig synthetic;
};

// One quoted option. expiry is in years from the start of the simulation, so
// tau = expiry - t uses the same arithmetic as the market source's slices.
struct Instrument {
  std::string name;
  OptionType type = OptionType::Call;
  double strike = 0.0;
  double expiry = 0.0;
};

inline constexpr double kSecondsPerYear = 365.0 * 86'400.0;

// Empty string when the config is usable, else a message naming the field.
// Checks value ranges and, for Mode::Synthetic, that the horizon ends before
// the first expiry and that the initial synthetic surface is free of static
// arbitrage (the generator's own check). mm_sim exits 2 on a message.
[[nodiscard]] std::string validate(const SimConfig& config);

}  // namespace opt::mm
