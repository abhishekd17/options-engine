// Runs the implied-vol round-trip grid of design 5.8 item 4 (the same points
// as the ImpliedVol.RoundTripGrid test) and prints accuracy per region.
#include "opt/pricing/black_scholes.hpp"
#include "opt/pricing/detail/iv_grid.hpp"
#include "opt/pricing/implied_vol.hpp"
#include "opt/pricing/types.hpp"

#include "args.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

namespace {

constexpr std::array<const char*, 6> kStatusNames = {
    "Ok", "InvalidInput", "BelowIntrinsic", "AboveUpperBound", "AboveMaxVol", "NotConverged"};

struct RegionStats {
  std::size_t points = 0;
  double max_vol_error = 0.0;
  double max_price_rel_error = 0.0;
  long long iterations = 0;
  int max_iterations = 0;
  std::array<std::size_t, kStatusNames.size()> status{};
};

void print_region(const char* name, const char* definition, const RegionStats& r) {
  const std::size_t ok = r.status[0];
  std::printf("%s (%s)\n", name, definition);
  std::printf("  points:                    %zu\n", r.points);
  std::printf("  max |vol_hat - vol|:       %.3e  (Ok points)\n", r.max_vol_error);
  std::printf("  max |P(vol_hat) - P| / P:  %.3e  (Ok points)\n", r.max_price_rel_error);
  std::printf("  iterations (Ok points):    mean %.2f, max %d\n",
              ok == 0 ? 0.0 : static_cast<double>(r.iterations) / static_cast<double>(ok),
              r.max_iterations);
  std::printf("  status counts:");
  for (std::size_t i = 0; i < kStatusNames.size(); ++i) {
    std::printf(" %s=%zu", kStatusNames[i], r.status[i]);
  }
  std::printf("\n");
}

void usage(std::FILE* out) {
  std::fprintf(out,
               "usage: iv_accuracy [--help]\n"
               "Runs the 56,000-point implied-vol round-trip grid (sigma 0.01..3, T 1h..5y,\n"
               "z = ln(K/F)/(sigma sqrt T) in [-6, 6], calls and puts, r in {0, 0.05},\n"
               "q in {0, 0.03}) and prints accuracy per conditioning region.\n");
}

void add(RegionStats& r, const opt::detail::IvGridPoint& p, const opt::IvResult& res) {
  ++r.points;
  ++r.status[static_cast<std::size_t>(res.status)];
  if (res.status != opt::IvStatus::Ok) {
    return;
  }
  r.iterations += res.iterations;
  r.max_iterations = std::max(r.max_iterations, res.iterations);
  r.max_vol_error = std::max(r.max_vol_error, std::abs(res.vol - p.spec.vol));
  opt::OptionSpec back = p.spec;
  back.vol = res.vol;
  r.max_price_rel_error =
      std::max(r.max_price_rel_error, std::abs(opt::bsm_price(back) - p.price) / p.price);
}

int run() {
  RegionStats well;
  RegionStats ill;
  RegionStats all;
  RegionStats design_well;  // the original design 5.8 definition, for comparison
  opt::detail::for_each_iv_grid_point([&](const opt::detail::IvGridPoint& p) {
    const auto res = opt::implied_vol_bsm(p.price, p.spec);
    add(p.well_conditioned ? well : ill, p, res);
    add(all, p, res);
    const double df = opt::bsm_discount(p.spec) * opt::bsm_forward(p.spec);
    if (p.vega / df >= 1e-6 && p.price / df >= 1e-12) {
      add(design_well, p, res);
    }
  });
  std::printf("implied vol round trip: %zu grid points, S = 100\n", all.points);
  print_region("well-conditioned",
               "vega >= 1e-5 x D (F N(w d1) + K N(w d2)) and price / (D F) >= 1e-12", well);
  print_region("ill-conditioned", "everything else", ill);
  print_region("all points", "both regions", all);
  print_region("design 5.8 definition, for comparison",
               "vega / (D F) >= 1e-6 and price / (D F) >= 1e-12", design_well);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    opt::tools::ArgIterator it{argc, argv};
    while (!it.done()) {
      const std::string_view flag = it.next();
      if (flag == "--help" || flag == "-h") {
        usage(stdout);
        return 0;
      }
      throw opt::tools::UsageError("unknown flag '" + std::string{flag} + "'");
    }
    return run();
  } catch (const opt::tools::UsageError& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    usage(stderr);
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
