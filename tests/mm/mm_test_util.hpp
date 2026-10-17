#pragma once
// Shared helpers for the market-maker tests: a run recorder that hashes fill
// events and checks the limits and the hedge rule after every event.
#include "opt/mm/config.hpp"
#include "opt/mm/portfolio.hpp"
#include "opt/mm/sim.hpp"
#include "opt/mm/synthetic.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace opt::mm::test {

// Records every event of a Simulator and checks, at each fill, the post-fill
// (pre-hedge) limits and, at each hedge, the strict threshold rule. The
// simulator applies a fill before it emits the event, so compute_risk on the
// simulator's state is the risk right after that fill.
struct Recorder {
  const Simulator* sim = nullptr;
  LimitConfig limits;
  double threshold = 0.0;
  FillHash hash;
  std::uint64_t fills = 0;
  std::uint64_t hedges = 0;
  std::uint64_t limit_failures = 0;
  std::uint64_t hedge_failures = 0;
  double max_abs_position = 0.0;
  double max_abs_vega = 0.0;
  double max_abs_gamma = 0.0;
  double max_abs_delta = 0.0;

  static void on_event(void* ctx, const Event& e) {
    static_cast<Recorder*>(ctx)->check(e);
  }

  void check(const Event& e) {
    hash.add(e);
    if (e.kind == EventKind::Hedge) {
      ++hedges;
      // The hedge decision used the pre-hedge risk; after it, delta is ~0.
      if (!(std::abs(sim->pre_hedge_risk().net_delta) > threshold) ||
          std::abs(sim->risk().net_delta) > 1e-9) {
        ++hedge_failures;
      }
      return;
    }
    ++fills;
    const auto r = compute_risk(sim->portfolio().positions(), sim->portfolio().hedge(),
                                sim->marks(), sim->spot());
    const auto q = static_cast<double>(sim->portfolio().positions()[e.instrument]);
    constexpr double kRel = 1e-9;  // incremental vs recomputed risk sums
    max_abs_position = std::max(max_abs_position, std::abs(q));
    max_abs_vega = std::max(max_abs_vega, std::abs(r.vega_volpt));
    max_abs_gamma = std::max(max_abs_gamma, std::abs(r.gamma_1pct));
    max_abs_delta = std::max(max_abs_delta, std::abs(r.net_delta));
    if (std::abs(q) > static_cast<double>(limits.max_position) ||
        std::abs(r.vega_volpt) > limits.max_vega * (1 + kRel) ||
        std::abs(r.gamma_1pct) > limits.max_gamma_1pct * (1 + kRel) ||
        std::abs(r.net_delta) > limits.max_net_delta * (1 + kRel)) {
      ++limit_failures;
    }
  }
};

struct SyntheticRun {
  std::unique_ptr<SyntheticMarket> market;
  std::unique_ptr<Simulator> sim;
  std::unique_ptr<Recorder> rec;
  SimStatus status = SimStatus::Running;
};

inline SyntheticRun run_synthetic(const SimConfig& config) {
  SyntheticRun r;
  r.market = std::make_unique<SyntheticMarket>(config);
  r.sim = std::make_unique<Simulator>(config, synthetic_instruments(config));
  r.rec = std::make_unique<Recorder>();
  r.rec->sim = r.sim.get();
  r.rec->limits = config.limits;
  r.rec->threshold = config.hedge.threshold;
  r.sim->set_event_sink(&Recorder::on_event, r.rec.get());
  r.status = r.sim->run(*r.market);
  return r;
}

}  // namespace opt::mm::test
