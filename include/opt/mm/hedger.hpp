#pragma once
// Delta hedger (design 8.6): hedges iff |net delta| > threshold (strict), to
// a net delta of 0, at the spot price, paying |qty| x spot x bps / 1e4.
#include "opt/mm/config.hpp"
#include <optional>

namespace opt::mm {

struct HedgeTrade {
  double qty = 0.0;    // BTC, signed (+ buy)
  double price = 0.0;  // USD
  double cost = 0.0;   // USD, >= 0
};

class Hedger {
 public:
  explicit Hedger(const HedgeConfig& config) noexcept : c_(config) {}
  [[nodiscard]] std::optional<HedgeTrade> maybe_hedge(double net_delta,
                                                      double spot) const noexcept;

 private:
  HedgeConfig c_;
};

}  // namespace opt::mm
