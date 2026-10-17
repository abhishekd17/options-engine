// mm_sim: runs the options market maker on the synthetic generator or on a
// recorded series of Deribit snapshots, and prints quotes, fills, hedges,
// Greeks over time, the PnL attribution and the latency per quote update.
//
// Everything except the latency section is deterministic for a given seed and
// input; --no-latency drops that section so tests can compare full output.
#include "opt/mm/config.hpp"
#include "opt/mm/market.hpp"
#include "opt/mm/recorded.hpp"
#include "opt/mm/sim.hpp"
#include "opt/mm/synthetic.hpp"
#include "opt/pricing/types.hpp"
#include "args.hpp"

#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using opt::mm::Event;
using opt::mm::EventKind;
using opt::mm::MarketState;
using opt::mm::Mode;
using opt::mm::Side;
using opt::mm::SimConfig;
using opt::mm::SimStatus;
using opt::mm::Simulator;
using opt::tools::UsageError;

struct Options {
  SimConfig config;
  std::optional<fs::path> series;
  std::optional<fs::path> events;
  std::uint64_t print_every = 0;  // 0 = mode default
  bool latency = true;
};

void usage(std::FILE* out) {
  std::fprintf(
      out,
      "usage: mm_sim --mode synthetic|recorded [--series DIR] [--seed N] [--steps N]\n"
      "              [--dt-sec S] [--print-every N] [--events FILE.csv] [--no-latency]\n"
      "              [config flags]\n"
      "Runs the options market maker and prints quotes, fills, hedges, Greeks over\n"
      "time, the PnL attribution and the latency per quote update.\n"
      "  --mode M             synthetic (default) or recorded\n"
      "  --series DIR         recorded mode: directory of Deribit snapshots\n"
      "  --seed N             RNG seed (default 42)\n"
      "  --steps N            synthetic steps (default 86400)\n"
      "  --dt-sec S           synthetic step in seconds (default 1)\n"
      "  --print-every N      status line every N states (default 3600 synthetic, 1 recorded)\n"
      "  --events FILE.csv    write fill and hedge events to a CSV instead of stdout\n"
      "  --no-latency         omit the latency section\n"
      "config flags (defaults in docs/mm_model.md):\n"
      "  --quote-size N --half-spread VOLPTS --max-position N --max-delta BTC\n"
      "  --max-vega USD --max-gamma BTC --hedge-threshold BTC --hedge-cost-bps BPS\n"
      "  --fill-intensity PER_SEC --fill-kappa PER_VOLPT --spot0 USD --vol0 VOL\n"
      "  --vol-xi PER_SQRT_YEAR\n");
}

const char* type_char(opt::OptionType t) { return t == opt::OptionType::Call ? "call" : "put"; }

struct EventPrinter {
  std::FILE* out = stdout;
  bool csv = false;
  const Simulator* sim = nullptr;
  opt::mm::FillHash hash;

  static void on_event(void* ctx, const Event& e) {
    auto* self = static_cast<EventPrinter*>(ctx);
    self->hash.add(e);
    self->print(e);
  }

  void print(const Event& e) const {
    const bool fill = e.kind == EventKind::Fill;
    if (csv) {
      std::fprintf(out, "%" PRIu64 ",%.9g,%s,%s,%s,%.10g,%.10g,%.10g,%.10g,%.10g\n", e.step,
                   e.t_years, fill ? "fill" : "hedge",
                   fill ? sim->instruments()[e.instrument].name.c_str() : "",
                   e.side == Side::Bid ? "buy" : "sell", e.qty, e.price, e.fair,
                   e.spread_capture, e.cost);
      return;
    }
    if (fill) {
      std::fprintf(out,
                   "fill   state %7" PRIu64 "  %-22s %s %2.0f @ %10.4f  fair %10.4f  capture "
                   "%+9.4f\n",
                   e.step, sim->instruments()[e.instrument].name.c_str(),
                   e.side == Side::Bid ? "buy " : "sell", std::abs(e.qty), e.price, e.fair,
                   e.spread_capture);
    } else {
      std::fprintf(out, "hedge  state %7" PRIu64 "  %s %.6f BTC @ %.2f  cost %.4f\n", e.step,
                   e.side == Side::Bid ? "buy " : "sell", std::abs(e.qty), e.price, e.cost);
    }
  }
};

// Remembers the last market state so the status line can show the ATM vol.
template <class S>
struct Capture {
  S* source;
  MarketState last;
  bool next(MarketState& m) {
    const bool ok = source->next(m);
    if (ok) {
      last = m;
    }
    return ok;
  }
};

