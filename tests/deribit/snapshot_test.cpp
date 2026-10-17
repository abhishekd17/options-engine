#include "opt/deribit/snapshot.hpp"
#include "opt/vol/market_iv.hpp"
#include "snapshot_dir.hpp"
#include <gtest/gtest.h>
#include <unistd.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
namespace {
namespace fs = std::filesystem;
using opt::deribit::DataError;
using opt::deribit::load_snapshot;
// ---------------------------------------------------------------------------
// Committed snapshot
// ---------------------------------------------------------------------------
TEST(DeribitSnapshot, LoadsCommittedFullChain) {
  const auto snap = load_snapshot(opt::test::committed_snapshot_dir());
  EXPECT_EQ(snap.currency, "BTC");
  EXPECT_GT(snap.index_price, 0.0);
  EXPECT_EQ(snap.expiries.size(), 8U);  // trimmed to the 8 nearest expiries
  EXPECT_EQ(snap.quotes.size(), 524U);
  EXPECT_EQ(snap.skipped.count("unmatched"), 0U);
  EXPECT_LE(snap.valuation_spread_ms(), 1000);
  std::size_t total = 0;
  for (std::size_t i = 0; i < snap.expiries.size(); ++i) {
    const auto& e = snap.expiries[i];
    EXPECT_GT(e.forward, 0.0);
    EXPECT_GT(e.tau, 0.0);
    if (i > 0) {
      EXPECT_GT(e.expiry_ms, snap.expiries[i - 1].expiry_ms);
    }
    for (std::size_t j = 0; j < e.quote_index.size(); ++j) {
      const auto& q = snap.quotes[e.quote_index[j]];
      EXPECT_EQ(q.expiry_ms, e.expiry_ms);
      EXPECT_EQ(q.forward, e.forward);
      EXPECT_GT(q.tau, 0.0);
      EXPECT_GT(q.mark_iv, 0.0);
      EXPECT_LT(q.mark_iv, 10.0);  // decimal, not percent
      if (j > 0) {
        EXPECT_LE(snap.quotes[e.quote_index[j - 1]].strike, q.strike);
      }
    }
    total += e.quote_index.size();
  }
  EXPECT_EQ(total, snap.quotes.size());
}
TEST(DeribitSnapshot, MarkConventionMatchesDeribitMarkIv) {
  const auto snap = load_snapshot(opt::test::committed_snapshot_dir());
  const auto ivs = opt::compute_market_ivs(snap);
  ASSERT_EQ(ivs.size(), snap.quotes.size());
  const auto cmp = opt::compare_to_mark(snap, ivs);
  std::printf("  OTM |iv_mark - mark_iv|: n=%zu median %.4f p95 %.4f max %.4f vol pts\n",
              cmp.mark_abs_otm.count, cmp.mark_abs_otm.median * 100.0,
              cmp.mark_abs_otm.p95 * 100.0, cmp.mark_abs_otm.max_abs * 100.0);
  EXPECT_GT(cmp.mark_abs_otm.count, 100U);
  EXPECT_LE(cmp.mark_abs_otm.median * 100.0, 0.05);
}
TEST(DeribitSnapshot, ListSeriesIsSortedByTime) {
  const auto dirs = opt::deribit::list_series(opt::test::deribit_data_dir() / "series");
  ASSERT_EQ(dirs.size(), 6U);
  for (std::size_t i = 1; i < dirs.size(); ++i) {
    EXPECT_LT(dirs[i - 1].filename().string(), dirs[i].filename().string());
  }
  std::int64_t prev = 0;
  for (const auto& d : dirs) {
    const auto snap = load_snapshot(d);
    EXPECT_EQ(snap.expiries.size(), 2U);
    EXPECT_GT(snap.valuation_ms, prev);
    prev = snap.valuation_ms;
  }
  EXPECT_THROW((void)opt::deribit::list_series(opt::test::deribit_data_dir() / "no-such-dir"),
               DataError);
}
// ---------------------------------------------------------------------------
// Handcrafted tiny snapshots
// ---------------------------------------------------------------------------
constexpr long long kNowMs = 1'790'000'000'000LL;
constexpr long long kExpiryMs = kNowMs + 30LL * 86'400'000LL;
class TinySnapshot : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = fs::temp_directory_path() /
           ("oe_deribit_" + std::string{info->name()} + "_" + std::to_string(::getpid()));
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    write("index_price.json", R"({"result":{"index_price":50000.0}})");
    write("meta.json", R"({"currency":"BTC"})");
  }
  void TearDown() override { fs::remove_all(dir_); }
  void write(const std::string& name, const std::string& text) const {
    std::ofstream(dir_ / name) << text;
  }
  static std::string instrument(const std::string& name, const std::string& type, double strike,
                                long long expiry = kExpiryMs) {
    return R"({"instrument_name":")" + name + R"(","kind":"option","option_type":")" + type +
           R"(","strike":)" + std::to_string(strike) +
           R"(,"expiration_timestamp":)" + std::to_string(expiry) + "}";
  }
  static std::string book(const std::string& name, const std::string& bid,
                          const std::string& ask, const std::string& underlying = "50100.0") {
    return R"({"instrument_name":")" + name + R"(","bid_price":)" + bid + R"(,"ask_price":)" +
           ask + R"(,"mark_price":0.05,"mark_iv":60.0,"underlying_price":)" + underlying +
           R"(,"creation_timestamp":)" + std::to_string(kNowMs) + R"(,"open_interest":1.5})";
  }
  fs::path dir_;
};
TEST_F(TinySnapshot, NullCrossedMissingWrongTypeAndExpiredRows) {
  write("instruments.json",
        R"({"result":[)" + instrument("BTC-X-50000-C", "call", 50000) + "," +
            instrument("BTC-X-52000-C", "call", 52000) + "," +
            instrument("BTC-X-48000-P", "put", 48000) + "," +
            R"({"instrument_name":"BTC-X-49000-P","kind":"option","option_type":"put",)"
            R"("strike":"49000","expiration_timestamp":)" + std::to_string(kExpiryMs) + "}," +
            instrument("BTC-OLD-50000-C", "call", 50000, kNowMs - 1) + "," +
            instrument("BTC-X-51000-C", "call", 51000) + "]}");
  const std::string missing_mark_iv =
      R"({"instrument_name":"BTC-X-51000-C","bid_price":0.01,"ask_price":0.02,)"
      R"("mark_price":0.015,"underlying_price":50100.0,"creation_timestamp":)" +
      std::to_string(kNowMs) + R"(,"open_interest":0})";
  write("book_summary.json", R"({"result":[)" + book("BTC-X-50000-C", "null", "0.06") + "," +
                                 book("BTC-X-52000-C", "0.05", "0.04") + "," +
                                 book("BTC-X-48000-P", "0.03", "0.035") + "," +
                                 book("BTC-X-49000-P", "0.03", "0.035") + "," +
                                 book("BTC-OLD-50000-C", "0.01", "0.02") + "," +
                                 book("BTC-NONE-1-C", "0.01", "0.02") + "," + missing_mark_iv +
                                 "]}");
  const auto snap = load_snapshot(dir_);
  ASSERT_EQ(snap.quotes.size(), 3U);
  ASSERT_EQ(snap.expiries.size(), 1U);
  EXPECT_EQ(snap.valuation_ms, kNowMs);
  EXPECT_DOUBLE_EQ(snap.index_price, 50000.0);
  EXPECT_DOUBLE_EQ(snap.expiries[0].forward, 50100.0);
  EXPECT_DOUBLE_EQ(snap.expiries[0].tau, 30.0 / 365.0);
  auto find = [&snap](const std::string& name) -> const opt::deribit::Quote& {
    for (const auto& q : snap.quotes) {
      if (q.name == name) {
        return q;
      }
    }
    throw std::runtime_error("missing " + name);
  };
  const auto& null_bid = find("BTC-X-50000-C");
  EXPECT_FALSE(null_bid.bid_btc.has_value());
  ASSERT_TRUE(null_bid.ask_btc.has_value());
  EXPECT_DOUBLE_EQ(*null_bid.ask_btc, 0.06);
  EXPECT_DOUBLE_EQ(null_bid.mark_iv, 0.60);
  const auto& crossed = find("BTC-X-52000-C");
  EXPECT_FALSE(crossed.bid_btc.has_value());
  EXPECT_FALSE(crossed.ask_btc.has_value());
  EXPECT_DOUBLE_EQ(crossed.mark_btc, 0.05);
  EXPECT_EQ(find("BTC-X-48000-P").type, opt::OptionType::Put);
  EXPECT_EQ(snap.skipped.at("crossed_quote"), 1U);
  EXPECT_EQ(snap.skipped.at("bad_field:strike"), 1U);   // strike given as a string
  EXPECT_EQ(snap.skipped.at("bad_field:mark_iv"), 1U);  // field missing
  EXPECT_EQ(snap.skipped.at("expired"), 1U);
  // BTC-NONE has no instrument; BTC-X-49000-P's instrument was rejected, so
  // its book row has no match either.
  EXPECT_EQ(snap.skipped.at("unmatched"), 2U);
  // Strikes sorted.
  const auto& idx = snap.expiries[0].quote_index;
  EXPECT_EQ(snap.quotes[idx[0]].strike, 48000.0);
  EXPECT_EQ(snap.quotes[idx[2]].strike, 52000.0);
}
TEST_F(TinySnapshot, ForwardMismatchUsesMedian) {
  write("instruments.json", R"({"result":[)" + instrument("BTC-X-50000-C", "call", 50000) + "," +
                                instrument("BTC-X-51000-C", "call", 51000) + "," +
                                instrument("BTC-X-52000-C", "call", 52000) + "]}");
  write("book_summary.json",
        R"({"result":[)" + book("BTC-X-50000-C", "0.01", "0.02") + "," +
            book("BTC-X-51000-C", "0.01", "0.02") + "," +
            book("BTC-X-52000-C", "0.01", "0.02", "50200.0") + "]}");
  const auto snap = load_snapshot(dir_);
  EXPECT_DOUBLE_EQ(snap.expiries[0].forward, 50100.0);
  EXPECT_EQ(snap.skipped.at("forward_mismatch"), 1U);
}
TEST_F(TinySnapshot, MissingFileIsFatal) {
  write("book_summary.json", R"({"result":[]})");
  try {
    (void)load_snapshot(dir_);
    FAIL() << "expected DataError";
  } catch (const DataError& e) {
    EXPECT_NE(std::string{e.what()}.find("instruments.json"), std::string::npos) << e.what();
  }
}
TEST_F(TinySnapshot, MalformedJsonIsFatal) {
  write("instruments.json", R"({"result":[{"instrument_name": )");
  write("book_summary.json", R"({"result":[]})");
  try {
    (void)load_snapshot(dir_);
    FAIL() << "expected DataError";
  } catch (const DataError& e) {
    EXPECT_NE(std::string{e.what()}.find("invalid JSON"), std::string::npos) << e.what();
  }
}
TEST_F(TinySnapshot, EmptyResultAndBadIndexAreFatal) {
  write("instruments.json", R"({"result":[)" + instrument("BTC-X-50000-C", "call", 50000) + "]}");
  write("book_summary.json", R"({"result":[]})");
  try {
    (void)load_snapshot(dir_);
    FAIL() << "expected DataError";
  } catch (const DataError& e) {
    EXPECT_NE(std::string{e.what()}.find("/result: empty"), std::string::npos) << e.what();
  }
  write("book_summary.json", R"({"result":[)" + book("BTC-X-50000-C", "0.01", "0.02") + "]}");
  write("index_price.json", R"({"result":{"index_price":0}})");
  EXPECT_THROW((void)load_snapshot(dir_), DataError);
  write("index_price.json", R"({"result":{"index_price":50000.0}})");
  EXPECT_NO_THROW((void)load_snapshot(dir_));
}
}  // namespace
