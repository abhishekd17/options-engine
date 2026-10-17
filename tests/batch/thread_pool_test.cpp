// ThreadPool protocol and ThreadedPricer results (design 6.4, 6.6).
// Run under the tsan preset too.
#include "opt/batch/batch.hpp"
#include "opt/batch/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "batch_test_util.hpp"

namespace {

using opt::test::SoaGreeks;
using opt::test::SoaOptions;

// Counts how often each chunk ran.
struct Counter {
  std::vector<std::atomic<int>> hits;
  explicit Counter(std::size_t n) : hits(n) {}
  static void fn(void* ctx, std::size_t chunk) noexcept {
    static_cast<Counter*>(ctx)->hits[chunk].fetch_add(1, std::memory_order_relaxed);
  }
};

void expect_each_chunk_once(opt::ThreadPool& pool, std::size_t chunks) {
  Counter c(chunks);
  pool.run(chunks, &Counter::fn, &c);
  for (std::size_t i = 0; i < chunks; ++i) {
    ASSERT_EQ(c.hits[i].load(), 1) << "chunk " << i << " of " << chunks << ", threads "
                                   << pool.threads();
  }
}

std::vector<unsigned> thread_counts() {
  const unsigned hw = opt::resolve_thread_count(0);
  return {1U, 2U, 3U, hw};
}

TEST(ThreadPool, ResolvesThreadCount) {
  EXPECT_EQ(opt::resolve_thread_count(5), 5U);
  EXPECT_GE(opt::resolve_thread_count(0), 1U);
  const opt::ThreadPool pool(0);
  EXPECT_EQ(pool.threads(), opt::resolve_thread_count(0));
}

TEST(ThreadPool, EveryChunkRunsExactlyOnce) {
  for (const unsigned t : thread_counts()) {
    opt::ThreadPool pool(t);
    EXPECT_EQ(pool.threads(), t);
    for (const std::size_t chunks :
         {std::size_t{0}, std::size_t{1}, std::size_t{t - 1}, std::size_t{t}, std::size_t{1000}}) {
      expect_each_chunk_once(pool, chunks);
    }
  }
}

TEST(ThreadPool, ManyJobsOnOnePool) {
  opt::ThreadPool pool(4);
  for (int job = 0; job < 1000; ++job) {
    expect_each_chunk_once(pool, static_cast<std::size_t>(job % 17));
  }
}

TEST(ThreadPool, DestructionWhileIdle) {
  for (int i = 0; i < 50; ++i) {
    opt::ThreadPool pool(4);  // workers may not even have started waiting yet
  }
  opt::ThreadPool pool(3);
  expect_each_chunk_once(pool, 10);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));  // workers asleep again
}

TEST(ThreadPool, ConcurrentRunCallsAreSerialised) {
  opt::ThreadPool pool(3);
  constexpr int kJobs = 200;
  std::atomic<int> done{0};
  auto caller = [&] {
    for (int j = 0; j < kJobs; ++j) {
      Counter c(37);
      pool.run(37, &Counter::fn, &c);
      bool ok = true;
      for (auto& h : c.hits) {
        ok = ok && h.load() == 1;
      }
      if (ok) {
        done.fetch_add(1);
      }
    }
  };
  std::thread a(caller);
  std::thread b(caller);
  a.join();
  b.join();
  EXPECT_EQ(done.load(), 2 * kJobs);
}

std::size_t bit_mismatches(std::span<const double> a, std::span<const double> b) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    n += std::bit_cast<std::uint64_t>(a[i]) != std::bit_cast<std::uint64_t>(b[i]) ? 1U : 0U;
  }
  return n;
}

TEST(ThreadedPricer, EqualsSimdBitwise) {
  // Several chunks plus a partial last chunk with a vector tail.
  const SoaOptions o = opt::test::random_options(5 * opt::ThreadedPricer::kChunk + 1003, 77);
  SoaGreeks simd(o.size());
  opt::greeks_batch(opt::Engine::Simd, o.view(), simd.view());
  std::vector<double> simd_price(o.size());
  opt::price_batch(opt::Engine::Simd, o.view(), simd_price);
  for (const unsigned t : thread_counts()) {
    opt::ThreadedPricer pricer(t);
    EXPECT_EQ(pricer.threads(), t);
    for (int rep = 0; rep < 3; ++rep) {
      SoaGreeks g(o.size());
      pricer.greeks(o.view(), g.view());
      std::vector<double> price(o.size(), -1.0);
      pricer.price(o.view(), price);
      EXPECT_EQ(bit_mismatches(g.price, simd.price), 0U) << t << " threads";
      EXPECT_EQ(bit_mismatches(g.delta, simd.delta), 0U) << t << " threads";
      EXPECT_EQ(bit_mismatches(g.gamma, simd.gamma), 0U) << t << " threads";
      EXPECT_EQ(bit_mismatches(g.vega, simd.vega), 0U) << t << " threads";
      EXPECT_EQ(bit_mismatches(g.theta, simd.theta), 0U) << t << " threads";
      EXPECT_EQ(bit_mismatches(g.rho, simd.rho), 0U) << t << " threads";
      EXPECT_EQ(bit_mismatches(price, simd_price), 0U) << t << " threads";
    }
    // Empty and odd-offset batches.
    std::vector<double> none;
    pricer.price(o.view(0, 0), none);
    std::vector<double> part(o.size(), -1.0);
    pricer.price(o.view(3, 9000), std::span(part).subspan(3, 9000));
    std::vector<double> expect(o.size(), -1.0);
    opt::price_batch(opt::Engine::Simd, o.view(3, 9000), std::span(expect).subspan(3, 9000));
    EXPECT_EQ(bit_mismatches(part, expect), 0U);
  }
}

}  // namespace
