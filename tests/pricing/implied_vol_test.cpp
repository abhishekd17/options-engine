#include "opt/pricing/implied_vol.hpp"

#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/detail/iv_grid.hpp"
#include "opt/pricing/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace {

using opt::IvStatus;
using opt::OptionSpec;
using opt::OptionType;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

OptionSpec spec(OptionType type, double s, double k, double t, double r, double q, double vol) {
  return OptionSpec{type, s, k, t, r, q, vol};
}

// Design 5.8 item 2b. J. Hull, "Options, Futures, and Other Derivatives",
// 9th ed., section 15.11: S = 21, K = 20, r = 0.10, T = 0.25 and a call
// price of 1.875 give sigma = 0.235. Citation as given in the design; not
// re-checked against a printed copy. Tolerance 0.0005 (the book rounds to
// three decimals).
TEST(ImpliedVol, HullSection15_11) {
  const auto r = opt::implied_vol_bsm(1.875, spec(OptionType::Call, 21, 20, 0.25, 0.10, 0, 0));
  ASSERT_EQ(r.status, IvStatus::Ok);
  EXPECT_NEAR(r.vol, 0.235, 0.0005);
  std::printf("[measure] Hull 15.11: implied vol = %.6f (book 0.235), %d iterations\n", r.vol,
              r.iterations);
}

// Design 5.8 item 4: round trip price -> IV over the 56,000-point grid.
//
// Well-conditioned points (definition and the reason it differs from the
// design in include/opt/pricing/detail/iv_grid.hpp): status Ok and
// |vol_hat - vol| <= 1e-10.
// Elsewhere: status Ok, or BelowIntrinsic only where the price rounded to
// <= intrinsic. When Ok the reprice must match to 1e-10 relative, plus a
// rounding floor of 16 eps x price_scale: the price is the difference of two
// terms of size price_scale, so for a tiny out-of-the-money price no vol at
// all reproduces it to 1e-10 relative (measured: 2 of 56,000 points needed
// the floor, at 7.8 eps x price_scale; see docs/accuracy.md).
TEST(ImpliedVol, RoundTripGrid) {
  constexpr double kEps = 0x1.0p-52;
  std::size_t points = 0;
  std::size_t well = 0;
  std::size_t ill_ok = 0;
  std::size_t ill_below = 0;
  std::size_t ill_above = 0;
  std::size_t ill_floor = 0;      // Ok points that needed the rounding floor
  double max_floor_ratio = 0.0;   // their |dP| / (eps price_scale)
  double max_err_well = 0.0;
  double max_reprice_ill = 0.0;
  long long iter_sum = 0;
  int iter_max = 0;
  std::size_t ok_count = 0;
  opt::detail::for_each_iv_grid_point([&](const opt::detail::IvGridPoint& p) {
    ++points;
    const auto r = opt::implied_vol_bsm(p.price, p.spec);
    if (r.status == IvStatus::Ok) {
      ++ok_count;
      iter_sum += r.iterations;
      iter_max = std::max(iter_max, r.iterations);
    } else {
      EXPECT_TRUE(std::isnan(r.vol));
    }
    const auto where = [&] {
      return std::string{"type="} + (p.spec.type == OptionType::Call ? "call" : "put") +
             " vol=" + std::to_string(p.spec.vol) + " T=" + std::to_string(p.spec.time) +
             " z=" + std::to_string(p.z) + " r=" + std::to_string(p.spec.rate) +
             " q=" + std::to_string(p.spec.carry_yield) + " status=" +
             std::to_string(static_cast<int>(r.status));
    };
    if (p.well_conditioned) {
      ++well;
      ASSERT_EQ(r.status, IvStatus::Ok) << where();
      const double err = std::abs(r.vol - p.spec.vol);
      max_err_well = std::max(max_err_well, err);
      EXPECT_LE(err, 1e-10) << where() << " vol_hat=" << r.vol;
      return;
    }
    if (r.status == IvStatus::Ok) {
      ++ill_ok;
      OptionSpec back = p.spec;
      back.vol = r.vol;
      const double diff = std::abs(opt::bsm_price(back) - p.price);
      max_reprice_ill = std::max(max_reprice_ill, diff / p.price);
      if (diff > 1e-10 * p.price) {
        ++ill_floor;
        max_floor_ratio = std::max(max_floor_ratio, diff / (kEps * p.price_scale));
      }
      EXPECT_LE(diff, 1e-10 * p.price + 16 * kEps * p.price_scale) << where();
      return;
    }
    // Only allowed where the price rounded onto a no-arbitrage bound:
    // BelowIntrinsic where it underflowed to <= intrinsic, and (the mirror
    // case, deep in the money at sigma = 3, T = 5) AboveUpperBound where it
    // rounded up to the bound F (call) or K (put).
    const double w = opt::omega(p.spec.type);
    const double fwd = opt::bsm_forward(p.spec);
    const double c = p.price / opt::bsm_discount(p.spec);
    if (r.status == IvStatus::AboveUpperBound) {
      ++ill_above;
      EXPECT_GE(c, p.spec.type == OptionType::Call ? fwd : p.spec.strike) << where();
      return;
    }
    ASSERT_EQ(r.status, IvStatus::BelowIntrinsic) << where();
    ++ill_below;
    EXPECT_LE(c, std::max(w * (fwd - p.spec.strike), 0.0)) << where();
  });
  EXPECT_EQ(points, opt::detail::kIvGridPoints);
  const double mean_iter =
      ok_count == 0 ? 0.0 : static_cast<double>(iter_sum) / static_cast<double>(ok_count);
  RecordProperty("max_vol_error_well_conditioned", std::to_string(max_err_well));
  RecordProperty("max_reprice_error_ill_conditioned", std::to_string(max_reprice_ill));
  RecordProperty("mean_iterations", std::to_string(mean_iter));
  RecordProperty("max_iterations", std::to_string(iter_max));
  std::printf(
      "[measure] IV round trip: %zu points, %zu well-conditioned (max |vol_hat - vol| = %.3g, "
      "tolerance 1e-10); ill-conditioned: %zu Ok (max reprice rel error %.3g; %zu above 1e-10 "
      "relative, max %.3g eps x price_scale, floor 16), %zu BelowIntrinsic, %zu "
      "AboveUpperBound; iterations (Ok) mean %.2f max %d\n",
      points, well, max_err_well, ill_ok, max_reprice_ill, ill_floor, max_floor_ratio, ill_below,
      ill_above, mean_iter, iter_max);
}