void print_config(const Options& o, const Simulator& sim) {
  const SimConfig& c = o.config;
  const auto& q = c.quoter;
  const auto& l = c.limits;
  std::printf("config: quote size %" PRId64
              "  half spread %.2f volpt  w_inv %.2f w_vega %.2f w_gamma %.2f  k_inv %.2f "
              "k_vega %.2f\n",
              q.size, q.half_spread, q.w_inv, q.w_vega, q.w_gamma, q.k_inv, q.k_vega);
  std::printf("limits: |position| %" PRId64
              "  |net delta| %.3f BTC  |vega| %.1f USD/volpt  |gamma_1pct| %.3f BTC\n",
              l.max_position, l.max_net_delta, l.max_vega, l.max_gamma_1pct);
  std::printf("hedge: threshold %.3f BTC  cost %.2f bps   fills: A %.6g /s  kappa %.3f /volpt\n",
              c.hedge.threshold, c.hedge.cost_bps, c.fill.intensity, c.fill.kappa);
  if (c.mode == Mode::Synthetic) {
    const auto& s = c.synthetic;
    std::printf("synthetic: S0 %.2f  v0 %.4f  vol mean %.4f  kappa %.3f /yr  xi %.3f /sqrt(yr)  "
                "rho %.3f  expiries",
                s.spot0, s.vol0, s.vol_mean, s.vol_kappa, s.vol_xi, s.rho_sv);
    for (const double d : s.expiry_days) {
      std::printf(" %gd", d);
    }
    std::printf("  SVI beta %.3f rho %.3f m %.3f s %.3f\n", s.svi_beta, s.svi_rho, s.svi_m,
                s.svi_s);
  }
  std::printf("instruments %zu:\n", sim.instruments().size());
  for (std::size_t i = 0; i < sim.instruments().size(); ++i) {
    const auto& in = sim.instruments()[i];
    std::printf("  [%zu] %-22s %-4s K %9.1f  expiry %.4f d\n", i, in.name.c_str(),
                type_char(in.type), in.strike, in.expiry * 365.0);
  }
}

void print_header() {
  std::printf("%8s %9s %10s %7s %9s %10s %10s %10s %4s  %s\n", "state", "t_hours", "spot",
              "atm_vol", "net_delta", "gamma_1pct", "vega/volpt", "theta/day", "open",
              "quotes [0] and [1]: bid/ask vol pts (price)");
}

void print_status(const Simulator& sim, const MarketState& m) {
  const auto& r = sim.risk();
  double atm = 0.0;
  if (m.surface != nullptr) {
    atm = m.surface->vol(sim.forward(0), sim.tau(0));
  }
  int open = 0;
  for (const auto q : sim.portfolio().positions()) {
    open += q != 0 ? 1 : 0;
  }
  std::printf("%8" PRIu64 " %9.4f %10.2f %7.4f %+9.4f %+10.5f %+10.3f %+10.3f %4d ",
              sim.counters().states - 1, sim.t_years() * 8760.0, sim.spot(), atm, r.net_delta,
              r.gamma_1pct, r.vega_volpt, r.theta_day, open);
  const auto quotes = sim.quotes();
  for (std::size_t i = 0; i < 2 && i < quotes.size(); ++i) {
    const auto& q = quotes[i];
    std::printf(" %s%.2f/%.2f%s (%.2f/%.2f)", q.bid_active ? "" : "[", q.bid_vol * 100.0,
                q.ask_vol * 100.0, q.ask_active ? "" : "]", q.bid_price, q.ask_price);
  }
  std::printf("\n");
}

