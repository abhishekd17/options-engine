#include "opt/mm/recorded.hpp"

#include "opt/deribit/snapshot.hpp"
#include "opt/mm/config.hpp"
#include "opt/mm/market.hpp"
#include "opt/vol/market_iv.hpp"
#include "opt/vol/surface.hpp"
#include "opt/vol/svi_fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace opt::mm {
namespace {

double years_between(std::int64_t from_ms, std::int64_t to_ms) {
  return static_cast<double>(to_ms - from_ms) / deribit::kMsPerYear;
}

}  // namespace

RecordedSource::RecordedSource(const std::filesystem::path& series_dir, const SimConfig& config)
    : paths_(deribit::list_series(series_dir)) {
  if (paths_.empty()) {
    throw deribit::DataError(series_dir.string() + ": no snapshot directories");
  }
  first_ = deribit::load_snapshot(paths_.front());
  const deribit::Snapshot& snap = *first_;
  t0_ms_ = snap.valuation_ms;
  const auto ivs = compute_market_ivs(snap);
  const auto slices = select_fit_points(snap, ivs);
  for (const auto& s : slices) {
    if (s.selection == SliceSelection::Selected && expiry_ms_.size() < config.quoted_expiries) {
      expiry_ms_.push_back(snap.expiries[s.expiry].expiry_ms);
    }
  }
  if (expiry_ms_.empty()) {
    throw deribit::DataError(paths_.front().string() + ": no expiry selected for fitting");
  }
  for (const std::int64_t ems : expiry_ms_) {
    const auto it = std::find_if(snap.expiries.begin(), snap.expiries.end(),
                                 [ems](const deribit::ExpirySlice& e) { return e.expiry_ms == ems; });
    std::set<std::size_t> used;  // quote indices already chosen for this expiry
    for (const double k : config.target_k) {
      const OptionType type = k < 0.0 ? OptionType::Put : OptionType::Call;
      const double target = it->forward * std::exp(k);
      std::size_t best = snap.quotes.size();
      double best_dist = 0.0;
      for (const std::size_t qi : it->quote_index) {
        const auto& q = snap.quotes[qi];
        if (q.type != type) {
          continue;
        }
        const double dist = std::abs(q.strike - target);
        if (best == snap.quotes.size() || dist < best_dist) {
          best = qi;
          best_dist = dist;
        }
      }
      if (best == snap.quotes.size() || !used.insert(best).second) {
        continue;
      }
      const auto& q = snap.quotes[best];
      instruments_.push_back({q.name, q.type, q.strike, years_between(t0_ms_, q.expiry_ms)});
    }
  }
  listed_.assign(instruments_.size(), 1);
}

void RecordedSource::load(const deribit::Snapshot& snap) {
  t_ = years_between(t0_ms_, snap.valuation_ms);
  spot_ = snap.index_price;
  std::set<std::string> names;
  for (const auto& q : snap.quotes) {
    names.insert(q.name);
  }
  for (std::size_t i = 0; i < instruments_.size(); ++i) {
    listed_[i] = names.count(instruments_[i].name) != 0 ? 1 : 0;
  }
  const auto ivs = compute_market_ivs(snap);
  const auto slices = select_fit_points(snap, ivs);
  fits_.clear();
  std::vector<SurfaceSlice> surf;
  bool ok = true;
  for (const std::int64_t ems : expiry_ms_) {
    RecordedFit rf;
    rf.expiry_ms = ems;
    const auto it = std::find_if(slices.begin(), slices.end(), [&](const FitSlice& s) {
      return snap.expiries[s.expiry].expiry_ms == ems;
    });
    if (it == slices.end() || it->selection != SliceSelection::Selected) {
      rf.status = it == slices.end() ? SviStatus::Failed : SviStatus::TooFewPoints;
      ok = false;
      fits_.push_back(rf);
      continue;
    }
    const auto fit = fit_svi(it->k, it->w, it->tau);
    rf.status = fit.status;
    rf.rmse_volpts = fit.rmse_vol;
    rf.points = fit.n_points;
    fits_.push_back(rf);
    if (fit.status != SviStatus::Ok) {
      ok = false;
      continue;
    }
    // Slice tau uses the same arithmetic as the simulator's
    // instrument.expiry - t, so instrument and slice taus are equal.
    const double tau = years_between(t0_ms_, ems) - t_;
    surf.push_back({tau, it->forward, fit.p});
  }
  surface_ok_ = ok;
  if (ok) {
    surface_.emplace(std::move(surf));
  }
}

bool RecordedSource::next(MarketState& m) {
  if (pos_ >= paths_.size()) {
    return false;
  }
  if (pos_ == 0) {
    load(*first_);
    first_.reset();
    if (!surface_) {
      throw deribit::DataError(paths_.front().string() +
                               ": the first snapshot has no usable surface");
    }
  } else {
    load(deribit::load_snapshot(paths_[pos_]));
  }
  ++pos_;
  m.t_years = t_;
  m.spot = spot_;
  m.surface = &*surface_;
  m.surface_ok = surface_ok_;
  m.arbitrage_ok = true;
  m.listed = listed_;
  return true;
}

}  // namespace opt::mm
