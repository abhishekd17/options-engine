#include "opt/mm/sim.hpp"

#include "opt/batch/batch.hpp"
#include "opt/mm/config.hpp"
#include "opt/mm/market.hpp"
#include "opt/mm/pnl.hpp"
#include "opt/mm/portfolio.hpp"
#include "opt/mm/quoter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace opt::mm {
namespace {

constexpr double kMinTau = 1e-8;  // batch-engine domain

std::size_t checked_size(const SimConfig& config, const std::vector<Instrument>& instruments) {
  if (instruments.empty()) {
    throw std::invalid_argument("Simulator: empty instrument set");
  }
  if (const std::string err = validate(config); !err.empty()) {
    throw std::invalid_argument("Simulator: " + err);
  }
  return instruments.size();
}

void copy_marks(const Marks& from, Marks& to) noexcept {
  std::copy(from.price.begin(), from.price.end(), to.price.begin());
  std::copy(from.delta.begin(), from.delta.end(), to.delta.begin());
  std::copy(from.gamma.begin(), from.gamma.end(), to.gamma.begin());
  std::copy(from.vega.begin(), from.vega.end(), to.vega.begin());
  std::copy(from.theta.begin(), from.theta.end(), to.theta.begin());
  std::copy(from.vol.begin(), from.vol.end(), to.vol.begin());
}

}  // namespace

Simulator::Simulator(const SimConfig& config, std::vector<Instrument> instruments)
    : config_(config),
      instruments_(std::move(instruments)),
      n_(checked_size(config_, instruments_)),
      fills_(config.fill, config.seed),
      hedger_(config.hedge),
      portfolio_(n_),
      type_(n_),
      fwd_(n_),
      strike_(n_),
      tau_(n_),
      zero_(n_, 0.0),
      rho_(n_),
      cur_(n_),
      prev_(n_),
      views_(n_),
      quotes_(n_) {
  for (std::size_t i = 0; i < n_; ++i) {
    type_[i] = instruments_[i].type;
    strike_[i] = instruments_[i].strike;
  }
}

void Simulator::mark(const MarketState& m) noexcept {
  for (std::size_t i = 0; i < n_; ++i) {
    const bool listed = m.listed.empty() || m.listed[i] != 0;
    const double tau = instruments_[i].expiry - m.t_years;
    // Unlisted (or expired) instruments keep their last batch inputs, so the
    // batch call reproduces their last mark bit for bit.
    if (!listed || !(tau >= kMinTau)) {
      if (have_prev_) {
        ++counters_.unlisted_instrument_steps;
        continue;
      }
    }
    tau_[i] = std::max(tau, kMinTau);
    fwd_[i] = m.surface->forward(tau_[i]);
    cur_.vol[i] = m.surface->vol(strike_[i], tau_[i]);
  }
  const OptionBatch in{type_, fwd_, strike_, tau_, zero_, zero_, cur_.vol};
  const GreeksBatch out{cur_.price, cur_.delta, cur_.gamma, cur_.vega, cur_.theta, rho_};
  greeks_batch(Engine::FastScalar, in, out);
}