void print_report(const Simulator& sim, const EventPrinter& ev) {
  const auto& c = sim.counters();
  std::printf("final report\n");
  std::printf("  states %" PRIu64 "  fills bid %" PRIu64 " ask %" PRIu64 "  hedges %" PRIu64
              "  limit-blocked sides %" PRIu64 "  limit-rejected fills %" PRIu64 "\n",
              c.states, c.bid_fills, c.ask_fills, c.hedges, c.limit_blocked_sides,
              c.limit_rejected_fills);
  std::printf("  steps without surface %" PRIu64 "  unlisted instrument-steps %" PRIu64 "\n",
              c.steps_without_surface, c.unlisted_instrument_steps);
  std::printf("  fill hash 0x%016" PRIx64 "\n", ev.hash.value());
  std::printf("  final positions:");
  for (const auto q : sim.portfolio().positions()) {
    std::printf(" %" PRId64, q);
  }
  std::printf("\n  hedge position %.6f BTC  final spot %.4f  final t %.6f d\n",
              sim.portfolio().hedge(), sim.spot(), sim.t_years() * 365.0);
  const auto& r = sim.risk();
  std::printf("  final greeks: net delta %+.4f BTC  gamma_1pct %+.5f BTC  vega %+.3f USD/volpt  "
              "theta %+.3f USD/day\n",
              r.net_delta, r.gamma_1pct, r.vega_volpt, r.theta_day);
  const auto p = sim.pnl();
  std::printf("PnL attribution (USD):\n");
  std::printf("  delta            %+16.6f\n", p.delta);
  std::printf("  gamma            %+16.6f\n", p.gamma);
  std::printf("  vega             %+16.6f\n", p.vega);
  std::printf("  theta            %+16.6f\n", p.theta);
  std::printf("  residual         %+16.6f\n", p.residual);
  std::printf("  spread capture   %+16.6f\n", p.spread_capture);
  std::printf("  hedge cost       %+16.6f\n", -p.hedge_cost);
  std::printf("  total            %+16.6f\n", p.total);
  std::printf("  ledger total     %+16.6f\n", sim.ledger_pnl());
  std::printf("  difference       %.3e  (tolerance %.3e: %s)\n", sim.identity_error(),
              sim.identity_tol(), sim.identity_holds() ? "ok" : "VIOLATED");
}

void print_latency(const Simulator& sim) {
  const auto& h = sim.latency();
  const auto n = static_cast<double>(sim.instruments().size());
  std::printf("latency per quote update (mark + quote of %zu options, steady_clock ns):\n",
              sim.instruments().size());
  std::printf("  count %" PRIu64 "  p50 %" PRIu64 "  p90 %" PRIu64 "  p99 %" PRIu64
              "  p99.9 %" PRIu64 "  max %" PRIu64 "\n",
              h.count(), h.value_at_percentile(50), h.value_at_percentile(90),
              h.value_at_percentile(99), h.value_at_percentile(99.9), h.max());
  std::printf("  per option: p50 %.1f  p90 %.1f  p99 %.1f  p99.9 %.1f  max %.1f\n",
              static_cast<double>(h.value_at_percentile(50)) / n,
              static_cast<double>(h.value_at_percentile(90)) / n,
              static_cast<double>(h.value_at_percentile(99)) / n,
              static_cast<double>(h.value_at_percentile(99.9)) / n,
              static_cast<double>(h.max()) / n);
}

template <class S>
int simulate(const Options& o, S& source, std::vector<opt::mm::Instrument> instruments) {
  auto sim = std::make_unique<Simulator>(o.config, std::move(instruments));
  std::unique_ptr<std::FILE, int (*)(std::FILE*)> csv_file{nullptr, &std::fclose};
  EventPrinter ev;
  ev.sim = sim.get();
  if (o.events) {
    csv_file.reset(std::fopen(o.events->string().c_str(), "w"));
    if (!csv_file) {
      throw std::runtime_error(o.events->string() + ": cannot open for writing");
    }
    ev.out = csv_file.get();
    ev.csv = true;
    std::fprintf(ev.out, "step,t,kind,instrument,side,qty,price,fair,spread_capture,cost\n");
  }
  sim->set_event_sink(&EventPrinter::on_event, &ev);
  print_config(o, *sim);
  const std::uint64_t every =
      o.print_every != 0 ? o.print_every : (o.config.mode == Mode::Synthetic ? 3600 : 1);
  Capture<S> cap{&source, {}};
  print_header();
  SimStatus st = SimStatus::Running;
  bool printed_last = false;
  while ((st = sim->step(cap)) == SimStatus::Running) {
    printed_last = (sim->counters().states - 1) % every == 0;
    if (printed_last) {
      print_status(*sim, cap.last);
    }
  }
  if (st == SimStatus::ArbitrageViolation) {
    std::printf("static-arbitrage guard failed at state %" PRIu64 "; run stopped\n",
                sim->violation_state());
    return 1;
  }
  if (!printed_last) {
    print_status(*sim, cap.last);
  }
  if (o.events) {
    std::printf("events written to %s\n", o.events->string().c_str());
  }
  print_report(*sim, ev);
  if (o.latency) {
    print_latency(*sim);
  }
  return sim->identity_holds() ? 0 : 1;
}

