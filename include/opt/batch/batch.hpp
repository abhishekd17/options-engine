#pragma once
// Batch Black-Scholes-Merton pricing over structure-of-arrays inputs.
//
// Engines:
//   Reference  - a loop over opt::bsm_price / opt::bsm_greeks (libm exp, log,
//                erfc). The oracle for the others.
//   FastScalar - the shared kernel (kernel.hpp) instantiated with double:
//                fast_exp, fast_log, fast_norm_cdf, explicit fma. Compiler
//                auto-vectorization of this path is switched off (clang,
//                GCC >= 14) so it stays a one-option-at-a-time baseline.
//   Simd       - the same kernel instantiated with opt::simd::Native over full
//                vectors; the n % W tail runs the double kernel. Every
//                operation is a correctly rounded IEEE operation done lane by
//                lane, so Simd is expected to equal FastScalar bit for bit.
//   ThreadedPricer - Simd split into 4,096-option chunks over a persistent
//                thread pool (the caller participates).
//
// Domain (validate_batch): S, K in [1e-8, 1e12], T in [1e-8, 100],
// sigma in [1e-6, 10], r and q in [-1, 1], all finite, type Call or Put.
// T == 0 and sigma == 0 are handled by the scalar reference only
// (opt::bsm_greeks). Outputs must not alias inputs. Span size mismatches throw
// std::invalid_argument before any work; nothing else is checked per call
// (debug builds assert the domain). No engine allocates.
#include "opt/batch/thread_pool.hpp"
#include "opt/pricing/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace opt {

struct OptionBatch {  // structure of arrays, all the same length n
  std::span<const OptionType> type;
  std::span<const double> spot, strike, time, rate, carry_yield, vol;
  [[nodiscard]] std::size_t size() const noexcept { return spot.size(); }
};

struct GreeksBatch {  // same units as opt::Greeks (vega per 1.00 vol, theta per year)
  std::span<double> price, delta, gamma, vega, theta, rho;
};

enum class Engine : std::uint8_t { Reference, FastScalar, Simd };

// Index of the first option outside the batch domain, or n if all are valid.
// Throws std::invalid_argument if the input spans differ in length.
[[nodiscard]] std::size_t validate_batch(const OptionBatch& in);

void price_batch(Engine engine, const OptionBatch& in, std::span<double> price);
void greeks_batch(Engine engine, const OptionBatch& in, const GreeksBatch& out);

// Size checks shared by the engines (throw std::invalid_argument).
void check_batch_sizes(const OptionBatch& in, std::span<const double> price);
void check_batch_sizes(const OptionBatch& in, const GreeksBatch& out);

class ThreadedPricer {
 public:
  static constexpr std::size_t kChunk = 4096;  // options per chunk

  // threads = total participating threads including the caller; the pool
  // spawns threads - 1 workers. 0 = hardware_concurrency() (or 1).
  explicit ThreadedPricer(unsigned threads = 0);

  void price(const OptionBatch& in, std::span<double> price);
  void greeks(const OptionBatch& in, const GreeksBatch& out);

  [[nodiscard]] unsigned threads() const noexcept { return pool_.threads(); }

 private:
  ThreadPool pool_;
};

}  // namespace opt
