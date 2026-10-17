#pragma once
// PnL attribution (design 8.7, docs/mm_model.md).
//
// Doubles, not fixed point: option values, Greeks and fill prices are real
// numbers from a pricing model and there is no tick grid in this simulator,
// so fixed point would add conversion rounding without making anything
// exact. Buckets accumulate with NeumaierSum. A real venue would need fixed
// point for prices and positions.
//
// For the step t -> t+dt, positions q_i and hedge h held over the step,
// dS = S1 - S0, dsigma_i = sigma_i(t+dt) - sigma_i(t) at the fixed strike
// (sticky strike), dt in years, Greeks from time t:
//   delta = (sum q_i Delta_i + h) dS        gamma = 1/2 sum q_i Gamma_i dS^2
//   vega  = sum q_i vega_i dsigma_i          theta = sum q_i Theta_i dt
//   actual = sum q_i (V_i(t+dt) - V_i(t)) + h dS
//   residual = actual - (delta + gamma + vega + theta)
// Spread capture per fill: (fair - price) x qty for a buy, (price - fair) x
// |qty| for a sell. Hedge cost per hedge.
// Total (attribution) = sum of all buckets
//   = delta + gamma + vega + theta + residual + spread capture - hedge cost.
// The independent ledger PnL is cash + sum q_i V_i + h S (Portfolio). The
// identity |attribution - ledger| <= 1e-6 + 1e-12 x (sum of |cash flows|) is
// checked by the simulator, mm_sim and the tests.
#include "opt/core/compensated_sum.hpp"
#include "opt/mm/portfolio.hpp"
#include <cstdint>
#include <span>

namespace opt::mm {

struct StepAttribution {
  double delta = 0.0;
  double gamma = 0.0;
  double vega = 0.0;
  double theta = 0.0;
  double actual = 0.0;
  double residual = 0.0;
};

// Attribution of one step; m0 and m1 are the marks at t and t+dt.
[[nodiscard]] StepAttribution attribute_step(std::span<const std::int64_t> positions,
                                             double hedge, double s0, double s1,
                                             double dt_years, const MarkView& m0,
                                             const MarkView& m1) noexcept;

struct PnlReport {
  double delta = 0.0;
  double gamma = 0.0;
  double vega = 0.0;
  double theta = 0.0;
  double residual = 0.0;
  double spread_capture = 0.0;
  double hedge_cost = 0.0;  // positive number, subtracted in total
  double total = 0.0;       // sum of the buckets above (hedge cost negative)
};

class PnlAttribution {
 public:
  void add(const StepAttribution& s) noexcept;
  void add_spread_capture(double usd) noexcept { spread_.add(usd); }
  void add_hedge_cost(double usd) noexcept { hedge_cost_.add(usd); }
  [[nodiscard]] PnlReport report() const noexcept;

 private:
  NeumaierSum delta_, gamma_, vega_, theta_, residual_, spread_, hedge_cost_;
};

[[nodiscard]] inline double identity_tolerance(double sum_abs_cash_flows) noexcept {
  return 1e-6 + 1e-12 * sum_abs_cash_flows;
}

}  // namespace opt::mm
