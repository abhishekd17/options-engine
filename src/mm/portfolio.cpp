#include "opt/mm/portfolio.hpp"

#include "opt/core/compensated_sum.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace opt::mm {

void Portfolio::apply_fill(std::size_t i, std::int64_t qty, double price) noexcept {
  assert(i < positions_.size());
  positions_[i] += qty;
  const double premium = static_cast<double>(qty) * price;
  cash_.add(-premium);
  abs_flows_.add(std::abs(premium));
}

void Portfolio::apply_hedge(double qty, double price, double cost) noexcept {
  hedge_ += qty;
  const double notional = qty * price;
  cash_.add(-notional);
  cash_.add(-cost);
  abs_flows_.add(std::abs(notional));
  abs_flows_.add(std::abs(cost));
}

double Portfolio::ledger_value(std::span<const double> price, double spot) const noexcept {
  NeumaierSum v{cash_.value()};
  for (std::size_t i = 0; i < positions_.size(); ++i) {
    v.add(static_cast<double>(positions_[i]) * price[i]);
  }
  v.add(hedge_ * spot);
  return v.value();
}

PortfolioRisk compute_risk(std::span<const std::int64_t> positions, double hedge,
                           const MarkView& marks, double spot) noexcept {
  PortfolioRisk r;
  r.spot = spot;
  double delta = hedge;
  double gamma = 0.0;
  double vega = 0.0;
  double theta = 0.0;
  for (std::size_t i = 0; i < positions.size(); ++i) {
    const auto q = static_cast<double>(positions[i]);
    delta += q * marks.delta[i];
    gamma += q * marks.gamma[i];
    vega += q * marks.vega[i];
    theta += q * marks.theta[i];
  }
  r.net_delta = delta;
  r.gamma_1pct = gamma_1pct(gamma, spot);
  r.vega_volpt = vega / 100.0;
  r.theta_day = theta / 365.0;
  return r;
}

}  // namespace opt::mm
