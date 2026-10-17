// Measures the error of the fast_math.hpp functions against libm and the
// reference normal functions (design section 4). Reports, asserts nothing.
//
// For each function and domain: 10,000,000 seeded random points plus edge
// points (range ends, reduction and branch boundaries). Prints the maximum
// absolute error, the maximum relative error and the argument where each
// occurs. The reference itself has about 1 ulp of error, so these are errors
// "vs libm", not vs the exact value.
//
// Usage: fast_math_error [--points N] [--seed S]
#include "opt/core/fast_math.hpp"
#include "opt/core/normal.hpp"
#include "opt/core/rng.hpp"
#include "opt/core/version.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "args.hpp"

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

struct Stats {
  double max_abs = 0.0;
  double arg_abs = 0.0;
  double max_rel = 0.0;
  double arg_rel = 0.0;
  std::uint64_t points = 0;
  void add(double x, double got, double ref) {
    const double abs_err = std::fabs(got - ref);
    const double rel_err = ref != 0.0 ? abs_err / std::fabs(ref) : (got == 0.0 ? 0.0 : kInf);
    if (abs_err > max_abs) {
      max_abs = abs_err;
      arg_abs = x;
    }
    if (rel_err > max_rel) {
      max_rel = rel_err;
      arg_rel = x;
    }
    ++points;
  }
};

void print(std::string_view name, std::string_view domain, std::string_view reference,
           const Stats& s) {
  std::printf("%-14s %-26s vs %-9s points %9llu  max abs %.3e at x = %-24.17g max rel %.3e at x = %.17g\n",
              std::string(name).c_str(), std::string(domain).c_str(),
              std::string(reference).c_str(), static_cast<unsigned long long>(s.points),
              s.max_abs, s.arg_abs, s.max_rel, s.arg_rel);
}

using Draw = std::function<double(opt::Xoshiro256pp&)>;
using Fn = double (*)(double);

Stats measure(std::uint64_t points, std::uint64_t seed, const Draw& draw,
              const std::vector<double>& edges, Fn fast, Fn ref) {
  Stats s;
  for (const double x : edges) {
    s.add(x, fast(x), ref(x));
  }
  opt::Xoshiro256pp rng(seed);
  for (std::uint64_t i = 0; i < points; ++i) {
    const double x = draw(rng);
    s.add(x, fast(x), ref(x));
  }
  return s;
}

double fast_exp_d(double x) {
  return opt::fast_exp(x);
}
double std_exp(double x) {
  return std::exp(x);
}
double fast_log_d(double x) {
  return opt::fast_log(x);
}
double std_log(double x) {
  return std::log(x);
}
double fast_cdf_d(double x) {
  return opt::fast_norm_cdf(x);
}
double ref_cdf(double x) {
  return opt::norm_cdf(x);
}
double fast_pdf_d(double x) {
  return opt::fast_norm_pdf(x);
}
double ref_pdf(double x) {
  return opt::norm_pdf(x);
}
double fast_inv(double p) {
  return opt::fast_norm_inv(p);
}
double ref_inv(double p) {
  return opt::norm_inv_cdf(p);
}