// Design 5.8 item 5: edge cases.
TEST(ImpliedVol, BoundsAndInvalidInputs) {
  const double f = 100.0;
  const double k = 90.0;
  const double t = 0.5;
  const double d = std::exp(-0.05 * t);
  auto solve = [&](OptionType type, double price, double ff = 100.0, double kk = 90.0,
                   double tt = 0.5, double dd = -1.0) {
    return opt::implied_vol_black(type, price, ff, kk, tt, dd < 0 ? d : dd);
  };
  // Below and exactly at intrinsic (call intrinsic is D (F - K)). With
  // D = 1 the price equals intrinsic exactly.
  EXPECT_EQ(solve(OptionType::Call, 0.99 * d * (f - k)).status, IvStatus::BelowIntrinsic);
  EXPECT_EQ(solve(OptionType::Call, f - k, f, k, t, 1.0).status, IvStatus::BelowIntrinsic);
  EXPECT_EQ(solve(OptionType::Put, k - 85.0, 85.0, k, t, 1.0).status, IvStatus::BelowIntrinsic);
  // Price 0 for an OTM option (real Deribit data has these).
  EXPECT_EQ(solve(OptionType::Put, 0.0).status, IvStatus::BelowIntrinsic);
  // Upper bound: call < D F, put < D K.
  EXPECT_EQ(solve(OptionType::Call, f, f, k, t, 1.0).status, IvStatus::AboveUpperBound);
  EXPECT_EQ(solve(OptionType::Call, 1.01 * d * f).status, IvStatus::AboveUpperBound);
  EXPECT_EQ(solve(OptionType::Put, k, f, k, t, 1.0).status, IvStatus::AboveUpperBound);
  EXPECT_EQ(solve(OptionType::Put, 1.01 * d * k).status, IvStatus::AboveUpperBound);
  // Invalid inputs.
  EXPECT_EQ(solve(OptionType::Call, 12.0, f, k, 0.0).status, IvStatus::InvalidInput);  // T = 0
  EXPECT_EQ(solve(OptionType::Call, -1.0).status, IvStatus::InvalidInput);
  EXPECT_EQ(solve(OptionType::Call, kNaN).status, IvStatus::InvalidInput);
  EXPECT_EQ(solve(OptionType::Call, 12.0, kNaN).status, IvStatus::InvalidInput);
  EXPECT_EQ(solve(OptionType::Call, 12.0, f, kNaN).status, IvStatus::InvalidInput);
  EXPECT_EQ(solve(OptionType::Call, 12.0, f, k, kNaN).status, IvStatus::InvalidInput);
  EXPECT_EQ(solve(OptionType::Call, 12.0, f, k, t, kNaN).status, IvStatus::InvalidInput);
  EXPECT_EQ(solve(OptionType::Call, 12.0, -f).status, IvStatus::InvalidInput);
  EXPECT_EQ(solve(OptionType::Call, 12.0, f, 0.0).status, IvStatus::InvalidInput);
  EXPECT_EQ(solve(OptionType::Call, std::numeric_limits<double>::infinity()).status,
            IvStatus::InvalidInput);
  EXPECT_EQ(opt::implied_vol_bsm(5.0, spec(OptionType::Call, kNaN, 100, 1, 0, 0, 0)).status,
            IvStatus::InvalidInput);
  for (const double price : {0.99 * d * (f - k), 0.0, d * f, -1.0, kNaN}) {
    EXPECT_TRUE(std::isnan(solve(OptionType::Call, price).vol)) << price;
  }
}

