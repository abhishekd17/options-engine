#pragma once
// Persistent thread pool for chunked, allocation-free parallel loops.
//
// threads = total participating threads INCLUDING the caller of run(), so the
// pool owns threads - 1 std::jthread workers (threads == 1: no workers). The
// workers are created once; run() only wakes them.
//
// run(chunks, fn, ctx) calls fn(ctx, i) exactly once for every i in
// [0, chunks), spread over the caller and the workers, and returns when all
// calls are done. Chunk indices are claimed with fetch_add on one atomic
// counter (dynamic claiming: fast cores take more chunks, which matters on a
// CPU that mixes performance and efficiency cores). A function pointer plus
// context is used instead of std::function so a call allocates nothing.
//
// Protocol (all under mutex_ except the chunk claiming):
//   publish: run() stores fn/ctx/chunks, resets the claim counter, bumps
//     generation_ and wakes the workers;
//   join: a worker that sees a new generation with an open job copies it and
//     increments active_;
//   leave: when no chunk is left to claim it decrements active_ and notifies
//     done_cv_ if it was the last one;
//   close: run() claims chunks itself, then waits until active_ == 0 and
//     closes the job (fn_ = nullptr) before returning. A worker that wakes
//     after the close sees no open job and goes back to sleep, so it can never
//     run a stale fn on the next job's chunks.
// One job at a time: concurrent run() calls are serialised by run_mutex_.
// Shutdown: the jthreads' stop tokens wake the condition_variable_any wait.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace opt {

class ThreadPool {
 public:
  // Chunk functions cannot throw (the pricing kernels are noexcept).
  using ChunkFn = void (*)(void* ctx, std::size_t chunk) noexcept;

  // threads >= 1 total participants; 0 means hardware_concurrency() (or 1
  // if that is unknown).
  explicit ThreadPool(unsigned threads = 0);
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;
  ~ThreadPool();

  void run(std::size_t chunks, ChunkFn fn, void* ctx);

  [[nodiscard]] unsigned threads() const noexcept { return threads_; }

 private:
  void worker_loop(const std::stop_token& stop);
  void claim_chunks(ChunkFn fn, void* ctx, std::size_t chunks) noexcept;

  unsigned threads_;
  std::mutex run_mutex_;  // serialises run()
  std::mutex mutex_;      // guards everything below except next_chunk_
  std::condition_variable_any job_cv_;
  std::condition_variable done_cv_;
  std::uint64_t generation_ = 0;
  ChunkFn fn_ = nullptr;  // nullptr: no open job
  void* ctx_ = nullptr;
  std::size_t chunks_ = 0;
  unsigned active_ = 0;  // workers inside the open job
  std::atomic<std::size_t> next_chunk_{0};
  // Last member: destroyed first, so workers stop before the state above dies.
  std::vector<std::jthread> workers_;
};

[[nodiscard]] unsigned resolve_thread_count(unsigned threads) noexcept;

}  // namespace opt
