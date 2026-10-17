// SVI fit on the committed Deribit snapshot: a regression guard. The printed
// values are informational; docs/conventions.md quotes the vol_surface tool.
#include "opt/deribit/snapshot.hpp"
#include "opt/vol/market_iv.hpp"
#include "opt/vol/svi_fit.hpp"
#include "../deribit/snapshot_dir.hpp"
#include <gtest/gtest.h>
#include <cstdio>
namespace {
TEST(SnapshotFit, EveryExpiryOfAtLeastOneDayFitsOk) {
  const auto snap = opt::deribit::load_snapshot(opt::test::committed_snapshot_dir());
  const auto ivs = opt::compute_market_ivs(snap);
  const auto slices = opt::select_fit_points(snap, ivs);
  int fitted = 0;
  for (const auto& s : slices) {
    if (s.tau < 1.0 / 365.0) {
      EXPECT_EQ(s.selection, opt::SliceSelection::TooShort);
      continue;
    }
    ASSERT_EQ(s.selection, opt::SliceSelection::Selected) << "tau " << s.tau;
    const auto r = opt::fit_svi(s.k, s.w, s.tau);
    std::printf("  tau %.6f: %d points, rmse %.4f vol pts, max %.4f vol pts, %s\n", s.tau,
                r.n_points, r.rmse_vol, r.max_abs_vol_err, opt::to_string(r.status));
    EXPECT_EQ(r.status, opt::SviStatus::Ok);
    EXPECT_LT(r.rmse_vol, 2.0);
    ++fitted;
  }
  EXPECT_GE(fitted, 5);
}
}  // namespace
