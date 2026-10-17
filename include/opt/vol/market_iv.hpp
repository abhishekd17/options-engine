#pragma once
// Market implied vols of a Deribit snapshot and the selection of SVI fit
// points.
//
// Each quote's BTC prices are converted to USD with the expiry forward F
// (price_usd = price_btc * F) and inverted with the Part 1 solver as
// Black-76 with zero rate: implied_vol_black(type, price_usd, F, K, tau, 1).
// The code is compiled into the opt::deribit library because it reads a
// deribit::Snapshot; it lives under opt/vol/ because it produces the fit
// input for the vol layer.
#include "opt/deribit/snapshot.hpp"
#include "opt/pricing/implied_vol.hpp"
#include <array>
#include <cstddef>
#include <span>
#include <vector>
namespace opt {
inline constexpr std::size_t kIvStatusCount = 6;
[[nodiscard]] constexpr const char* to_string(IvStatus s) noexcept {
  switch (s) {
    case IvStatus::Ok: return "Ok";
    case IvStatus::InvalidInput: return "InvalidInput";
    case IvStatus::BelowIntrinsic: return "BelowIntrinsic";
    case IvStatus::AboveUpperBound: return "AboveUpperBound";
    case IvStatus::AboveMaxVol: return "AboveMaxVol";
    case IvStatus::NotConverged: return "NotConverged";
  }
  return "?";
}
struct MarketIv {
  std::size_t quote = 0;  // index into Snapshot::quotes
  double k = 0.0;         // ln(K / F)
  bool otm = false;       // call with K >= F, or put with K < F
  IvResult bid;           // InvalidInput when the side is missing
  IvResult ask;
  IvResult mid;           // from (bid + ask) / 2; InvalidInput unless both sides exist
  IvResult mark;          // from mark_price
};
// One entry per Snapshot::quotes element, in the same order.
[[nodiscard]] std::vector<MarketIv> compute_market_ivs(const deribit::Snapshot& snap);
// Order statistics of a set of differences (decimals, not vol points).
// Percentiles use the nearest-rank rule.
struct DiffStats {
  std::size_t count = 0;
  double median = 0.0;
  double p05 = 0.0;
  double p95 = 0.0;
  double max_abs = 0.0;
};
struct MarkComparison {
  DiffStats mark_abs_otm;    // |iv_mark - mark_iv|, OTM quotes with iv_mark Ok
  DiffStats mark_abs_itm;    // same, ITM
  DiffStats mid_signed_otm;  // iv_mid - mark_iv, OTM quotes with iv_mid Ok
  DiffStats mid_signed_itm;  // same, ITM
  std::array<std::size_t, kIvStatusCount> mark_status{};
  std::array<std::size_t, kIvStatusCount> mid_status{};
};
[[nodiscard]] DiffStats diff_stats(std::vector<double> values);
[[nodiscard]] MarkComparison compare_to_mark(const deribit::Snapshot& snap,
                                             std::span<const MarketIv> ivs);
struct FitSelectOptions {
  double min_tau = 1.0 / 365.0;  // years; shorter expiries are reported, not fitted
  int min_points = 5;
};
enum class SliceSelection { Selected, TooShort, TooFewPoints };
[[nodiscard]] constexpr const char* to_string(SliceSelection s) noexcept {
  switch (s) {
    case SliceSelection::Selected: return "Selected";
    case SliceSelection::TooShort: return "TooShort";
    case SliceSelection::TooFewPoints: return "TooFewPoints";
  }
  return "?";
}
// Fit input of one expiry: OTM quotes with bid and ask present, bid > 0 and
// iv_mid Ok. Market vol = iv_mid, target total variance w = iv_mid^2 tau.
struct FitSlice {
  std::size_t expiry = 0;  // index into Snapshot::expiries
  double tau = 0.0;
  double forward = 0.0;
  SliceSelection selection = SliceSelection::TooFewPoints;
  std::vector<std::size_t> iv_index;  // into the MarketIv vector, sorted by k
  std::vector<double> k;
  std::vector<double> w;
  std::vector<double> vol;
};
// One FitSlice per expiry (in expiry order), including the ones not selected.
[[nodiscard]] std::vector<FitSlice> select_fit_points(const deribit::Snapshot& snap,
                                                      std::span<const MarketIv> ivs,
                                                      const FitSelectOptions& options = {});
}  // namespace opt
