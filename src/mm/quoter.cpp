#include "opt/mm/quoter.hpp"

#include "opt/batch/batch.hpp"
#include "opt/mm/portfolio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace opt::mm {

bool fill_within_limits(const LimitConfig& lim, std::int64_t position, std::int64_t qty,
                        double delta, double gamma, double vega,
                        const PortfolioRisk& risk) noexcept {
  const std::int64_t q_after = position + qty;
  const auto dq = static_cast<double>(qty);
  if (q_after > lim.max_position || q_after < -lim.max_position) {
    return false;
  }
  if (std::abs(risk.vega_volpt + dq * vega / 100.0) > lim.max_vega) {
    return false;
  }
  if (std::abs(risk.gamma_1pct + dq * gamma_1pct(gamma, risk.spot)) > lim.max_gamma_1pct) {
    return false;
  }
  return std::abs(risk.net_delta + dq * delta) <= lim.max_net_delta;
}

std::uint64_t make_quotes(const QuoterConfig& qc, const LimitConfig& lim,
                          std::span<const InstrumentView> instruments, const PortfolioRisk& risk,
                          std::span<Quote> out) {
  if (out.size() != instruments.size()) {
    throw std::invalid_argument("make_quotes: out.size() != instruments.size()");
  }
  const double u_vega = std::clamp(risk.vega_volpt / lim.max_vega, -1.0, 1.0);
  const double u_gamma = std::clamp(risk.gamma_1pct / lim.max_gamma_1pct, -1.0, 1.0);
  std::uint64_t blocked = 0;
  for (std::size_t i = 0; i < instruments.size(); ++i) {
    const InstrumentView& in = instruments[i];
    const double u_inv = std::clamp(
        static_cast<double>(in.position) / static_cast<double>(lim.max_position), -1.0, 1.0);
    const double center = in.fair_vol - (qc.k_inv * u_inv + qc.k_vega * u_vega) / 100.0;
    const double half = (qc.half_spread + qc.w_inv * std::abs(u_inv) +
                         qc.w_vega * std::abs(u_vega) + qc.w_gamma * std::abs(u_gamma)) /
                        100.0;
    Quote& q = out[i];
    q.bid_vol = std::max(center - half, qc.min_vol);
    q.ask_vol = std::max(center + half, qc.min_vol);

    // Black-76 with r = 0 via the FastScalar BSM engine (spot = forward).
    const std::array<OptionType, 2> type{in.type, in.type};
    const std::array<double, 2> fwd{in.forward, in.forward};
    const std::array<double, 2> strike{in.strike, in.strike};
    const std::array<double, 2> tau{in.tau, in.tau};
    const std::array<double, 2> zero{0.0, 0.0};
    const std::array<double, 2> vol{q.bid_vol, q.ask_vol};
    std::array<double, 2> price{};
    const OptionBatch batch{type, fwd, strike, tau, zero, zero, vol};
    price_batch(Engine::FastScalar, batch, price);
    q.bid_price = price[0];
    q.ask_price = price[1];

    if (!in.quotable) {
      q.bid_active = false;
      q.ask_active = false;
      continue;
    }
    q.bid_active = fill_within_limits(lim, in.position, qc.size, in.delta, in.gamma, in.vega, risk);
    q.ask_active =
        fill_within_limits(lim, in.position, -qc.size, in.delta, in.gamma, in.vega, risk);
    blocked += (q.bid_active ? 0U : 1U) + (q.ask_active ? 0U : 1U);
  }
  return blocked;
}

}  // namespace opt::mm
