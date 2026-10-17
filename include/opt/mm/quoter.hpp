#pragma once
// Quoter (design 8.4): a pure function of the fair vols, the Greeks, the
// positions and the portfolio risk. No state, no allocation.
//
// Utilisations, each clamped to [-1, 1]:
//   u_inv_i = q_i / max_position, u_vega = V / max_vega, u_gamma = G / max_gamma_1pct.
// center = fair_vol - (k_inv u_inv_i + k_vega u_vega) / 100
// half   = (h0 + w_inv |u_inv_i| + w_vega |u_vega| + w_gamma |u_gamma|) / 100
// bid_vol = max(center - half, min_vol), ask_vol = center + half.
// Prices: Black-76 with r = 0 through the FastScalar batch engine (spot =
// forward, rate = carry = 0). Being long lowers both quotes, so the maker
// buys less and sells more.
//
// Pre-trade check per side: if one fill of the quote size would breach any
// limit (|position|, |vega|, |gamma_1pct| or pre-hedge |net delta|), that
// side is inactive and counted. A non-quotable instrument (no fresh surface,
// or not listed) has both sides inactive and is not counted as blocked.
// Vols and prices are filled in for inactive sides too.
#include "opt/mm/config.hpp"
#include "opt/mm/portfolio.hpp"
#include "opt/pricing/types.hpp"
#include <cstdint>
#include <span>

namespace opt::mm {

struct InstrumentView {
  OptionType type = OptionType::Call;
  double forward = 0.0;
  double strike = 0.0;
  double tau = 0.0;       // years
  double fair_vol = 0.0;  // surface vol at (strike, tau), decimal
  double delta = 0.0;     // per contract, w.r.t. the forward
  double gamma = 0.0;
  double vega = 0.0;      // per 1.00 of vol
  std::int64_t position = 0;
  bool quotable = true;
};

struct Quote {
  double bid_vol = 0.0;
  double ask_vol = 0.0;
  double bid_price = 0.0;  // USD
  double ask_price = 0.0;
  bool bid_active = false;
  bool ask_active = false;
};

// Writes one Quote per instrument (out.size() must equal instruments.size(),
// else std::invalid_argument). Returns the number of sides blocked by limits.
std::uint64_t make_quotes(const QuoterConfig& qc, const LimitConfig& lim,
                          std::span<const InstrumentView> instruments, const PortfolioRisk& risk,
                          std::span<Quote> out);

// True iff a fill of signed qty contracts of an option with the given Greeks
// keeps every limit. Used by the quoter and by the simulator when it applies
// several fills in one step.
[[nodiscard]] bool fill_within_limits(const LimitConfig& lim, std::int64_t position,
                                      std::int64_t qty, double delta, double gamma, double vega,
                                      const PortfolioRisk& risk) noexcept;

}  // namespace opt::mm
