#pragma once
// Positions, cash and portfolio risk (design 8.6).
//
// Accounting is in USD as if the options were linear USD-settled contracts
// (docs/mm_model.md). Greeks are per contract (1 BTC notional) and with
// respect to the forward; vega per 1.00 of vol, theta per year.
#include "opt/core/compensated_sum.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace opt::mm {

// Structure-of-arrays marks of the option set at one time.
struct Marks {
  std::vector<double> price, delta, gamma, vega, theta, vol;
  explicit Marks(std::size_t n = 0)
      : price(n), delta(n), gamma(n), vega(n), theta(n), vol(n) {}
  [[nodiscard]] std::size_t size() const noexcept { return price.size(); }
};

struct MarkView {
  std::span<const double> price, delta, gamma, vega, theta, vol;
  MarkView() = default;
  // NOLINTNEXTLINE(google-explicit-constructor): a view of the arrays
  MarkView(const Marks& m)
      : price(m.price), delta(m.delta), gamma(m.gamma), vega(m.vega), theta(m.theta),
        vol(m.vol) {}
};

struct PortfolioRisk {
  double spot = 0.0;
  double net_delta = 0.0;   // BTC: sum q_i delta_i + hedge
  double gamma_1pct = 0.0;  // BTC of delta per 1% spot move: sum q_i gamma_i S 0.01
  double vega_volpt = 0.0;  // USD per vol point: sum q_i vega_i / 100
  double theta_day = 0.0;   // USD per day: sum q_i theta_i / 365
};

class Portfolio {
 public:
  explicit Portfolio(std::size_t instruments) : positions_(instruments, 0) {}

  // Buys (qty > 0) or sells (qty < 0) qty contracts of option i at price.
  void apply_fill(std::size_t i, std::int64_t qty, double price) noexcept;
  // Trades qty BTC of the hedge instrument at price and pays cost.
  void apply_hedge(double qty, double price, double cost) noexcept;

  [[nodiscard]] std::span<const std::int64_t> positions() const noexcept { return positions_; }
  [[nodiscard]] double hedge() const noexcept { return hedge_; }
  [[nodiscard]] double cash() const noexcept { return cash_.value(); }
  // Sum of |premium|, |hedge notional| and costs paid; scales the identity
  // tolerance.
  [[nodiscard]] double sum_abs_cash_flows() const noexcept { return abs_flows_.value(); }
  // cash + sum q_i price_i + hedge spot: the ledger PnL (initial value 0).
  [[nodiscard]] double ledger_value(std::span<const double> price, double spot) const noexcept;

 private:
  std::vector<std::int64_t> positions_;
  double hedge_ = 0.0;
  NeumaierSum cash_;
  NeumaierSum abs_flows_;
};

[[nodiscard]] PortfolioRisk compute_risk(std::span<const std::int64_t> positions, double hedge,
                                         const MarkView& marks, double spot) noexcept;

// Per-contract gamma_1pct of one option: gamma S 0.01.
[[nodiscard]] inline double gamma_1pct(double gamma, double spot) noexcept {
  return gamma * spot * 0.01;
}

}  // namespace opt::mm
