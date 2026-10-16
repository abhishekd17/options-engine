#pragma once
// Origin: copied from itch-pipeline/include/itch/latency_histogram.hpp
// (same author), namespace renamed from itch to opt.
//
// Log-linear latency histogram in the style of HdrHistogram.
//
// Values are unsigned 64-bit integers (nanoseconds, in this project). Each
// power-of-two range [2^k, 2^(k+1)) is split into 2^SubBucketBits equal
// sub-buckets, and values below 2^(SubBucketBits+1) get one bucket each. So
// every value from 0 to UINT64_MAX has a bucket, and a bucket's width is at
// most 1/2^SubBucketBits of its lower bound: with the default 7 bits, a
// reported value is within 0.79% of the recorded one (3 significant decimal
// digits would need 10 bits, at 8 times the memory).
//
// Index. With w = bit_width(v) and shift = max(w - (S+1), 0),
//   index = shift * 2^S + (v >> shift).
// For shift 0 that is v itself (exact buckets 0 .. 2^(S+1)-1). For shift
// >= 1, v >> shift lies in [2^S, 2^(S+1)), so the indexes of consecutive
// shifts are contiguous. The bucket count is (65 - S) * 2^S: 7,424 counters
// (58 KiB) for S = 7. They are an in-object std::array, so record() is a
// bit_width, a shift, an add and an increment, with no allocation and no
// branch on the value's range. Trade-off: the object is large; put it on
// the heap or in static storage, not in a small stack frame.
//
// Percentiles use the nearest-rank definition: the value at percentile p is
// the smallest recorded value v such that at least p% of the samples are
// <= v. It is reported as the highest value of v's bucket (the HdrHistogram
// convention, so a percentile never under-reports), clamped to [min, max].
// p is resolved to 1e-4 percent and the rank is computed in integers, so
// p99.9 of 1,000 samples is exactly the 999th, with no floating-point
// rounding to push it to the 1,000th.
//
// Not thread-safe: one thread records. To combine threads, give each its
// own histogram and merge() them after joining.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace opt {

template <unsigned SubBucketBits = 7>
  requires(SubBucketBits >= 1 && SubBucketBits <= 16)
class LogLinearHistogram {
 public:
  static constexpr unsigned kSubBucketBits = SubBucketBits;
  static constexpr std::size_t kSubBuckets = std::size_t{1} << SubBucketBits;
  static constexpr std::size_t kBuckets = (65U - SubBucketBits) * kSubBuckets;

  // The bucket a value is counted in.
  [[nodiscard]] static constexpr std::size_t bucket_index(std::uint64_t value) noexcept {
    const auto width = static_cast<unsigned>(std::bit_width(value));
    const unsigned shift = width > SubBucketBits + 1 ? width - (SubBucketBits + 1) : 0U;
    return (std::size_t{shift} << SubBucketBits) + static_cast<std::size_t>(value >> shift);
  }
  // The smallest and largest values counted in bucket `index`.
  [[nodiscard]] static constexpr std::uint64_t bucket_low(std::size_t index) noexcept {
    if (index < 2 * kSubBuckets) {
      return index;
    }
    const std::size_t shift = (index >> SubBucketBits) - 1;
    return static_cast<std::uint64_t>(index - (shift << SubBucketBits)) << shift;
  }
  [[nodiscard]] static constexpr std::uint64_t bucket_high(std::size_t index) noexcept {
    if (index < 2 * kSubBuckets) {
      return index;
    }
    const std::size_t shift = (index >> SubBucketBits) - 1;
    return bucket_low(index) + ((std::uint64_t{1} << shift) - 1);
  }

  void record(std::uint64_t value) noexcept { record_n(value, 1); }
  // Records `value` `count` times.
  void record_n(std::uint64_t value, std::uint64_t count) noexcept {
    if (count == 0) {
      return;
    }
    counts_[bucket_index(value)] += count;
    count_ += count;
    sum_ += value * count;  // wraps past 2^64; see mean()
    min_ = std::min(min_, value);
    max_ = std::max(max_, value);
  }

  // Adds every sample of `other`.
  void merge(const LogLinearHistogram& other) noexcept {
    for (std::size_t i = 0; i < kBuckets; ++i) {
      counts_[i] += other.counts_[i];
    }
    count_ += other.count_;
    sum_ += other.sum_;
    min_ = std::min(min_, other.min_);
    max_ = std::max(max_, other.max_);
  }

  void reset() noexcept {
    counts_.fill(0);
    count_ = 0;
    sum_ = 0;
    min_ = std::numeric_limits<std::uint64_t>::max();
    max_ = 0;
  }

  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
  // Exact smallest and largest recorded values; 0 when empty.
  [[nodiscard]] std::uint64_t min() const noexcept { return count_ == 0 ? 0 : min_; }
  [[nodiscard]] std::uint64_t max() const noexcept { return max_; }
  // Exact mean, as long as the sum of all samples stays below 2^64 (585
  // years of nanoseconds); 0 when empty.
  [[nodiscard]] double mean() const noexcept {
    return count_ == 0 ? 0.0 : static_cast<double>(sum_) / static_cast<double>(count_);
  }
  // Samples in bucket `index`.
  [[nodiscard]] std::uint64_t bucket_count(std::size_t index) const noexcept {
    return counts_[index];
  }

  // The value at percentile p (0..100, clamped), as described above; 0 when
  // empty. p 0 gives min(), p 100 gives max().
  [[nodiscard]] std::uint64_t value_at_percentile(double percentile) const noexcept {
    if (count_ == 0) {
      return 0;
    }
    constexpr std::uint64_t kScale = 1'000'000;  // percent resolved to 1e-4
    const double clamped = std::clamp(percentile, 0.0, 100.0);
    const auto ppm = static_cast<std::uint64_t>(std::llround(clamped * (kScale / 100)));
    // rank = ceil(count * ppm / kScale), without overflowing 64 bits.
    const std::uint64_t whole = (count_ / kScale) * ppm;
    const std::uint64_t part = ((count_ % kScale) * ppm + kScale - 1) / kScale;
    const std::uint64_t rank = std::max<std::uint64_t>(whole + part, 1);
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < kBuckets; ++i) {
      seen += counts_[i];
      if (seen >= rank) {
        return std::clamp(bucket_high(i), min_, max_);
      }
    }
    return max_;  // unreachable: the counts sum to count_
  }

  // Calls f(low, high, count) for every non-empty bucket, lowest first.
  template <class F>
  void for_each_bucket(F&& f) const {  // NOLINT(cppcoreguidelines-missing-std-forward): called per bucket
    for (std::size_t i = 0; i < kBuckets; ++i) {
      if (counts_[i] != 0) {
        f(bucket_low(i), bucket_high(i), counts_[i]);
      }
    }
  }

 private:
  std::array<std::uint64_t, kBuckets> counts_{};
  std::uint64_t count_ = 0;
  std::uint64_t sum_ = 0;
  std::uint64_t min_ = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t max_ = 0;
};

// Nanosecond latencies, within 0.79%.
using LatencyHistogram = LogLinearHistogram<7>;

}  // namespace opt
