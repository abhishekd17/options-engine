// vol_surface: loads a Deribit snapshot, computes market implied vols,
// compares them with Deribit's mark_iv, fits SVI per expiry, checks the
// fitted surface for static arbitrage, and optionally writes a CSV of
// fitted vs market vols and an SVG of the smiles.
#include "opt/deribit/snapshot.hpp"
#include "opt/vol/arbitrage.hpp"
#include "opt/vol/market_iv.hpp"
#include "opt/vol/surface.hpp"
#include "opt/vol/svi.hpp"
#include "opt/vol/svi_fit.hpp"
#include "args.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace {
namespace fs = std::filesystem;
using opt::deribit::Snapshot;
struct Options {
  fs::path snapshot;
  std::optional<fs::path> csv;
  std::optional<fs::path> svg;
  int min_points = 5;
  double min_tau_days = 1.0;
};
void usage(std::FILE* out) {
  std::fprintf(out,
               "usage: vol_surface <snapshot_dir> [--csv FILE] [--min-points N]\n"
               "                   [--min-tau-days D] [--svg FILE]\n"
               "Fits raw SVI per expiry of a Deribit snapshot (fetch_deribit.sh output),\n"
               "prints fit RMSE, the mark_iv convention check and static-arbitrage checks.\n"
               "  --csv FILE         write fitted vs market vols per quote\n"
               "  --min-points N     minimum OTM quotes per expiry to fit (default 5)\n"
               "  --min-tau-days D   skip expiries shorter than D days (default 1)\n"
               "  --svg FILE         write one smile plot per fitted expiry\n");
}
std::string utc_string(std::int64_t ms) {
  const std::time_t secs = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  gmtime_r(&secs, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
  char out[48];
  std::snprintf(out, sizeof out, "%s.%03dZ", buf, static_cast<int>(ms % 1000));
  return out;
}
// The expiry token of a Deribit instrument name, e.g. "27NOV26".
std::string expiry_label(const Snapshot& snap, std::size_t expiry) {
  const std::string& name = snap.quotes[snap.expiries[expiry].quote_index.front()].name;
  const auto a = name.find('-');
  const auto b = name.find('-', a + 1);
  return a == std::string::npos || b == std::string::npos ? name : name.substr(a + 1, b - a - 1);
}
void print_stats(const char* label, const opt::DiffStats& s, bool is_signed) {
  if (s.count == 0) {
    std::printf("  %-26s n=0\n", label);
    return;
  }
  if (is_signed) {
    std::printf("  %-26s n=%zu median %+.4f p05 %+.4f p95 %+.4f max|.| %.4f\n", label, s.count,
                s.median * 100.0, s.p05 * 100.0, s.p95 * 100.0, s.max_abs * 100.0);
  } else {
    std::printf("  %-26s n=%zu median %.4f p95 %.4f max %.4f\n", label, s.count,
                s.median * 100.0, s.p95 * 100.0, s.max_abs * 100.0);
  }
}
void print_status(const char* label, const std::array<std::size_t, opt::kIvStatusCount>& c) {
  std::printf("  %-26s", label);
  for (std::size_t i = 0; i < c.size(); ++i) {
    if (c[i] != 0) {
      std::printf(" %s=%zu", opt::to_string(static_cast<opt::IvStatus>(i)), c[i]);
    }
  }
  std::printf("\n");
}
struct Fitted {
  std::size_t fit_slice = 0;  // index into the FitSlice vector
  opt::SviFitResult fit;
};
std::string csv_num(double v) {
  if (!std::isfinite(v)) {
    return "";
  }
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.8g", v);
  return buf;
}
double ok_vol(const opt::IvResult& r) {
  return r.status == opt::IvStatus::Ok ? r.vol : std::numeric_limits<double>::quiet_NaN();
}
void write_csv(const fs::path& file, const Snapshot& snap, const std::vector<opt::MarketIv>& ivs,
               const std::vector<opt::FitSlice>& slices, const std::vector<Fitted>& fitted) {
  std::ofstream out(file);
  if (!out) {
    throw std::runtime_error(file.string() + ": cannot open for writing");
  }
  out << "expiry,tau,strike,k,type,iv_bid,iv_ask,iv_mid,mark_iv,iv_fit,resid_volpts,used_in_fit\n";
  for (std::size_t e = 0; e < snap.expiries.size(); ++e) {
    const opt::SviFitResult* fit = nullptr;
    for (const auto& f : fitted) {
      if (slices[f.fit_slice].expiry == e) {
        fit = &f.fit;
      }
    }
    const auto& used = slices[e].iv_index;
    const std::string label = expiry_label(snap, e);
    for (const std::size_t qi : snap.expiries[e].quote_index) {
      const auto& q = snap.quotes[qi];
      const auto& m = ivs[qi];
      const double mid = ok_vol(m.mid);
      double iv_fit = std::numeric_limits<double>::quiet_NaN();
      if (fit != nullptr) {
        iv_fit = std::sqrt(std::max(opt::svi_w(fit->p, m.k), 0.0) / q.tau);
      }
      const bool in_fit = std::find(used.begin(), used.end(), qi) != used.end() &&
                          slices[e].selection == opt::SliceSelection::Selected;
      out << label << ',' << csv_num(q.tau) << ',' << csv_num(q.strike) << ',' << csv_num(m.k)
          << ',' << (q.type == opt::OptionType::Call ? 'C' : 'P') << ','
          << csv_num(ok_vol(m.bid)) << ',' << csv_num(ok_vol(m.ask)) << ',' << csv_num(mid)
          << ',' << csv_num(q.mark_iv) << ',' << csv_num(iv_fit) << ','
          << csv_num((iv_fit - mid) * 100.0) << ',' << (in_fit ? 1 : 0) << '\n';
    }
  }
  if (!out) {
    throw std::runtime_error(file.string() + ": write failed");
  }
}
// One panel per fitted expiry: market mids as dots, the SVI fit as a line.
void write_svg(const fs::path& file, const Snapshot& snap,
               const std::vector<opt::FitSlice>& slices, const std::vector<Fitted>& fitted) {
  constexpr double kW = 420.0;
  constexpr double kH = 260.0;
  constexpr double kPad = 40.0;
  constexpr int kCols = 2;
  const auto n = static_cast<int>(fitted.size());
  const int rows = (n + kCols - 1) / kCols;
  std::ofstream out(file);
  if (!out) {
    throw std::runtime_error(file.string() + ": cannot open for writing");
  }
  char buf[256];
  std::snprintf(buf, sizeof buf,
                "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" "
                "font-family=\"sans-serif\" font-size=\"11\">\n",
                kW * kCols, kH * std::max(rows, 1));
  out << buf << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
  for (int i = 0; i < n; ++i) {
    const auto& f = fitted[static_cast<std::size_t>(i)];
    const auto& s = slices[f.fit_slice];
    const double ox = kW * (i % kCols);
    const double oy = kH * (i / kCols);
    const auto [klo_it, khi_it] = std::minmax_element(s.k.begin(), s.k.end());
    const double klo = *klo_it;
    const double khi = *khi_it > klo ? *khi_it : klo + 1e-6;
    constexpr int kSteps = 100;
    std::vector<double> fit_vol(kSteps + 1);
    double vlo = *std::min_element(s.vol.begin(), s.vol.end());
    double vhi = *std::max_element(s.vol.begin(), s.vol.end());
    for (int j = 0; j <= kSteps; ++j) {
      const double k = klo + (khi - klo) * j / kSteps;
      fit_vol[static_cast<std::size_t>(j)] =
          std::sqrt(std::max(opt::svi_w(f.fit.p, k), 0.0) / s.tau);
      vlo = std::min(vlo, fit_vol[static_cast<std::size_t>(j)]);
      vhi = std::max(vhi, fit_vol[static_cast<std::size_t>(j)]);
    }
    if (vhi <= vlo) {
      vhi = vlo + 1e-6;
    }
    auto px = [&](double k) { return ox + kPad + (k - klo) / (khi - klo) * (kW - 2 * kPad); };
    auto py = [&](double v) { return oy + kH - kPad - (v - vlo) / (vhi - vlo) * (kH - 2 * kPad); };
    std::snprintf(buf, sizeof buf,
                  "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"none\" "
                  "stroke=\"#999\"/>\n<text x=\"%.1f\" y=\"%.1f\">%s  tau %.4f  RMSE %.3f vol "
                  "pts</text>\n",
                  ox + kPad, oy + kPad, kW - 2 * kPad, kH - 2 * kPad, ox + kPad, oy + kPad - 8,
                  expiry_label(snap, s.expiry).c_str(), s.tau, f.fit.rmse_vol);
    out << buf;
    std::snprintf(buf, sizeof buf,
                  "<text x=\"%.1f\" y=\"%.1f\">k %.3f .. %.3f, vol %.1f%% .. %.1f%%</text>\n",
                  ox + kPad, oy + kH - kPad + 16, klo, khi, vlo * 100.0, vhi * 100.0);
    out << buf << "<polyline fill=\"none\" stroke=\"#c33\" stroke-width=\"1.5\" points=\"";
    for (int j = 0; j <= kSteps; ++j) {
      const double k = klo + (khi - klo) * j / kSteps;
      std::snprintf(buf, sizeof buf, "%.1f,%.1f ", px(k), py(fit_vol[static_cast<std::size_t>(j)]));
      out << buf;
    }
    out << "\"/>\n";
    for (std::size_t j = 0; j < s.k.size(); ++j) {
      std::snprintf(buf, sizeof buf, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"2.5\" fill=\"#36c\"/>\n",
                    px(s.k[j]), py(s.vol[j]));
      out << buf;
    }
  }
  out << "</svg>\n";
  if (!out) {
    throw std::runtime_error(file.string() + ": write failed");
  }
}
int run(const Options& opt_in) {
  const Snapshot snap = opt::deribit::load_snapshot(opt_in.snapshot);
  if (snap.valuation_spread_ms() > 1000) {
    std::fprintf(stderr,
                 "warning: creation_timestamp spread is %lld ms (> 1 s); valuation uses the "
                 "maximum\n",
                 static_cast<long long>(snap.valuation_spread_ms()));
  }
  std::printf("snapshot: %s\n", opt_in.snapshot.string().c_str());
  std::printf("currency %s  valuation %s  index price %.2f\n", snap.currency.c_str(),
              utc_string(snap.valuation_ms).c_str(), snap.index_price);
  std::printf("quotes %zu  expiries %zu  skipped:", snap.quotes.size(), snap.expiries.size());
  if (snap.skipped.empty()) {
    std::printf(" none");
  }
  for (const auto& [reason, count] : snap.skipped) {
    std::printf(" %s=%zu", reason.c_str(), count);
  }
  std::printf("\n");
  const auto ivs = opt::compute_market_ivs(snap);
  const auto cmp = opt::compare_to_mark(snap, ivs);
  std::printf(
      "mark convention check (Black-76 on price_btc x F, r = 0, ACT/365), vol points:\n");
  print_stats("OTM |iv_mark - mark_iv|", cmp.mark_abs_otm, false);
  print_stats("ITM |iv_mark - mark_iv|", cmp.mark_abs_itm, false);
  print_stats("OTM iv_mid - mark_iv", cmp.mid_signed_otm, true);
  print_stats("ITM iv_mid - mark_iv", cmp.mid_signed_itm, true);
  print_status("mark IV status:", cmp.mark_status);
  print_status("mid IV status:", cmp.mid_status);
  opt::FitSelectOptions sel;
  sel.min_points = opt_in.min_points;
  sel.min_tau = opt_in.min_tau_days / 365.0;
  const auto slices = opt::select_fit_points(snap, ivs, sel);
  opt::SviFitOptions fopt;
  fopt.min_points = opt_in.min_points;
  std::vector<Fitted> fitted;
  std::vector<opt::SurfaceSlice> surf;
  std::vector<opt::SliceRange> ranges;
  int n_ok = 0;
  double sum_rmse = 0.0;
  double max_rmse = 0.0;
  for (std::size_t i = 0; i < slices.size(); ++i) {
    const auto& s = slices[i];
    const std::string label = expiry_label(snap, s.expiry);
    std::printf("expiry %-8s tau %.6f (%.2f d)  F %.2f  points %zu", label.c_str(), s.tau,
                s.tau * 365.0, s.forward, s.k.size());
    if (s.selection != opt::SliceSelection::Selected) {
      std::printf("  not fitted: %s\n", opt::to_string(s.selection));
      continue;
    }
    const auto fit = opt::fit_svi(s.k, s.w, s.tau, fopt);
    std::printf("  status %s\n", opt::to_string(fit.status));
    if (fit.status != opt::SviStatus::Ok && fit.status != opt::SviStatus::NotConverged) {
      continue;
    }
    if (fit.status == opt::SviStatus::Ok) {
      ++n_ok;
    }
    std::printf("  svi a %.6g b %.6g rho %.6g m %.6g s %.6g\n", fit.p.a, fit.p.b, fit.p.rho,
                fit.p.m, fit.p.s);
    std::printf("  fit rmse %.4f vol pts  max %.4f vol pts  iterations %d\n", fit.rmse_vol,
                fit.max_abs_vol_err, fit.iterations);
    sum_rmse += fit.rmse_vol;
    max_rmse = std::max(max_rmse, fit.rmse_vol);
    fitted.push_back({i, fit});
    surf.push_back({s.tau, s.forward, fit.p});
    ranges.push_back({s.k.front(), s.k.back()});
  }
  if (fitted.empty()) {
    std::printf("total: 0 expiries fitted\n");
    if (opt_in.csv) {
      write_csv(*opt_in.csv, snap, ivs, slices, fitted);
    }
    return 0;
  }
  const opt::VolSurface surface{surf};
  const auto rep = opt::check_surface(surface, ranges);
  std::printf("static arbitrage (%d-point grids, data range +- %.1f in k):\n",
              opt::kArbitrageGridPoints, opt::kArbitrageGridPad);
  for (std::size_t i = 0; i < rep.slices.size(); ++i) {
    const auto& a = rep.slices[i];
    const std::string label = expiry_label(snap, slices[fitted[i].fit_slice].expiry);
    std::printf(
        "  butterfly %-8s min g %.4g at k %.3f  violations %d/%d  Lee slopes b(1+rho) %.3f "
        "b(1-rho) %.3f %s\n",
        label.c_str(), a.butterfly.min_value, a.butterfly.k_at_min, a.butterfly.violations,
        opt::kArbitrageGridPoints, a.right_slope, a.left_slope, a.lee_ok ? "ok" : "ABOVE 2");
  }
  for (const auto& c : rep.calendar) {
    const std::string l1 = expiry_label(snap, slices[fitted[c.first].fit_slice].expiry);
    const std::string l2 = expiry_label(snap, slices[fitted[c.first + 1].fit_slice].expiry);
    std::printf("  calendar  %s -> %s  min (w2 - w1) %.4g at k %.3f  violations %d/%d\n",
                l1.c_str(), l2.c_str(), c.check.min_value, c.check.k_at_min, c.check.violations,
                opt::kArbitrageGridPoints);
  }
  std::printf(
      "total: %zu of %zu expiries fitted (Ok %d), mean rmse %.4f vol pts, max rmse %.4f vol "
      "pts; butterfly violations in %d slices, calendar violations in %d pairs, Lee "
      "violations in %d slices\n",
      fitted.size(), slices.size(), n_ok, sum_rmse / static_cast<double>(fitted.size()),
      max_rmse, rep.butterfly_slices_violating, rep.calendar_pairs_violating,
      rep.lee_slices_violating);
  if (opt_in.csv) {
    write_csv(*opt_in.csv, snap, ivs, slices, fitted);
    std::printf("wrote %s\n", opt_in.csv->string().c_str());
  }
  if (opt_in.svg) {
    write_svg(*opt_in.svg, snap, slices, fitted);
    std::printf("wrote %s\n", opt_in.svg->string().c_str());
  }
  return 0;
}
}  // namespace
int main(int argc, char** argv) {
  try {
    Options o;
    bool have_dir = false;
    opt::tools::ArgIterator it{argc, argv};
    while (!it.done()) {
      const std::string_view a = it.next();
      if (a == "--help" || a == "-h") {
        usage(stdout);
        return 0;
      }
      if (a == "--csv") {
        o.csv = fs::path{std::string{it.value(a)}};
      } else if (a == "--svg") {
        o.svg = fs::path{std::string{it.value(a)}};
      } else if (a == "--min-points") {
        o.min_points = static_cast<int>(opt::tools::parse_u64(a, it.value(a), 5, 10000));
      } else if (a == "--min-tau-days") {
        o.min_tau_days = opt::tools::parse_double(a, it.value(a), 0.0, 3650.0);
      } else if (!a.empty() && a.front() == '-') {
        throw opt::tools::UsageError("unknown flag '" + std::string{a} + "'");
      } else if (!have_dir) {
        o.snapshot = fs::path{std::string{a}};
        have_dir = true;
      } else {
        throw opt::tools::UsageError("more than one snapshot directory given");
      }
    }
    if (!have_dir) {
      throw opt::tools::UsageError("missing <snapshot_dir>");
    }
    return run(o);
  } catch (const opt::tools::UsageError& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    usage(stderr);
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
