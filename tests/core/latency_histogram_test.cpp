#include "opt/core/latency_histogram.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace {

using opt::LatencyHistogram;
using opt::LogLinearHistogram;

// Nearest-rank percentile of a sorted sample.
std::uint64_t exact_percentile(const std::vector<std::uint64_t>& sorted, double p) {
  const auto n = static_cast<double>(sorted.size());
  auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * n - 1e-9));
  rank = std::clamp<std::size_t>(rank, 1, sorted.size());
  return sorted[rank - 1];
}

TEST(LatencyHistogram, BucketsCoverEveryValueContiguously) {
  using H = LogLinearHistogram<3>;  // small, so the whole range is checked
  EXPECT_EQ(H::kBuckets, (65U - 3U) * 8U);
  EXPECT_EQ(H::bucket_low(0), 0U);
  for (std::size_t i = 0; i + 1 < H::kBuckets; ++i) {
    ASSERT_LE(H::bucket_low(i), H::bucket_high(i)) << i;
    ASSERT_EQ(H::bucket_high(i) + 1, H::bucket_low(i + 1)) << i;
    ASSERT_EQ(H::bucket_index(H::bucket_low(i)), i);
    ASSERT_EQ(H::bucket_index(H::bucket_high(i)), i);
  }
  EXPECT_EQ(H::bucket_high(H::kBuckets - 1), std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(H::bucket_index(std::numeric_limits<std::uint64_t>::max()), H::kBuckets - 1);
}

TEST(LatencyHistogram, SmallValuesAreExact) {
  // Values below 2^(S+1) = 256 have a bucket each.
  for (std::uint64_t v = 0; v < 256; ++v) {
    ASSERT_EQ(LatencyHistogram::bucket_low(LatencyHistogram::bucket_index(v)), v);
    ASSERT_EQ(LatencyHistogram::bucket_high(LatencyHistogram::bucket_index(v)), v);
  }
  EXPECT_EQ(LatencyHistogram::bucket_index(256), 256U);
  EXPECT_EQ(LatencyHistogram::bucket_high(256), 257U);  // first 2-wide bucket
}

TEST(LatencyHistogram, BucketWidthIsBoundedRelativeToValue) {
  std::mt19937_64 rng{7};
  for (int i = 0; i < 100'000; ++i) {
    const std::uint64_t v = rng() >> (rng() % 64);
    const std::size_t b = LatencyHistogram::bucket_index(v);
    ASSERT_LE(LatencyHistogram::bucket_low(b), v);
    ASSERT_GE(LatencyHistogram::bucket_high(b), v);
    const double width = static_cast<double>(LatencyHistogram::bucket_high(b) -
                                             LatencyHistogram::bucket_low(b));
    ASSERT_LE(width, static_cast<double>(v) / 128.0) << v;
  }
}

TEST(LatencyHistogram, EmptyReportsZeros) {
  const auto h = std::make_unique<LatencyHistogram>();
  EXPECT_EQ(h->count(), 0U);
  EXPECT_EQ(h->min(), 0U);
  EXPECT_EQ(h->max(), 0U);
  EXPECT_EQ(h->mean(), 0.0);
  EXPECT_EQ(h->value_at_percentile(50), 0U);
  EXPECT_EQ(h->value_at_percentile(100), 0U);
}

TEST(LatencyHistogram, OneValue) {
  const auto h = std::make_unique<LatencyHistogram>();
  h->record(123'456);
  EXPECT_EQ(h->count(), 1U);
  EXPECT_EQ(h->min(), 123'456U);
  EXPECT_EQ(h->max(), 123'456U);
  EXPECT_EQ(h->mean(), 123'456.0);
  // Clamped to [min, max], so a single value is reported exactly.
  for (const double p : {0.0, 50.0, 99.9, 100.0}) {
    EXPECT_EQ(h->value_at_percentile(p), 123'456U) << p;
  }
}

TEST(LatencyHistogram, NearestRankWithoutRoundingSurprises) {
  // Ranks that a floating-point ceil(p * n) gets wrong: 0.9 * 10 is
  // 9.000000000000002 and 0.999 * 1000 is 998.9999999999999. All values are
  // below 256, so each has its own bucket and the answers are exact.
  const auto h = std::make_unique<LatencyHistogram>();
  for (std::uint64_t v = 1; v <= 10; ++v) {
    h->record(v);
  }
  EXPECT_EQ(h->value_at_percentile(90), 9U);
  EXPECT_EQ(h->value_at_percentile(50), 5U);
  EXPECT_EQ(h->value_at_percentile(10), 1U);
  EXPECT_EQ(h->value_at_percentile(0), 1U);
  EXPECT_EQ(h->value_at_percentile(100), 10U);
  EXPECT_EQ(h->value_at_percentile(90.0001), 10U);
  h->reset();
  for (std::uint64_t v = 1; v <= 1000; ++v) {
    h->record(v / 8);  // sorted, the k-th sample is k / 8
  }
  EXPECT_EQ(h->value_at_percentile(99.9), 999U / 8);  // rank 999
  EXPECT_EQ(h->value_at_percentile(50), 500U / 8);    // rank 500
}

TEST(LatencyHistogram, PercentilesNeverUnderReportAndStayWithinPrecision) {
  std::mt19937_64 rng{11};
  // Log-normal-ish latencies from ~1 us to ~10 ms, like a real tail.
  std::lognormal_distribution<double> dist{9.0, 1.2};
  const auto h = std::make_unique<LatencyHistogram>();
  std::vector<std::uint64_t> values;
  for (int i = 0; i < 200'000; ++i) {
    const auto v = static_cast<std::uint64_t>(dist(rng));
    values.push_back(v);
    h->record(v);
  }
  std::sort(values.begin(), values.end());
  for (const double p : {1.0, 50.0, 90.0, 99.0, 99.9, 99.99}) {
    const std::uint64_t exact = exact_percentile(values, p);
    const std::uint64_t reported = h->value_at_percentile(p);
    EXPECT_GE(reported, exact) << "p" << p;
    EXPECT_LE(static_cast<double>(reported), static_cast<double>(exact) * (1.0 + 1.0 / 128.0))
        << "p" << p;
  }
  EXPECT_EQ(h->min(), values.front());
  EXPECT_EQ(h->max(), values.back());
  EXPECT_EQ(h->value_at_percentile(100), values.back());
  double sum = 0;
  for (const std::uint64_t v : values) {
    sum += static_cast<double>(v);
  }
  EXPECT_NEAR(h->mean(), sum / static_cast<double>(values.size()), 1e-6 * h->mean());
}

TEST(LatencyHistogram, RecordNEqualsRepeatedRecord) {
  const auto a = std::make_unique<LatencyHistogram>();
  const auto b = std::make_unique<LatencyHistogram>();
  a->record_n(5'000, 3);
  a->record_n(7, 0);  // no-op: does not touch min
  for (int i = 0; i < 3; ++i) {
    b->record(5'000);
  }
  EXPECT_EQ(a->count(), b->count());
  EXPECT_EQ(a->min(), 5'000U);
  EXPECT_EQ(a->value_at_percentile(50), b->value_at_percentile(50));
  EXPECT_EQ(a->mean(), b->mean());
}

TEST(LatencyHistogram, MergeEqualsRecordingEverything) {
  const auto a = std::make_unique<LatencyHistogram>();
  const auto b = std::make_unique<LatencyHistogram>();
  const auto all = std::make_unique<LatencyHistogram>();
  std::mt19937_64 rng{3};
  for (int i = 0; i < 10'000; ++i) {
    const std::uint64_t v = rng() % 1'000'000;
    (i % 3 == 0 ? *a : *b).record(v);
    all->record(v);
  }
  a->merge(*b);
  EXPECT_EQ(a->count(), all->count());
  EXPECT_EQ(a->min(), all->min());
  EXPECT_EQ(a->max(), all->max());
  EXPECT_EQ(a->mean(), all->mean());
  for (std::size_t i = 0; i < LatencyHistogram::kBuckets; ++i) {
    ASSERT_EQ(a->bucket_count(i), all->bucket_count(i));
  }
  // Merging an empty histogram changes nothing (its min is the sentinel).
  const auto empty = std::make_unique<LatencyHistogram>();
  a->merge(*empty);
  EXPECT_EQ(a->min(), all->min());
  EXPECT_EQ(a->count(), all->count());
}

TEST(LatencyHistogram, ExtremeValues) {
  const auto h = std::make_unique<LatencyHistogram>();
  h->record(0);
  h->record(std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(h->min(), 0U);
  EXPECT_EQ(h->max(), std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(h->value_at_percentile(50), 0U);
  EXPECT_EQ(h->value_at_percentile(100), std::numeric_limits<std::uint64_t>::max());
}

TEST(LatencyHistogram, ForEachBucketVisitsNonEmptyBucketsInOrder) {
  const auto h = std::make_unique<LatencyHistogram>();
  h->record(1'000'000);
  h->record(3);
  h->record(3);
  std::vector<std::uint64_t> lows;
  std::uint64_t total = 0;
  h->for_each_bucket([&](std::uint64_t low, std::uint64_t high, std::uint64_t count) {
    EXPECT_LE(low, high);
    lows.push_back(low);
    total += count;
  });
  ASSERT_EQ(lows.size(), 2U);
  EXPECT_EQ(lows[0], 3U);
  EXPECT_LE(lows[1], 1'000'000U);
  EXPECT_EQ(total, 3U);
}

}  // namespace
