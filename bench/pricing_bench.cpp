// Scalar pricing and math-function speed (design 6.7): bsm_price, bsm_greeks,
// norm_cdf vs fast_norm_cdf, std::exp vs fast_exp, std::log vs fast_log, over
// 4,096 seeded inputs per iteration. Counter: ns per call.
#include "opt/core/fast_math.hpp"
#include "opt/core/normal.hpp"
#include "opt/core/rng.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

constexpr std::size_t kCount = 4096;

std::vector<double> uniform_inputs(double lo, double hi, std::uint64_t seed) {
  opt::Xoshiro256pp rng(seed);
  std::vector<double> x(kCount);
  for (auto& v : x) {
    v = opt::uniform(rng, lo, hi);
  }
  return x;
}

std::vector<opt::OptionSpec> specs() {
  opt::Xoshiro256pp rng(31);
  std::vector<opt::OptionSpec> s(kCount);
  for (auto& o : s) {
    o.type = (rng() & 1U) != 0 ? opt::OptionType::Put : opt::OptionType::Call;
    o.spot = 100.0;
    o.strike = opt::uniform(rng, 50.0, 200.0);
    o.time = opt::uniform(rng, 0.02, 2.0);
    o.rate = opt::uniform(rng, 0.0, 0.05);
    o.carry_yield = opt::uniform(rng, 0.0, 0.03);
    o.vol = opt::uniform(rng, 0.1, 0.8);
  }
  return s;
}

void per_call(benchmark::State& state) {
  state.counters["ns/call"] = benchmark::Counter(
      static_cast<double>(kCount) * 1e-9,
      benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

template <class Fn>
void run_unary(benchmark::State& state, const std::vector<double>& x, Fn fn) {
  for (auto _ : state) {
    for (const double v : x) {
      benchmark::DoNotOptimize(fn(v));
    }
  }
  per_call(state);
}

void BM_BsmPrice(benchmark::State& state) {
  static const auto s = specs();
  for (auto _ : state) {
    for (const auto& o : s) {
      benchmark::DoNotOptimize(opt::bsm_price(o));
    }
  }
  per_call(state);
}
BENCHMARK(BM_BsmPrice);

void BM_BsmGreeks(benchmark::State& state) {
  static const auto s = specs();
  for (auto _ : state) {
    for (const auto& o : s) {
      auto g = opt::bsm_greeks(o);
      benchmark::DoNotOptimize(g);
    }
  }
  per_call(state);
}
BENCHMARK(BM_BsmGreeks);

void BM_NormCdf(benchmark::State& state) {
  static const auto x = uniform_inputs(-6.0, 6.0, 41);
  run_unary(state, x, [](double v) { return opt::norm_cdf(v); });
}
BENCHMARK(BM_NormCdf);

void BM_FastNormCdf(benchmark::State& state) {
  static const auto x = uniform_inputs(-6.0, 6.0, 41);
  run_unary(state, x, [](double v) { return opt::fast_norm_cdf(v); });
}
BENCHMARK(BM_FastNormCdf);

void BM_StdExp(benchmark::State& state) {
  static const auto x = uniform_inputs(-20.0, 20.0, 42);
  run_unary(state, x, [](double v) { return std::exp(v); });
}
BENCHMARK(BM_StdExp);

void BM_FastExp(benchmark::State& state) {
  static const auto x = uniform_inputs(-20.0, 20.0, 42);
  run_unary(state, x, [](double v) { return opt::fast_exp(v); });
}
BENCHMARK(BM_FastExp);

void BM_StdLog(benchmark::State& state) {
  static const auto x = uniform_inputs(1e-3, 1e3, 43);
  run_unary(state, x, [](double v) { return std::log(v); });
}
BENCHMARK(BM_StdLog);

void BM_FastLog(benchmark::State& state) {
  static const auto x = uniform_inputs(1e-3, 1e3, 43);
  run_unary(state, x, [](double v) { return opt::fast_log(v); });
}
BENCHMARK(BM_FastLog);

}  // namespace