SimStatus Simulator::on_state(const MarketState& m) noexcept {
  const std::uint64_t step = counters_.states;
  if (!m.arbitrage_ok) {
    violation_state_ = step;
    return SimStatus::ArbitrageViolation;
  }
  ++counters_.states;
  if (!m.surface_ok) {
    ++counters_.steps_without_surface;
  }
  spot_ = m.spot;
  t_ = m.t_years;

  // B: mark
  using Clock = std::chrono::steady_clock;
  const auto t0 = Clock::now();
  mark(m);
  const auto t1 = Clock::now();

  // C: attribute the previous step
  if (have_prev_) {
    pnl_.add(attribute_step(portfolio_.positions(), portfolio_.hedge(), prev_spot_, spot_,
                            t_ - prev_t_, prev_, cur_));
    dt_sec_ = (t_ - prev_t_) * kSecondsPerYear;
  } else {
    dt_sec_ = config_.mode == Mode::Synthetic ? config_.dt_sec : 0.0;
  }

  // D: quote
  const auto t2 = Clock::now();
  risk_ = compute_risk(portfolio_.positions(), portfolio_.hedge(), cur_, spot_);
  const auto pos = portfolio_.positions();
  for (std::size_t i = 0; i < n_; ++i) {
    InstrumentView& v = views_[i];
    v.type = type_[i];
    v.forward = fwd_[i];
    v.strike = strike_[i];
    v.tau = tau_[i];
    v.fair_vol = cur_.vol[i];
    v.delta = cur_.delta[i];
    v.gamma = cur_.gamma[i];
    v.vega = cur_.vega[i];
    v.position = pos[i];
    const bool listed = m.listed.empty() || m.listed[i] != 0;
    v.quotable = m.surface_ok && listed && instruments_[i].expiry - t_ >= kMinTau;
  }
  counters_.limit_blocked_sides +=
      make_quotes(config_.quoter, config_.limits, views_, risk_, quotes_);
  const auto t3 = Clock::now();
  latency_.record(static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>((t1 - t0) + (t3 - t2)).count()));

  // E + F: fills. The fill stream is drawn for every active side; a fill is
  // applied only if it keeps every limit given the fills already applied.
  PortfolioRisk running = risk_;
  const std::int64_t size = config_.quoter.size;
  for (std::size_t i = 0; i < n_ && dt_sec_ > 0.0; ++i) {
    const Quote& q = quotes_[i];
    for (const Side side : {Side::Bid, Side::Ask}) {
      const bool active = side == Side::Bid ? q.bid_active : q.ask_active;
      if (!active) {
        continue;
      }
      const double qvol = side == Side::Bid ? q.bid_vol : q.ask_vol;
      if (!fills_.draw(std::abs(qvol - cur_.vol[i]) * 100.0, dt_sec_)) {
        continue;
      }
      const std::int64_t qty = side == Side::Bid ? size : -size;
      if (!fill_within_limits(config_.limits, portfolio_.positions()[i], qty, cur_.delta[i],
                              cur_.gamma[i], cur_.vega[i], running)) {
        ++counters_.limit_rejected_fills;
        continue;
      }
      const double price = side == Side::Bid ? q.bid_price : q.ask_price;
      const double fair = cur_.price[i];
      const double capture = side == Side::Bid ? (fair - price) * static_cast<double>(size)
                                               : (price - fair) * static_cast<double>(size);
      portfolio_.apply_fill(i, qty, price);
      pnl_.add_spread_capture(capture);
      const auto dq = static_cast<double>(qty);
      running.net_delta += dq * cur_.delta[i];
      running.vega_volpt += dq * cur_.vega[i] / 100.0;
      running.gamma_1pct += dq * gamma_1pct(cur_.gamma[i], spot_);
      ++(side == Side::Bid ? counters_.bid_fills : counters_.ask_fills);
      Event e;
      e.step = step;
      e.t_years = t_;
      e.kind = EventKind::Fill;
      e.instrument = i;
      e.side = side;
      e.qty = dq;
      e.price = price;
      e.fair = fair;
      e.spread_capture = capture;
      emit(e);
    }
  }

  // G: hedge
  pre_hedge_risk_ = compute_risk(portfolio_.positions(), portfolio_.hedge(), cur_, spot_);
  risk_ = pre_hedge_risk_;
  if (const auto h = hedger_.maybe_hedge(pre_hedge_risk_.net_delta, spot_)) {
    portfolio_.apply_hedge(h->qty, h->price, h->cost);
    pnl_.add_hedge_cost(h->cost);
    ++counters_.hedges;
    risk_ = compute_risk(portfolio_.positions(), portfolio_.hedge(), cur_, spot_);
    Event e;
    e.step = step;
    e.t_years = t_;
    e.kind = EventKind::Hedge;
    e.side = h->qty > 0.0 ? Side::Bid : Side::Ask;
    e.qty = h->qty;
    e.price = h->price;
    e.fair = spot_;
    e.cost = h->cost;
    emit(e);
  }

  copy_marks(cur_, prev_);
  prev_spot_ = spot_;
  prev_t_ = t_;
  have_prev_ = true;
  return SimStatus::Running;
}

double Simulator::identity_error() const noexcept {
  return std::abs(pnl_.report().total - ledger_pnl());
}

void FillHash::add_u64(std::uint64_t v) noexcept {
  for (int b = 0; b < 8; ++b) {
    h_ ^= (v >> (8 * b)) & 0xffU;
    h_ *= 0x100000001b3ULL;
  }
}

void FillHash::add(const Event& e) noexcept {
  if (e.kind != EventKind::Fill) {
    return;
  }
  add_u64(e.step);
  add_u64(static_cast<std::uint64_t>(e.instrument));
  add_u64(static_cast<std::uint64_t>(e.side));
  add_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(std::abs(e.qty))));
}

}  // namespace opt::mm