double round_trip(const OptionSpec& s, IvStatus expected = IvStatus::Ok) {
  const double price = opt::bsm_price(s);
  const auto r = opt::implied_vol_bsm(price, s);
  EXPECT_EQ(r.status, expected) << "vol " << s.vol << " K " << s.strike << " T " << s.time;
  return r.vol;
}

TEST(ImpliedVol, DeepInTheMoneyCallSolvedViaPut) {
  // K = 50 against S = 100: the call price is mostly intrinsic.
  const OptionSpec s = spec(OptionType::Call, 100, 50, 1.0, 0.03, 0.0, 0.35);
  EXPECT_NEAR(round_trip(s), 0.35, 1e-12);
  const OptionSpec p = spec(OptionType::Put, 100, 200, 1.0, 0.03, 0.0, 0.35);
  EXPECT_NEAR(round_trip(p), 0.35, 1e-12);
}

TEST(ImpliedVol, OneHourExpiry) {
  const double hour = 1.0 / (365.0 * 24.0);
  EXPECT_NEAR(round_trip(spec(OptionType::Call, 100, 100.2, hour, 0.05, 0, 0.6)), 0.6, 1e-10);
  EXPECT_NEAR(round_trip(spec(OptionType::Put, 100, 99.5, hour, 0.0, 0, 0.3)), 0.3, 1e-10);
}

TEST(ImpliedVol, HighVolInsideMaxVol) {
  EXPECT_NEAR(round_trip(spec(OptionType::Call, 100, 100, 1.0, 0.0, 0, 5.0)), 5.0, 1e-9);
  EXPECT_NEAR(round_trip(spec(OptionType::Put, 100, 120, 0.25, 0.02, 0, 5.0)), 5.0, 1e-9);
}

TEST(ImpliedVol, VolAboveMaxVol) {
  const OptionSpec s = spec(OptionType::Call, 100, 100, 0.1, 0.0, 0, 12.0);
  const auto r = opt::implied_vol_bsm(opt::bsm_price(s), s);
  EXPECT_EQ(r.status, IvStatus::AboveMaxVol);
  EXPECT_TRUE(std::isnan(r.vol));
  // A larger max_vol finds it.
  opt::IvOptions wide;
  wide.max_vol = 20.0;
  const auto r2 = opt::implied_vol_bsm(opt::bsm_price(s), s, wide);
  ASSERT_EQ(r2.status, IvStatus::Ok);
  EXPECT_NEAR(r2.vol, 12.0, 1e-9);
}

// z = 8: vega is about phi(8) ~ 5e-15 of the forward, so a price carries
// almost no information about sigma. The solver must still return Ok with a
// vol that reprices the input, or BelowIntrinsic if the price underflowed.
TEST(ImpliedVol, ExtremelyLowVega) {
  for (const OptionType type : {OptionType::Call, OptionType::Put}) {
    for (const double vol : {0.05, 0.2, 1.0}) {
      const double t = 0.25;
      OptionSpec s = spec(type, 100, 0, t, 0.01, 0, vol);
      const double fwd = opt::bsm_forward(s);
      const double z = type == OptionType::Call ? 8.0 : -8.0;  // out of the money
      s.strike = fwd * std::exp(z * vol * std::sqrt(t));
      const double price = opt::bsm_price(s);
      const auto r = opt::implied_vol_bsm(price, s);
      if (r.status == IvStatus::Ok) {
        OptionSpec back = s;
        back.vol = r.vol;
        EXPECT_NEAR(opt::bsm_price(back), price, 1e-10 * price) << vol;
        EXPECT_NEAR(r.vol, vol, 1e-6) << vol;  // OTM z = 8 still pins sigma down this well
      } else {
        EXPECT_EQ(r.status, IvStatus::BelowIntrinsic);
      }
    }
  }
}

TEST(ImpliedVol, NotConvergedWithTinyIterationBudget) {
  opt::IvOptions o;
  o.max_iter = 1;
  const OptionSpec s = spec(OptionType::Call, 100, 130, 2.0, 0.0, 0, 0.9);
  const auto r = opt::implied_vol_bsm(opt::bsm_price(s), s, o);
  EXPECT_EQ(r.status, IvStatus::NotConverged);
  EXPECT_TRUE(std::isfinite(r.vol));
  EXPECT_EQ(r.iterations, 1);
}

}  // namespace
