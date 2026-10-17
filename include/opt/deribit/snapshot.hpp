#pragma once
// Loader for a Deribit option-chain snapshot written by
// scripts/fetch_deribit.sh (or trimmed by scripts/trim_snapshot.sh).
//
// A snapshot directory holds instruments.json (get_instruments),
// book_summary.json (get_book_summary_by_currency), index_price.json
// (get_index_price) and meta.json. Conventions (docs/conventions.md):
//   - Options are inverse: bid/ask/mark are in BTC (or ETH) per 1 coin of
//     notional. USD price = price_btc * forward.
//   - The forward of an expiry is the median `underlying_price` of its rows
//     (the future for that expiry, or Deribit's synthetic forward).
//   - Valuation time = max `creation_timestamp` over book rows (ms, UTC).
//   - tau = (expiration_timestamp - valuation_ms) / (365 * 86,400,000):
//     ACT/365 on milliseconds, 24/7.
//   - mark_iv is converted from percent to a decimal.
//
// Fatal problems (missing file, invalid JSON, missing or empty `result`,
// index price not positive) throw DataError with the path and JSON pointer.
// Bad rows are skipped and counted by reason in Snapshot::skipped:
//   unmatched, not_option, bad_field:<name>, bad_value:<name>, expired.
// Rows kept but flagged are counted too: crossed_quote (bid > ask, both
// dropped, mark kept) and forward_mismatch (underlying_price differs from
// the expiry median by more than 1e-9 relative; the slice uses the median).
#include "opt/pricing/types.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace opt::deribit {
class DataError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};
struct Quote {
  std::string name;
  OptionType type = OptionType::Call;
  double strike = 0.0;
  std::int64_t expiry_ms = 0;
  double tau = 0.0;       // years, ACT/365
  double forward = 0.0;   // the expiry's (median) forward, USD
  double underlying_price = 0.0;  // this row's own underlying_price, USD
  std::optional<double> bid_btc;
  std::optional<double> ask_btc;
  double mark_btc = 0.0;
  double mark_iv = 0.0;  // decimal
  double open_interest = 0.0;
};
struct ExpirySlice {
  std::int64_t expiry_ms = 0;
  double tau = 0.0;
  double forward = 0.0;
  std::vector<std::size_t> quote_index;  // into Snapshot::quotes, sorted by strike
};
struct Snapshot {
  std::string currency;
  std::int64_t valuation_ms = 0;      // max creation_timestamp
  std::int64_t valuation_min_ms = 0;  // min creation_timestamp
  double index_price = 0.0;
  std::vector<Quote> quotes;
  std::vector<ExpirySlice> expiries;  // sorted by expiry
  std::map<std::string, std::size_t> skipped;  // reason -> count
  [[nodiscard]] std::int64_t valuation_spread_ms() const noexcept {
    return valuation_ms - valuation_min_ms;
  }
};
inline constexpr double kMsPerYear = 365.0 * 86'400'000.0;
[[nodiscard]] Snapshot load_snapshot(const std::filesystem::path& dir);
// Snapshot directories (<CUR>-<YYYYMMDDTHHMMSSZ>) directly under dir, sorted
// by name (= by time). Throws DataError when dir is not a directory.
[[nodiscard]] std::vector<std::filesystem::path> list_series(const std::filesystem::path& dir);
}  // namespace opt::deribit
