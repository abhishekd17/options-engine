#include "opt/batch/thread_pool.hpp"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <stop_token>
#include <thread>

namespace opt {

unsigned resolve_thread_count(unsigned threads) noexcept {
  if (threads != 0) {
    return threads;
  }
  const unsigned hw = std::thread::hardware_concurrency();
  return hw == 0 ? 1U : hw;
}

ThreadPool::ThreadPool(unsigned threads) : threads_(resolve_thread_count(threads)) {
  workers_.reserve(threads_ - 1);
  for (unsigned i = 1; i < threads_; ++i) {
    workers_.emplace_back([this](const std::stop_token& stop) { worker_loop(stop); });
  }
}

ThreadPool::~ThreadPool() {
  // Request stop on every worker first so they all wake together, then join
  // (the jthread destructors join).
  for (auto& w : workers_) {
    w.request_stop();
  }
  workers_.clear();
}

void ThreadPool::claim_chunks(ChunkFn fn, void* ctx, std::size_t chunks) noexcept {
  for (;;) {
    const std::size_t i = next_chunk_.fetch_add(1, std::memory_order_relaxed);
    if (i >= chunks) {
      return;
    }
    fn(ctx, i);
  }
}

void ThreadPool::worker_loop(const std::stop_token& stop) {
  std::uint64_t seen = 0;
  for (;;) {
    ChunkFn fn = nullptr;
    void* ctx = nullptr;
    std::size_t chunks = 0;
    {
      std::unique_lock lock(mutex_);
      if (!job_cv_.wait(lock, stop, [&] { return generation_ != seen; })) {
        return;  // stop requested
      }
      seen = generation_;
      if (fn_ == nullptr) {
        continue;  // woke after the job was closed
      }
      fn = fn_;
      ctx = ctx_;
      chunks = chunks_;
      ++active_;
    }
    claim_chunks(fn, ctx, chunks);
    bool last = false;
    {
      const std::lock_guard lock(mutex_);
      last = --active_ == 0;
    }
    if (last) {
      done_cv_.notify_one();
    }
  }
}

void ThreadPool::run(std::size_t chunks, ChunkFn fn, void* ctx) {
  if (chunks == 0) {
    return;
  }
  const std::lock_guard run_lock(run_mutex_);
  if (workers_.empty()) {
    for (std::size_t i = 0; i < chunks; ++i) {
      fn(ctx, i);
    }
    return;
  }
  {
    const std::lock_guard lock(mutex_);
    fn_ = fn;
    ctx_ = ctx;
    chunks_ = chunks;
    next_chunk_.store(0, std::memory_order_relaxed);
    ++generation_;
  }
  job_cv_.notify_all();
  claim_chunks(fn, ctx, chunks);
  std::unique_lock lock(mutex_);
  done_cv_.wait(lock, [&] { return active_ == 0; });
  fn_ = nullptr;  // close the job
  ctx_ = nullptr;
  chunks_ = 0;
}

}  // namespace opt
