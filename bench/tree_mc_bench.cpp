// CRR tree and Monte Carlo speed (design 6.7).
#include "opt/pricing/binomial.hpp"
#include "opt/pricing/monte_carlo.hpp"
#include "opt/pricing/types.hpp"

#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

const opt::OptionSpec kPut{opt::OptionType::Put, 100, 105, 1.0, 0.05, 0.01, 0.25};

void BM_CrrEuropean(benchmark::State& state) {
  const auto steps = static_cast<int>(state.range(0));
  std::vector<double> ws(static_cast<std::size_t>(steps) + 1);  // preallocated
  for (auto _ : state) {
    auto r = opt::crr_price(kPut, opt::Exercise::European, steps, ws);
    benchmark::DoNotOptimize(r.price);
  }
  state.counters["nodes/s"] = benchmark::Counter(0.5 * steps * (steps + 1.0),
                                                 benchmark::Counter::kIsIterationInvariantRate);
}
BENCHMARK(BM_CrrEuropean)->Arg(100)->Arg(500)->Arg(2000)->Arg(10000);

void BM_CrrAmerican(benchmark::State& state) {
  const auto steps = static_cast<int>(state.range(0));
  std::vector<double> ws(static_cast<std::size_t>(steps) + 1);
  for (auto _ : state) {
    auto r = opt::crr_price(kPut, opt::Exercise::American, steps, ws);
    benchmark::DoNotOptimize(r.price);
  }
  state.counters["nodes/s"] = benchmark::Counter(0.5 * steps * (steps + 1.0),
                                                 benchmark::Counter::kIsIterationInvariantRate);
}
BENCHMARK(BM_CrrAmerican)->Arg(100)->Arg(500)->Arg(2000)->Arg(10000);

// Arg 0: plain Monte Carlo; arg 1: antithetic + control variate.
void BM_MonteCarlo(benchmark::State& state) {
  constexpr std::uint64_t kPaths = 1'000'000;
  opt::McConfig c;
  c.paths = kPaths;
  c.seed = 5;
  c.antithetic = state.range(0) != 0;
  c.control_variate = state.range(0) != 0;
  double se = 0.0;
  for (auto _ : state) {
    auto r = opt::mc_european(kPut, c);
    benchmark::DoNotOptimize(r.price);
    se = r.std_error;
  }
  state.counters["paths/s"] =
      benchmark::Counter(static_cast<double>(kPaths), benchmark::Counter::kIsIterationInvariantRate);
  state.counters["std_error"] = se;
}
BENCHMARK(BM_MonteCarlo)->ArgName("variance_reduction")->Arg(0)->Arg(1)->Unit(benchmark::kMillisecond);

}  // namespace
