#include "opt/mm/pnl.hpp"

#include "opt/core/compensated_sum.hpp"
#include "opt/mm/portfolio.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace opt::mm {

StepAttribution attribute_step(std::span<const std::int64_t> positions, double hedge, double s0,
                               double s1, double dt_years, const MarkView& m0,
                               const MarkView& m1) noexcept {
  const double ds = s1 - s0;
  NeumaierSum delta{hedge * ds};
  NeumaierSum gamma;
  NeumaierSum vega;
  NeumaierSum theta;
  NeumaierSum actual{hedge * ds};
  for (std::size_t i = 0; i < positions.size(); ++i) {
    if (positions[i] == 0) {
      continue;
    }
    const auto q = static_cast<double>(positions[i]);
    delta.add(q * m0.delta[i] * ds);
    gamma.add(0.5 * q * m0.gamma[i] * ds * ds);
    vega.add(q * m0.vega[i] * (m1.vol[i] - m0.vol[i]));
    theta.add(q * m0.theta[i] * dt_years);
    actual.add(q * (m1.price[i] - m0.price[i]));
  }
  StepAttribution s;
  s.delta = delta.value();
  s.gamma = gamma.value();
  s.vega = vega.value();
  s.theta = theta.value();
  s.actual = actual.value();
  s.residual = s.actual - (s.delta + s.gamma + s.vega + s.theta);
  return s;
}

void PnlAttribution::add(const StepAttribution& s) noexcept {
  delta_.add(s.delta);
  gamma_.add(s.gamma);
  vega_.add(s.vega);
  theta_.add(s.theta);
  residual_.add(s.residual);
}

PnlReport PnlAttribution::report() const noexcept {
  PnlReport r;
  r.delta = delta_.value();
  r.gamma = gamma_.value();
  r.vega = vega_.value();
  r.theta = theta_.value();
  r.residual = residual_.value();
  r.spread_capture = spread_.value();
  r.hedge_cost = hedge_cost_.value();
  NeumaierSum total;
  for (const double x : {r.delta, r.gamma, r.vega, r.theta, r.residual, r.spread_capture}) {
    total.add(x);
  }
  total.add(-r.hedge_cost);
  r.total = total.value();
  return r;
}

}  // namespace opt::mm
