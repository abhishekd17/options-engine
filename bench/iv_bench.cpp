// Implied-vol solver speed (design 6.7): implied_vol_black over 4,096 seeded
// prices, near-the-money and wings as separate benchmarks.
#include "opt/core/rng.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/implied_vol.hpp"
#include "opt/pricing/types.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

constexpr std::size_t kCount = 4096;

struct Input {
  opt::OptionType type;
  double price, forward, strike, time, discount;
};

// z = ln(K/F) / (sigma sqrt T) drawn from [z_lo, z_hi] with a random sign;
// the option is the out-of-the-money one half the time and the
// in-the-money one (solved through parity) the other half.
std::vector<Input> make_inputs(double z_lo, double z_hi, std::uint64_t seed) {
  opt::Xoshiro256pp rng{seed};
  std::vector<Input> in;
  in.reserve(kCount);
  while (in.size() < kCount) {
    const double vol = opt::uniform(rng, 0.1, 1.0);
    const double time = opt::uniform(rng, 0.05, 2.0);
    const double sign = (rng() & 1U) != 0 ? 1.0 : -1.0;
    const double z = sign * opt::uniform(rng, z_lo, z_hi);
    const double forward = 100.0;
    const double strike = forward * std::exp(z * vol * std::sqrt(time));
    const double discount = std::exp(-0.03 * time);
    const auto type = (rng() & 1U) != 0 ? opt::OptionType::Call : opt::OptionType::Put;
    const double price = opt::black76(type, forward, strike, time, discount, vol).price;
    if (opt::implied_vol_black(type, price, forward, strike, time, discount).status ==
        opt::IvStatus::Ok) {
      in.push_back(Input{type, price, forward, strike, time, discount});
    }
  }
  return in;
}

void run(benchmark::State& state, const std::vector<Input>& in) {
  long long iterations = 0;
  long long solves = 0;
  for (auto _ : state) {
    for (const Input& x : in) {
      auto r =
          opt::implied_vol_black(x.type, x.price, x.forward, x.strike, x.time, x.discount);
      benchmark::DoNotOptimize(r.vol);
      iterations += r.iterations;
    }
    solves += static_cast<long long>(in.size());
  }
  state.SetItemsProcessed(solves);
  state.counters["time/solve"] = benchmark::Counter(
      static_cast<double>(in.size()),
      benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  state.counters["mean_iter"] =
      solves == 0 ? 0.0 : static_cast<double>(iterations) / static_cast<double>(solves);
}

void BM_ImpliedVolNearMoney(benchmark::State& state) {
  static const auto in = make_inputs(0.0, 1.0, 11);
  run(state, in);
}
BENCHMARK(BM_ImpliedVolNearMoney);

void BM_ImpliedVolWings(benchmark::State& state) {
  static const auto in = make_inputs(3.0, 6.0, 12);
  run(state, in);
}
BENCHMARK(BM_ImpliedVolWings);

}  // namespace
