#include "opt/vol/market_iv.hpp"
#include "opt/deribit/snapshot.hpp"
#include "opt/pricing/implied_vol.hpp"
#include "opt/pricing/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>
namespace opt {
namespace {
IvResult solve(const deribit::Quote& q, double price_btc) noexcept {
  return implied_vol_black(q.type, price_btc * q.forward, q.forward, q.strike, q.tau, 1.0);
}
double nearest_rank(const std::vector<double>& sorted, double p) noexcept {
  const auto n = static_cast<double>(sorted.size());
  const auto rank = static_cast<std::size_t>(std::max(1.0, std::ceil(p * n)));
  return sorted[std::min(rank, sorted.size()) - 1];
}
}  // namespace
std::vector<MarketIv> compute_market_ivs(const deribit::Snapshot& snap) {
  std::vector<MarketIv> out;
  out.reserve(snap.quotes.size());
  for (std::size_t i = 0; i < snap.quotes.size(); ++i) {
    const auto& q = snap.quotes[i];
    MarketIv m;
    m.quote = i;
    m.k = std::log(q.strike / q.forward);
    m.otm = q.type == OptionType::Call ? q.strike >= q.forward : q.strike < q.forward;
    if (q.bid_btc) {
      m.bid = solve(q, *q.bid_btc);
    }
    if (q.ask_btc) {
      m.ask = solve(q, *q.ask_btc);
    }
    if (q.bid_btc && q.ask_btc) {
      m.mid = solve(q, 0.5 * (*q.bid_btc + *q.ask_btc));
    }
    m.mark = solve(q, q.mark_btc);
    out.push_back(m);
  }
  return out;
}
DiffStats diff_stats(std::vector<double> values) {
  DiffStats s;
  s.count = values.size();
  if (values.empty()) {
    return s;
  }
  std::sort(values.begin(), values.end());
  s.median = nearest_rank(values, 0.5);
  s.p05 = nearest_rank(values, 0.05);
  s.p95 = nearest_rank(values, 0.95);
  s.max_abs = std::max(std::abs(values.front()), std::abs(values.back()));
  return s;
}
MarkComparison compare_to_mark(const deribit::Snapshot& snap, std::span<const MarketIv> ivs) {
  MarkComparison c;
  std::vector<double> mark_otm;
  std::vector<double> mark_itm;
  std::vector<double> mid_otm;
  std::vector<double> mid_itm;
  for (const MarketIv& m : ivs) {
    const double mark_iv = snap.quotes[m.quote].mark_iv;
    ++c.mark_status[static_cast<std::size_t>(m.mark.status)];
    ++c.mid_status[static_cast<std::size_t>(m.mid.status)];
    if (m.mark.status == IvStatus::Ok) {
      (m.otm ? mark_otm : mark_itm).push_back(std::abs(m.mark.vol - mark_iv));
    }
    if (m.mid.status == IvStatus::Ok) {
      (m.otm ? mid_otm : mid_itm).push_back(m.mid.vol - mark_iv);
    }
  }
  c.mark_abs_otm = diff_stats(std::move(mark_otm));
  c.mark_abs_itm = diff_stats(std::move(mark_itm));
  c.mid_signed_otm = diff_stats(std::move(mid_otm));
  c.mid_signed_itm = diff_stats(std::move(mid_itm));
  return c;
}
std::vector<FitSlice> select_fit_points(const deribit::Snapshot& snap,
                                        std::span<const MarketIv> ivs,
                                        const FitSelectOptions& options) {
  // MarketIv entries are in quote order, so ivs[quote_index] is the quote's IV.
  std::vector<FitSlice> out;
  out.reserve(snap.expiries.size());
  for (std::size_t e = 0; e < snap.expiries.size(); ++e) {
    const auto& ex = snap.expiries[e];
    FitSlice f;
    f.expiry = e;
    f.tau = ex.tau;
    f.forward = ex.forward;
    for (const std::size_t qi : ex.quote_index) {
      const MarketIv& m = ivs[qi];
      const auto& q = snap.quotes[qi];
      if (!m.otm || !q.bid_btc || !q.ask_btc || !(*q.bid_btc > 0.0) ||
          m.mid.status != IvStatus::Ok) {
        continue;
      }
      f.iv_index.push_back(qi);
    }
    std::sort(f.iv_index.begin(), f.iv_index.end(),
              [&ivs](std::size_t l, std::size_t r) { return ivs[l].k < ivs[r].k; });
    for (const std::size_t qi : f.iv_index) {
      const double v = ivs[qi].mid.vol;
      f.k.push_back(ivs[qi].k);
      f.vol.push_back(v);
      f.w.push_back(v * v * ex.tau);
    }
    if (ex.tau < options.min_tau) {
      f.selection = SliceSelection::TooShort;
    } else if (static_cast<int>(f.k.size()) < options.min_points) {
      f.selection = SliceSelection::TooFewPoints;
    } else {
      f.selection = SliceSelection::Selected;
    }
    out.push_back(std::move(f));
  }
  return out;
}
}  // namespace opt