void usage(std::ostream& os) {
  os << "usage: fast_math_error [--points N] [--seed S]\n"
        "  --points N  random points per function and domain (default 10000000)\n"
        "  --seed S    RNG seed (default 1)\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::uint64_t points = 10'000'000;
    std::uint64_t seed = 1;
    opt::tools::ArgIterator args(argc, argv);
    while (!args.done()) {
      const std::string_view flag = args.next();
      if (flag == "--help" || flag == "-h") {
        usage(std::cout);
        return 0;
      }
      if (flag == "--points") {
        points = opt::tools::parse_u64(flag, args.value(flag), 1, 1'000'000'000);
      } else if (flag == "--seed") {
        seed = opt::tools::parse_u64(flag, args.value(flag), 0,
                                     std::numeric_limits<std::uint64_t>::max());
      } else {
        throw opt::tools::UsageError("unknown flag: " + std::string(flag));
      }
    }

    std::printf("fast_math_error: %llu random points per row (seed %llu) plus edge points; "
                "simd backend %s; errors vs libm / reference normal\n",
                static_cast<unsigned long long>(points), static_cast<unsigned long long>(seed),
                std::string(opt::simd_backend_name()).c_str());

    // fast_exp on [-708, 709]: normal results only.
    std::vector<double> exp_edges = {-708.0, 709.0, 0.0, -0.0, 1e-300, -1e-300, 1.0, -1.0};
    for (int k = -1021; k <= 1022; ++k) {
      const double b = (k + 0.5) * std::numbers::ln2;
      if (b > -708.0 && b < 709.0) {
        exp_edges.push_back(std::nextafter(b, -kInf));
        exp_edges.push_back(std::nextafter(b, kInf));
      }
    }
    print("fast_exp", "uniform [-708, 709]", "std::exp",
          measure(points, seed, [](auto& r) { return opt::uniform(r, -708.0, 709.0); }, exp_edges,
                  fast_exp_d, std_exp));

    // fast_log on log-uniform [1e-300, 1e300].
    const double llo = std::log(1e-300);
    const double lhi = std::log(1e300);
    const std::vector<double> log_edges = {
        1e-300, 1e300, 1.0, std::nextafter(1.0, 2.0), std::nextafter(1.0, 0.0), 0.5, 2.0,
        std::numbers::sqrt2, std::nextafter(std::numbers::sqrt2, 0.0),
        std::numeric_limits<double>::min(), std::numeric_limits<double>::max()};
    print("fast_log", "log-uniform [1e-300, 1e300]", "std::log",
          measure(points, seed + 1, [=](auto& r) { return std::exp(opt::uniform(r, llo, lhi)); },
                  log_edges, fast_log_d, std_log));

    // fast_norm_cdf on [-38, 9], then split by region so the table shows
    // where the error lives.
    const double sw = 7.07106781186547;
    const std::vector<double> cdf_edges = {-38.0, 9.0,  -37.0, 37.0, 0.0,
                                           -sw,   std::nextafter(-sw, 0.0), sw,
                                           std::nextafter(sw, 0.0), -1e-300, 1e-300};
    print("fast_norm_cdf", "uniform [-38, 9]", "norm_cdf",
          measure(points, seed + 2, [](auto& r) { return opt::uniform(r, -38.0, 9.0); },
                  cdf_edges, fast_cdf_d, ref_cdf));
    print("fast_norm_cdf", "uniform [-37, -7.0710678]", "norm_cdf",
          measure(points, seed + 3, [=](auto& r) { return opt::uniform(r, -37.0, -sw); },
                  {-37.0, -sw}, fast_cdf_d, ref_cdf));
    print("fast_norm_cdf", "uniform [-7.0710678, 0]", "norm_cdf",
          measure(points, seed + 4, [=](auto& r) { return opt::uniform(r, -sw, 0.0); },
                  {std::nextafter(-sw, 0.0), 0.0}, fast_cdf_d, ref_cdf));
    print("fast_norm_cdf", "uniform [0, 9]", "norm_cdf",
          measure(points, seed + 5, [](auto& r) { return opt::uniform(r, 0.0, 9.0); },
                  {0.0, 9.0, sw}, fast_cdf_d, ref_cdf));
    print("fast_norm_cdf", "uniform [-38, -37]", "norm_cdf",
          measure(points, seed + 6, [](auto& r) { return opt::uniform(r, -38.0, -37.0); },
                  {-38.0, -37.0, std::nextafter(-37.0, -38.0)}, fast_cdf_d, ref_cdf));

    print("fast_norm_pdf", "uniform [-38, 38]", "norm_pdf",
          measure(points, seed + 7, [](auto& r) { return opt::uniform(r, -38.0, 38.0); },
                  {-38.0, 38.0, 0.0}, fast_pdf_d, ref_pdf));

    // fast_norm_inv on (1e-300, 1 - 1e-16): half the points log-uniform in the
    // lower half, half mirrored into the upper half.
    const double plo = std::log(1e-300);
    const double phi = std::log(0.5);
    print("fast_norm_inv", "(1e-300, 1 - 1e-16)", "AS241",
          measure(points, seed + 8,
                  [=](auto& r) {
                    const bool upper = (r() & 1U) != 0;
                    if (upper) {
                      return 1.0 - std::exp(opt::uniform(r, std::log(1e-16), phi));
                    }
                    return std::exp(opt::uniform(r, plo, phi));
                  },
                  {1e-300, 0.5, 1.0 - 1e-16, 0.025, 0.975}, fast_inv, ref_inv));
    return 0;
  } catch (const opt::tools::UsageError& e) {
    std::cerr << "error: " << e.what() << '\n';
    usage(std::cerr);
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