int run(const Options& o) {
  const SimConfig& c = o.config;
  std::printf("mm_sim: mode %s  seed %" PRIu64, c.mode == Mode::Synthetic ? "synthetic" : "recorded",
              c.seed);
  if (c.mode == Mode::Synthetic) {
    std::printf("  steps %" PRIu64 "  dt %g s\n", c.steps, c.dt_sec);
    opt::mm::SyntheticMarket market(c);
    return simulate(o, market, opt::mm::synthetic_instruments(c));
  }
  std::printf("  series %s\n", o.series->string().c_str());
  opt::mm::RecordedSource source(*o.series, c);
  std::printf("snapshots %zu\n", source.snapshot_count());
  return simulate(o, source, source.instruments());
}

Options parse(int argc, char** argv) {
  using opt::tools::parse_double;
  using opt::tools::parse_u64;
  Options o;
  SimConfig& c = o.config;
  opt::tools::ArgIterator it{argc, argv};
  while (!it.done()) {
    const std::string_view a = it.next();
    if (a == "--mode") {
      const std::string_view v = it.value(a);
      if (v == "synthetic") {
        c.mode = Mode::Synthetic;
      } else if (v == "recorded") {
        c.mode = Mode::Recorded;
      } else {
        throw UsageError("--mode: expected synthetic or recorded, got '" + std::string{v} + "'");
      }
    } else if (a == "--series") {
      o.series = fs::path{std::string{it.value(a)}};
    } else if (a == "--seed") {
      c.seed = parse_u64(a, it.value(a), 0, UINT64_MAX);
    } else if (a == "--steps") {
      c.steps = parse_u64(a, it.value(a), 1, 100'000'000);
    } else if (a == "--dt-sec") {
      c.dt_sec = parse_double(a, it.value(a), 1e-3, 86'400.0);
    } else if (a == "--print-every") {
      o.print_every = parse_u64(a, it.value(a), 1, UINT64_MAX);
    } else if (a == "--events") {
      o.events = fs::path{std::string{it.value(a)}};
    } else if (a == "--no-latency") {
      o.latency = false;
    } else if (a == "--quote-size") {
      c.quoter.size = static_cast<std::int64_t>(parse_u64(a, it.value(a), 1, 1000));
    } else if (a == "--half-spread") {
      c.quoter.half_spread = parse_double(a, it.value(a), 0.0, 100.0);
    } else if (a == "--max-position") {
      c.limits.max_position = static_cast<std::int64_t>(parse_u64(a, it.value(a), 1, 1'000'000));
    } else if (a == "--max-delta") {
      c.limits.max_net_delta = parse_double(a, it.value(a), 1e-9, 1e9);
    } else if (a == "--max-vega") {
      c.limits.max_vega = parse_double(a, it.value(a), 1e-9, 1e12);
    } else if (a == "--max-gamma") {
      c.limits.max_gamma_1pct = parse_double(a, it.value(a), 1e-9, 1e9);
    } else if (a == "--hedge-threshold") {
      c.hedge.threshold = parse_double(a, it.value(a), 0.0, 1e9);
    } else if (a == "--hedge-cost-bps") {
      c.hedge.cost_bps = parse_double(a, it.value(a), 0.0, 1e4);
    } else if (a == "--fill-intensity") {
      c.fill.intensity = parse_double(a, it.value(a), 0.0, 1e3);
    } else if (a == "--fill-kappa") {
      c.fill.kappa = parse_double(a, it.value(a), 0.0, 1e3);
    } else if (a == "--spot0") {
      c.synthetic.spot0 = parse_double(a, it.value(a), 1e-3, 1e9);
    } else if (a == "--vol0") {
      c.synthetic.vol0 = parse_double(a, it.value(a), 1e-4, 5.0);
    } else if (a == "--vol-xi") {
      c.synthetic.vol_xi = parse_double(a, it.value(a), 0.0, 100.0);
    } else {
      throw UsageError("unknown flag '" + std::string{a} + "'");
    }
  }
  if (c.mode == Mode::Recorded && !o.series) {
    throw UsageError("--mode recorded needs --series DIR");
  }
  if (c.mode == Mode::Synthetic && o.series) {
    throw UsageError("--series is only valid with --mode recorded");
  }
  if (const std::string err = opt::mm::validate(c); !err.empty()) {
    throw UsageError("invalid config: " + err);
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string_view{argv[i]} == "--help" || std::string_view{argv[i]} == "-h") {
      usage(stdout);
      return 0;
    }
  }
  Options o;
  try {
    o = parse(argc, argv);
  } catch (const UsageError& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    usage(stderr);
    return 2;
  }
  try {
    return run(o);
  } catch (const std::exception& e) {
    std::fflush(stdout);
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
