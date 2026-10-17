#include "opt/batch/batch.hpp"

#include "opt/batch/kernel.hpp"
#include "opt/batch/thread_pool.hpp"
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/types.hpp"
#include "opt/simd/simd.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>

namespace opt {

namespace {

void require_same_size(std::size_t n, std::size_t got, const char* name) {
  if (got != n) {
    throw std::invalid_argument(std::string("batch span '") + name + "' has size " +
                                std::to_string(got) + ", expected " + std::to_string(n) +
                                " (size of 'spot')");
  }
}

void check_inputs(const OptionBatch& in) {
  const std::size_t n = in.spot.size();
  require_same_size(n, in.type.size(), "type");
  require_same_size(n, in.strike.size(), "strike");
  require_same_size(n, in.time.size(), "time");
  require_same_size(n, in.rate.size(), "rate");
  require_same_size(n, in.carry_yield.size(), "carry_yield");
  require_same_size(n, in.vol.size(), "vol");
}

bool in_range(double x, double lo, double hi) noexcept {
  return x >= lo && x <= hi;  // false for NaN and +-inf outside [lo, hi]
}

OptionSpec spec_at(const OptionBatch& in, std::size_t i) noexcept {
  return {in.type[i],    in.spot[i],        in.strike[i], in.time[i],
          in.rate[i],    in.carry_yield[i], in.vol[i]};
}

}  // namespace

void check_batch_sizes(const OptionBatch& in, std::span<const double> price) {
  check_inputs(in);
  require_same_size(in.size(), price.size(), "price");
}

void check_batch_sizes(const OptionBatch& in, const GreeksBatch& out) {
  check_inputs(in);
  const std::size_t n = in.size();
  require_same_size(n, out.price.size(), "price");
  require_same_size(n, out.delta.size(), "delta");
  require_same_size(n, out.gamma.size(), "gamma");
  require_same_size(n, out.vega.size(), "vega");
  require_same_size(n, out.theta.size(), "theta");
  require_same_size(n, out.rho.size(), "rho");
}

std::size_t validate_batch(const OptionBatch& in) {
  check_inputs(in);
  const std::size_t n = in.size();
  for (std::size_t i = 0; i < n; ++i) {
    const bool ok = (in.type[i] == OptionType::Call || in.type[i] == OptionType::Put) &&
                    in_range(in.spot[i], 1e-8, 1e12) && in_range(in.strike[i], 1e-8, 1e12) &&
                    in_range(in.time[i], 1e-8, 100.0) && in_range(in.vol[i], 1e-6, 10.0) &&
                    in_range(in.rate[i], -1.0, 1.0) && in_range(in.carry_yield[i], -1.0, 1.0);
    if (!ok) {
      return i;
    }
  }
  return n;
}

void price_batch(Engine engine, const OptionBatch& in, std::span<double> price) {
  check_batch_sizes(in, price);
  assert(validate_batch(in) == in.size());
  const std::size_t n = in.size();
  switch (engine) {
    case Engine::Reference:
      for (std::size_t i = 0; i < n; ++i) {
        price[i] = bsm_price(spec_at(in, i));
      }
      return;
    case Engine::FastScalar:
      kernel::price_range<double>(in, price.data(), 0, n);
      return;
    case Engine::Simd:
      kernel::price_range<simd::Native>(in, price.data(), 0, n);
      return;
  }
}

void greeks_batch(Engine engine, const OptionBatch& in, const GreeksBatch& out) {
  check_batch_sizes(in, out);
  assert(validate_batch(in) == in.size());
  const std::size_t n = in.size();
  switch (engine) {
    case Engine::Reference:
      for (std::size_t i = 0; i < n; ++i) {
        const Greeks g = bsm_greeks(spec_at(in, i));
        out.price[i] = g.price;
        out.delta[i] = g.delta;
        out.gamma[i] = g.gamma;
        out.vega[i] = g.vega;
        out.theta[i] = g.theta;
        out.rho[i] = g.rho;
      }
      return;
    case Engine::FastScalar:
      kernel::greeks_range<double>(in, out, 0, n);
      return;
    case Engine::Simd:
      kernel::greeks_range<simd::Native>(in, out, 0, n);
      return;
  }
}

// ---------------------------------------------------------------------------
// ThreadedPricer
// ---------------------------------------------------------------------------

namespace {

struct PriceJob {
  const OptionBatch* in;
  double* out;
  std::size_t n;
};

struct GreeksJob {
  const OptionBatch* in;
  const GreeksBatch* out;
  std::size_t n;
};

constexpr std::size_t chunk_count(std::size_t n) noexcept {
  return (n + ThreadedPricer::kChunk - 1) / ThreadedPricer::kChunk;
}

void price_chunk(void* ctx, std::size_t chunk) noexcept {
  const auto* job = static_cast<const PriceJob*>(ctx);
  const std::size_t begin = chunk * ThreadedPricer::kChunk;
  const std::size_t end = std::min(job->n, begin + ThreadedPricer::kChunk);
  kernel::price_range<simd::Native>(*job->in, job->out, begin, end);
}

void greeks_chunk(void* ctx, std::size_t chunk) noexcept {
  const auto* job = static_cast<const GreeksJob*>(ctx);
  const std::size_t begin = chunk * ThreadedPricer::kChunk;
  const std::size_t end = std::min(job->n, begin + ThreadedPricer::kChunk);
  kernel::greeks_range<simd::Native>(*job->in, *job->out, begin, end);
}

// Chunk boundaries are multiples of the vector width, so the threaded split
// runs exactly the same vector/tail pattern as the single-threaded Simd engine.
static_assert(ThreadedPricer::kChunk % simd::kNativeWidth == 0);
static_assert(ThreadedPricer::kChunk % 8 == 0);  // 64-byte chunk boundaries

}  // namespace

ThreadedPricer::ThreadedPricer(unsigned threads) : pool_(threads) {}

void ThreadedPricer::price(const OptionBatch& in, std::span<double> price) {
  check_batch_sizes(in, price);
  assert(validate_batch(in) == in.size());
  PriceJob job{&in, price.data(), in.size()};
  pool_.run(chunk_count(job.n), &price_chunk, &job);
}

void ThreadedPricer::greeks(const OptionBatch& in, const GreeksBatch& out) {
  check_batch_sizes(in, out);
  assert(validate_batch(in) == in.size());
  GreeksJob job{&in, &out, in.size()};
  pool_.run(chunk_count(job.n), &greeks_chunk, &job);
}

}  // namespace opt
