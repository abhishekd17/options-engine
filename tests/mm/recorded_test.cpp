// Recorded mode on the committed Deribit series (design 8.10 item 7). Not
// compared exactly: it uses the libm-based IV solver and the SVI fits.
#include "opt/deribit/snapshot.hpp"
#include "opt/mm/config.hpp"
#include "opt/mm/recorded.hpp"
#include "opt/mm/sim.hpp"
#include "opt/vol/svi_fit.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "mm/mm_test_util.hpp"

namespace {

const std::filesystem::path kSeries = std::filesystem::path{OPTENG_TEST_DATA_DIR} / "deribit/series";

opt::mm::SimConfig recorded_config() {
  opt::mm::SimConfig c;
  c.mode = opt::mm::Mode::Recorded;
  return c;
}

TEST(Recorded, InstrumentsFromFirstSnapshot) {
  const opt::mm::RecordedSource src(kSeries, recorded_config());
  EXPECT_EQ(src.snapshot_count(), 6U);
  const auto& inst = src.instruments();
  ASSERT_EQ(inst.size(), 10U);
  std::set<std::string> names;
  for (const auto& in : inst) {
    names.insert(in.name);
    EXPECT_GT(in.expiry, 1.0 / 365.0);
    EXPECT_EQ(in.name.substr(0, 4), "BTC-");
  }
  EXPECT_EQ(names.size(), inst.size());
  EXPECT_EQ(inst[0].type, opt::OptionType::Put);
  EXPECT_EQ(inst[2].type, opt::OptionType::Call);
}

TEST(Recorded, RunsToTheEndWithInvariants) {
  const auto c = recorded_config();
  opt::mm::RecordedSource src(kSeries, c);
  opt::mm::Simulator sim(c, src.instruments());
  opt::mm::test::Recorder rec;
  rec.sim = &sim;
  rec.limits = c.limits;
  rec.threshold = c.hedge.threshold;
  sim.set_event_sink(&opt::mm::test::Recorder::on_event, &rec);
  int fitted_states = 0;
  opt::mm::SimStatus st = opt::mm::SimStatus::Running;
  while ((st = sim.step(src)) == opt::mm::SimStatus::Running) {
    bool all_ok = true;
    for (const auto& f : src.last_fits()) {
      all_ok = all_ok && f.status == opt::SviStatus::Ok;
    }
    fitted_states += all_ok ? 1 : 0;
    EXPECT_LE(std::abs(sim.risk().net_delta), c.hedge.threshold);
  }
  EXPECT_EQ(st, opt::mm::SimStatus::Finished);
  EXPECT_EQ(sim.counters().states, 6U);
  // Surface fitted at every step, or the skip is counted.
  EXPECT_EQ(static_cast<std::uint64_t>(fitted_states) + sim.counters().steps_without_surface, 6U);
  std::printf("recorded: fills %llu hedges %llu steps without surface %llu  total %.6f ledger "
              "%.6f diff %.3e\n",
              static_cast<unsigned long long>(rec.fills),
              static_cast<unsigned long long>(rec.hedges),
              static_cast<unsigned long long>(sim.counters().steps_without_surface),
              sim.pnl().total, sim.ledger_pnl(), sim.identity_error());
  EXPECT_GT(rec.fills, 0U);
  EXPECT_EQ(rec.limit_failures, 0U);
  EXPECT_EQ(rec.hedge_failures, 0U);
  EXPECT_TRUE(sim.identity_holds()) << sim.identity_error() << " > " << sim.identity_tol();
}

TEST(Recorded, MissingDirectoryThrows) {
  EXPECT_THROW(opt::mm::RecordedSource(kSeries / "no-such-dir", recorded_config()),
               opt::deribit::DataError);
}

}  // namespace
