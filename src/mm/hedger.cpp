#include "opt/mm/hedger.hpp"

#include <cmath>
#include <optional>

namespace opt::mm {

std::optional<HedgeTrade> Hedger::maybe_hedge(double net_delta, double spot) const noexcept {
  if (!(std::abs(net_delta) > c_.threshold)) {
    return std::nullopt;
  }
  HedgeTrade t;
  t.qty = -net_delta;
  t.price = spot;
  t.cost = std::abs(t.qty) * spot * c_.cost_bps / 1e4;
  return t;
}

}  // namespace opt::mm
