#pragma once
// Recorded market source (design 8.2): a series of Deribit snapshots
// (tests/data/deribit/series/ or fetch_deribit.sh --count N output).
//
// Each next() loads the next snapshot, computes market IVs, selects fit
// points, fits SVI per quoted expiry and rebuilds the VolSurface. Spot = the
// index price; t = valuation time minus the first snapshot's valuation time;
// dt follows from consecutive valuation times. If a quoted expiry is missing
// or does not fit with status Ok, surface_ok is false for that state and the
// last good surface is kept for marking. Instruments not in a snapshot are
// reported as unlisted. Loading and fitting allocate; this source is not
// part of the zero-allocation guarantee.
//
// Instruments come from the first snapshot: its first config.quoted_expiries
// expiries that are selected for fitting, and per target k the listed strike
// nearest to F e^k (puts for k < 0, calls for k >= 0; duplicates dropped).
#include "opt/deribit/snapshot.hpp"
#include "opt/mm/config.hpp"
#include "opt/mm/market.hpp"
#include "opt/vol/market_iv.hpp"
#include "opt/vol/surface.hpp"
#include "opt/vol/svi_fit.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace opt::mm {

struct RecordedFit {
  std::int64_t expiry_ms = 0;
  SviStatus status = SviStatus::Failed;
  double rmse_volpts = 0.0;
  int points = 0;
};

class RecordedSource {
 public:
  // Throws deribit::DataError when the directory has no snapshot or the
  // first snapshot has no usable quoted expiry.
  RecordedSource(const std::filesystem::path& series_dir, const SimConfig& config);

  bool next(MarketState& m);

  [[nodiscard]] const std::vector<Instrument>& instruments() const noexcept {
    return instruments_;
  }
  [[nodiscard]] std::size_t snapshot_count() const noexcept { return paths_.size(); }
  // Snapshot loaded by the last next() (index into the series) and its fits.
  [[nodiscard]] std::size_t current() const noexcept { return pos_ - 1; }
  [[nodiscard]] const std::filesystem::path& current_path() const { return paths_[pos_ - 1]; }
  [[nodiscard]] const std::vector<RecordedFit>& last_fits() const noexcept { return fits_; }

 private:
  void load(const deribit::Snapshot& snap);

  std::vector<std::filesystem::path> paths_;
  std::size_t pos_ = 0;
  std::int64_t t0_ms_ = 0;
  std::vector<std::int64_t> expiry_ms_;  // quoted expiries
  std::vector<Instrument> instruments_;
  std::optional<deribit::Snapshot> first_;
  std::optional<VolSurface> surface_;
  std::vector<std::uint8_t> listed_;
  std::vector<RecordedFit> fits_;
  bool surface_ok_ = false;
  double t_ = 0.0;
  double spot_ = 0.0;
};

}  // namespace opt::mm
