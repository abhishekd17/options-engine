// Batch pricing speed (design 6.7): price-only and price+Greeks for the
// Reference, FastScalar, Simd and Threaded (1, 2, 4, 8, all threads) engines
// at n = 4,096 (inputs + outputs about 400 KB, cache resident: measures
// compute) and n = 1,048,576 (about 100 MB: measures memory bandwidth).
//
// Inputs: a seeded chain with S = 100, K uniform [50, 200], T uniform
// [0.02, 2], sigma uniform [0.1, 0.8], r uniform [0, 0.05], q uniform [0, 0.03],
// calls and puts by coin flip. Buffers are 64-byte aligned.
//
// Counters: ns/option (wall time per option) and Mopt/s (million options per
// second).
#include "opt/batch/batch.hpp"
#include "opt/core/rng.hpp"
#include "opt/pricing/types.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <new>
#include <span>
#include <utility>

#include <benchmark/benchmark.h>

namespace {

// n doubles, 64-byte aligned.
struct AlignedDeleter {
  void operator()(double* p) const noexcept { ::operator delete[](p, std::align_val_t{64}); }
};
using AlignedArray = std::unique_ptr<double[], AlignedDeleter>;

AlignedArray aligned(std::size_t n) {
  auto* p = static_cast<double*>(::operator new[](n * sizeof(double), std::align_val_t{64}));
  for (std::size_t i = 0; i < n; ++i) {
    p[i] = 0.0;
  }
  return AlignedArray(p);
}

struct Chain {
  std::size_t n;
  std::unique_ptr<opt::OptionType[]> type;
  AlignedArray spot, strike, time, rate, carry, vol;
  AlignedArray price, delta, gamma, vega, theta, rho;

  explicit Chain(std::size_t count)
      : n(count), type(new opt::OptionType[count]), spot(aligned(count)),
        strike(aligned(count)), time(aligned(count)), rate(aligned(count)),
        carry(aligned(count)), vol(aligned(count)), price(aligned(count)),
        delta(aligned(count)), gamma(aligned(count)), vega(aligned(count)),
        theta(aligned(count)), rho(aligned(count)) {
    opt::Xoshiro256pp rng(2026);
    for (std::size_t i = 0; i < n; ++i) {
      type[i] = (rng() & 1U) != 0 ? opt::OptionType::Put : opt::OptionType::Call;
      spot[i] = 100.0;
      strike[i] = opt::uniform(rng, 50.0, 200.0);
      time[i] = opt::uniform(rng, 0.02, 2.0);
      rate[i] = opt::uniform(rng, 0.0, 0.05);
      carry[i] = opt::uniform(rng, 0.0, 0.03);
      vol[i] = opt::uniform(rng, 0.1, 0.8);
    }
  }
  [[nodiscard]] opt::OptionBatch in() const {
    return {{type.get(), n},  {spot.get(), n},  {strike.get(), n}, {time.get(), n},
            {rate.get(), n},  {carry.get(), n}, {vol.get(), n}};
  }
  [[nodiscard]] opt::GreeksBatch out() const {
    return {{price.get(), n}, {delta.get(), n}, {gamma.get(), n},
            {vega.get(), n},  {theta.get(), n}, {rho.get(), n}};
  }
};

const Chain& chain(std::size_t n) {
  static std::map<std::size_t, std::unique_ptr<Chain>> chains;
  auto& c = chains[n];
  if (!c) {
    c = std::make_unique<Chain>(n);
  }
  return *c;
}

void set_counters(benchmark::State& state, std::size_t n) {
  const double count = static_cast<double>(n);
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(n));
  state.counters["ns/option"] =
      benchmark::Counter(count * 1e-9, benchmark::Counter::kIsIterationInvariantRate |
                                           benchmark::Counter::kInvert);
  state.counters["Mopt/s"] =
      benchmark::Counter(count * 1e-6, benchmark::Counter::kIsIterationInvariantRate);
}

template <opt::Engine kEngine>
void BM_Price(benchmark::State& state) {
  const auto& c = chain(static_cast<std::size_t>(state.range(0)));
  const auto in = c.in();
  const std::span<double> out(c.price.get(), c.n);
  for (auto _ : state) {
    opt::price_batch(kEngine, in, out);
    benchmark::DoNotOptimize(out.data());
    benchmark::ClobberMemory();
  }
  set_counters(state, c.n);
}

template <opt::Engine kEngine>
void BM_Greeks(benchmark::State& state) {
  const auto& c = chain(static_cast<std::size_t>(state.range(0)));
  const auto in = c.in();
  const auto out = c.out();
  for (auto _ : state) {
    opt::greeks_batch(kEngine, in, out);
    benchmark::DoNotOptimize(out.price.data());
    benchmark::ClobberMemory();
  }
  set_counters(state, c.n);
}

// range(0) = n, range(1) = threads (0 = all hardware threads).
void BM_ThreadedPrice(benchmark::State& state) {
  const auto& c = chain(static_cast<std::size_t>(state.range(0)));
  opt::ThreadedPricer pricer(static_cast<unsigned>(state.range(1)));
  const auto in = c.in();
  const std::span<double> out(c.price.get(), c.n);
  for (auto _ : state) {
    pricer.price(in, out);
    benchmark::DoNotOptimize(out.data());
    benchmark::ClobberMemory();
  }
  set_counters(state, c.n);
  state.counters["pool_threads"] = pricer.threads();
}

void BM_ThreadedGreeks(benchmark::State& state) {
  const auto& c = chain(static_cast<std::size_t>(state.range(0)));
  opt::ThreadedPricer pricer(static_cast<unsigned>(state.range(1)));
  const auto in = c.in();
  const auto out = c.out();
  for (auto _ : state) {
    pricer.greeks(in, out);
    benchmark::DoNotOptimize(out.price.data());
    benchmark::ClobberMemory();
  }
  set_counters(state, c.n);
  state.counters["pool_threads"] = pricer.threads();
}

constexpr std::int64_t kSmall = 4096;
constexpr std::int64_t kLarge = 1 << 20;

BENCHMARK(BM_Price<opt::Engine::Reference>)->Name("BM_Price/Reference")->Arg(kSmall)->Arg(kLarge);
BENCHMARK(BM_Price<opt::Engine::FastScalar>)->Name("BM_Price/FastScalar")->Arg(kSmall)->Arg(kLarge);
BENCHMARK(BM_Price<opt::Engine::Simd>)->Name("BM_Price/Simd")->Arg(kSmall)->Arg(kLarge);
BENCHMARK(BM_Greeks<opt::Engine::Reference>)->Name("BM_Greeks/Reference")->Arg(kSmall)->Arg(kLarge);
BENCHMARK(BM_Greeks<opt::Engine::FastScalar>)->Name("BM_Greeks/FastScalar")->Arg(kSmall)->Arg(kLarge);
BENCHMARK(BM_Greeks<opt::Engine::Simd>)->Name("BM_Greeks/Simd")->Arg(kSmall)->Arg(kLarge);

void threaded_args(benchmark::Benchmark* b) {
  b->ArgNames({"n", "threads"});
  for (const std::int64_t n : {kSmall, kLarge}) {
    for (const std::int64_t t : {1, 2, 4, 8, 0}) {
      b->Args({n, t});
    }
  }
  b->UseRealTime();
}
BENCHMARK(BM_ThreadedPrice)->Apply(threaded_args);
BENCHMARK(BM_ThreadedGreeks)->Apply(threaded_args);

}  // namespace
