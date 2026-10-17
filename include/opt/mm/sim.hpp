#pragma once
// Market-making simulator (design 8.1).
//
// Per market state:
//   A  source.next(m)
//   B  mark: per option tau = expiry - t, F = surface.forward(tau),
//      sigma = surface.vol(K, tau); greeks_batch(FastScalar) with spot = F,
//      rate = carry = 0 (Black-76 with discount 1)
//   C  attribute the PnL of the previous step (pnl.hpp)
//   D  quote (quoter.hpp)
//   E  fill draws per active side (fill_model.hpp)
//   F  apply fills: positions, cash, spread capture; a fill that would breach
//      a limit given the fills already applied this step is rejected (counted)
//   G  hedge if |net delta| > threshold (hedger.hpp)
// Latency per quote update = steady_clock time of B + D for the whole option
// set, in a LogLinearHistogram<7> of nanoseconds.
//
// After construction a step allocates nothing (all buffers sized once; the
// recorded source allocates on its own side, outside the simulator).
#include "opt/core/latency_histogram.hpp"
#include "opt/mm/config.hpp"
#include "opt/mm/fill_model.hpp"
#include "opt/mm/hedger.hpp"
#include "opt/mm/market.hpp"
#include "opt/mm/pnl.hpp"
#include "opt/mm/portfolio.hpp"
#include "opt/mm/quoter.hpp"
#include "opt/pricing/types.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace opt::mm {

enum class SimStatus : std::uint8_t { Running, Finished, ArbitrageViolation };

enum class EventKind : std::uint8_t { Fill, Hedge };
enum class Side : std::uint8_t { Bid = 0, Ask = 1 };  // Bid fill = the maker buys

struct Event {
  std::uint64_t step = 0;
  double t_years = 0.0;
  EventKind kind = EventKind::Fill;
  std::size_t instrument = 0;  // fills only
  Side side = Side::Bid;       // hedges: Bid = buy
  double qty = 0.0;            // contracts (fills) or BTC (hedges), signed (+ buy)
  double price = 0.0;          // USD
  double fair = 0.0;           // fills: mark price; hedges: spot
  double spread_capture = 0.0;
  double cost = 0.0;           // hedges
};

using EventFn = void (*)(void* ctx, const Event& event);

struct SimCounters {
  std::uint64_t states = 0;            // market states processed
  std::uint64_t bid_fills = 0;
  std::uint64_t ask_fills = 0;
  std::uint64_t hedges = 0;
  std::uint64_t limit_blocked_sides = 0;   // sides not quoted by the pre-trade check
  std::uint64_t limit_rejected_fills = 0;  // fills dropped by the in-step re-check
  std::uint64_t steps_without_surface = 0;
  std::uint64_t unlisted_instrument_steps = 0;
};

class Simulator {
 public:
  // Throws std::invalid_argument for an empty instrument set or a config
  // that validate() rejects.
  Simulator(const SimConfig& config, std::vector<Instrument> instruments);

  template <MarketSource S>
  SimStatus step(S& source) {
    MarketState m;
    if (!source.next(m)) {
      return SimStatus::Finished;
    }
    return on_state(m);
  }

  template <MarketSource S>
  SimStatus run(S& source) {
    SimStatus st = SimStatus::Running;
    while (st == SimStatus::Running) {
      st = step(source);
    }
    return st;
  }

  // Processes one state (step B..G). Public for tests that feed states.
  SimStatus on_state(const MarketState& m) noexcept;

  void set_event_sink(EventFn fn, void* ctx) noexcept {
    sink_ = fn;
    sink_ctx_ = ctx;
  }

  [[nodiscard]] std::span<const Instrument> instruments() const noexcept { return instruments_; }
  [[nodiscard]] const SimCounters& counters() const noexcept { return counters_; }
  [[nodiscard]] const Portfolio& portfolio() const noexcept { return portfolio_; }
  [[nodiscard]] const PortfolioRisk& risk() const noexcept { return risk_; }  // after hedging
  [[nodiscard]] const PortfolioRisk& pre_hedge_risk() const noexcept { return pre_hedge_risk_; }
  [[nodiscard]] std::span<const Quote> quotes() const noexcept { return quotes_; }
  [[nodiscard]] const Marks& marks() const noexcept { return cur_; }
  [[nodiscard]] double spot() const noexcept { return spot_; }
  [[nodiscard]] double t_years() const noexcept { return t_; }
  [[nodiscard]] double forward(std::size_t i) const noexcept { return fwd_[i]; }
  [[nodiscard]] double tau(std::size_t i) const noexcept { return tau_[i]; }
  [[nodiscard]] std::uint64_t violation_state() const noexcept { return violation_state_; }
  [[nodiscard]] PnlReport pnl() const noexcept { return pnl_.report(); }
  [[nodiscard]] double ledger_pnl() const noexcept {
    return portfolio_.ledger_value(cur_.price, spot_);
  }
  // |attribution total - ledger| and its tolerance.
  [[nodiscard]] double identity_error() const noexcept;
  [[nodiscard]] double identity_tol() const noexcept {
    return identity_tolerance(portfolio_.sum_abs_cash_flows());
  }
  [[nodiscard]] bool identity_holds() const noexcept { return identity_error() <= identity_tol(); }
  [[nodiscard]] const LogLinearHistogram<7>& latency() const noexcept { return latency_; }

 private:
  void mark(const MarketState& m) noexcept;
  void emit(const Event& e) const noexcept {
    if (sink_ != nullptr) {
      sink_(sink_ctx_, e);
    }
  }

  SimConfig config_;
  std::vector<Instrument> instruments_;
  std::size_t n_;
  FillModel fills_;
  Hedger hedger_;
  Portfolio portfolio_;
  PnlAttribution pnl_;
  // Batch inputs (SoA)
  std::vector<OptionType> type_;
  std::vector<double> fwd_, strike_, tau_, zero_, rho_;
  Marks cur_, prev_;
  std::vector<InstrumentView> views_;
  std::vector<Quote> quotes_;
  PortfolioRisk risk_;
  PortfolioRisk pre_hedge_risk_;
  SimCounters counters_;
  LogLinearHistogram<7> latency_;
  double spot_ = 0.0;
  double prev_spot_ = 0.0;
  double t_ = 0.0;
  double prev_t_ = 0.0;
  double dt_sec_ = 0.0;
  bool have_prev_ = false;
  std::uint64_t violation_state_ = 0;
  EventFn sink_ = nullptr;
  void* sink_ctx_ = nullptr;
};

// FNV-1a 64 over fill events, each serialised as 4 little-endian uint64:
// step, instrument, side (0 bid, 1 ask), |qty| in contracts. Used by the
// golden test and printed by mm_sim.
class FillHash {
 public:
  void add(const Event& e) noexcept;
  [[nodiscard]] std::uint64_t value() const noexcept { return h_; }

 private:
  void add_u64(std::uint64_t v) noexcept;
  std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

}  // namespace opt::mm
