// Heap allocation counting for the batch engines (design 6.5).
//
// This file replaces the global operator new and delete for the whole test
// executable (it is a separate executable for that reason; approach copied
// from itch-pipeline/tests/alloc_test.cpp). Every form of new increments a
// counter while counting is on, then defers to malloc; every delete calls
// free. The tests assert that repeated batch calls of every engine, including
// ThreadedPricer, allocate nothing once inputs, outputs and the pool exist.
//
// A control test shows the counter is live: a plain `new` is counted, and so
// is constructing a ThreadedPricer (its worker vector and threads).
#include "opt/batch/batch.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include <gtest/gtest.h>

#include "batch/batch_test_util.hpp"

namespace {

std::atomic<bool> g_counting{false};
std::atomic<std::uint64_t> g_allocations{0};

void* counted_alloc(std::size_t size, std::size_t alignment) noexcept {
  if (g_counting.load(std::memory_order_relaxed)) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
  }
  if (size == 0) {
    size = 1;
  }
  if (alignment <= alignof(std::max_align_t)) {
    return std::malloc(size);  // NOLINT(cppcoreguidelines-no-malloc): this is operator new
  }
  void* p = nullptr;
  return ::posix_memalign(&p, alignment, size) == 0 ? p : nullptr;
}

void* counted_alloc_or_throw(std::size_t size, std::size_t alignment) {
  void* p = counted_alloc(size, alignment);
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

// Counts allocations (on every thread) for its lifetime. Not reentrant.
class CountAllocations {
 public:
  CountAllocations() {
    g_allocations.store(0);
    g_counting.store(true);
  }
  CountAllocations(const CountAllocations&) = delete;
  CountAllocations& operator=(const CountAllocations&) = delete;
  ~CountAllocations() { g_counting.store(false); }
  std::uint64_t stop() {
    g_counting.store(false);
    return g_allocations.load();
  }
};

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-no-malloc): replacement allocation functions
void* operator new(std::size_t size) {
  return counted_alloc_or_throw(size, 0);
}
void* operator new[](std::size_t size) {
  return counted_alloc_or_throw(size, 0);
}
void* operator new(std::size_t size, std::align_val_t al) {
  return counted_alloc_or_throw(size, static_cast<std::size_t>(al));
}
void* operator new[](std::size_t size, std::align_val_t al) {
  return counted_alloc_or_throw(size, static_cast<std::size_t>(al));
}
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
  return counted_alloc(size, 0);
}
void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
  return counted_alloc(size, 0);
}
void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t& /*tag*/) noexcept {
  return counted_alloc(size, static_cast<std::size_t>(al));
}
void* operator new[](std::size_t size, std::align_val_t al,
                     const std::nothrow_t& /*tag*/) noexcept {
  return counted_alloc(size, static_cast<std::size_t>(al));
}
void operator delete(void* p) noexcept {
  std::free(p);
}
void operator delete[](void* p) noexcept {
  std::free(p);
}
void operator delete(void* p, std::size_t /*size*/) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::size_t /*size*/) noexcept {
  std::free(p);
}
void operator delete(void* p, std::align_val_t /*al*/) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::align_val_t /*al*/) noexcept {
  std::free(p);
}
void operator delete(void* p, std::size_t /*size*/, std::align_val_t /*al*/) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::size_t /*size*/, std::align_val_t /*al*/) noexcept {
  std::free(p);
}
void operator delete(void* p, const std::nothrow_t& /*tag*/) noexcept {
  std::free(p);
}
void operator delete[](void* p, const std::nothrow_t& /*tag*/) noexcept {
  std::free(p);
}
void operator delete(void* p, std::align_val_t /*al*/, const std::nothrow_t& /*tag*/) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::align_val_t /*al*/, const std::nothrow_t& /*tag*/) noexcept {
  std::free(p);
}
// NOLINTEND(cppcoreguidelines-no-malloc)

namespace {

using opt::test::SoaGreeks;
using opt::test::SoaOptions;

TEST(Allocations, CounterIsLive) {
  CountAllocations count;
  auto p = std::make_unique<int>(7);
  {
    const opt::ThreadedPricer pricer(3);  // 2 worker threads + vector
  }
  EXPECT_GE(count.stop(), 2U);
  EXPECT_EQ(*p, 7);
}

TEST(Allocations, BatchEnginesAllocateNothing) {
  // Not a multiple of any SIMD width, several thread-pool chunks.
  const SoaOptions o = opt::test::random_options(3 * opt::ThreadedPricer::kChunk + 5, 11);
  SoaGreeks g(o.size());
  std::vector<double> price(o.size());
  const auto in = o.view();
  const auto out = g.view();
  opt::ThreadedPricer pricer(4);
  opt::ThreadedPricer all(0);
  CountAllocations count;
  for (int rep = 0; rep < 20; ++rep) {
    for (const auto e : {opt::Engine::Reference, opt::Engine::FastScalar, opt::Engine::Simd}) {
      opt::price_batch(e, in, price);
      opt::greeks_batch(e, in, out);
    }
    pricer.price(in, price);
    pricer.greeks(in, out);
    all.price(in, price);
    all.greeks(in, out);
  }
  EXPECT_EQ(count.stop(), 0U);
}

}  // namespace
