#include "opt/vol/svi_fit.hpp"
#include "opt/vol/svi.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
namespace opt {
namespace {
constexpr int kVars = 3;  // (a, c, d)
constexpr int kConstraints = 8;
constexpr int kMaxKkt = kVars + 3;
// Row j of the constraint matrix G (in (a, c, d) order) and its right-hand
// side h_j, so that the domain is G x <= h.
struct Constraint {
  std::array<double, kVars> g;
  double h;
};
std::array<Constraint, kConstraints> zeliade_constraints(double s, double a_max) noexcept {
  const double four_s = 4.0 * s;
  return {{
      {{0.0, -1.0, 0.0}, 0.0},     // c >= 0
      {{0.0, 1.0, 0.0}, four_s},   // c <= 4s
      {{0.0, -1.0, 1.0}, 0.0},     // d <= c
      {{0.0, -1.0, -1.0}, 0.0},    // -d <= c
      {{0.0, 1.0, 1.0}, four_s},   // d <= 4s - c
      {{0.0, 1.0, -1.0}, four_s},  // -d <= 4s - c
      {{-1.0, 0.0, 0.0}, 0.0},     // a >= 0
      {{1.0, 0.0, 0.0}, a_max},    // a <= max w
  }};
}
// Solves the n x n system A x = b in place by Gaussian elimination with
// partial pivoting. Returns false when a pivot is negligible (singular).
template <std::size_t N>
bool solve_dense(std::array<std::array<double, N>, N>& a, std::array<double, N>& b,
                 std::size_t n) noexcept {
  double scale = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      scale = std::max(scale, std::abs(a[i][j]));
    }
  }
  if (!(scale > 0.0) || !std::isfinite(scale)) {
    return false;
  }
  for (std::size_t col = 0; col < n; ++col) {
    std::size_t piv = col;
    for (std::size_t r = col + 1; r < n; ++r) {
      if (std::abs(a[r][col]) > std::abs(a[piv][col])) {
        piv = r;
      }
    }
    if (std::abs(a[piv][col]) <= 1e-13 * scale) {
      return false;
    }
    std::swap(a[piv], a[col]);
    std::swap(b[piv], b[col]);
    for (std::size_t r = col + 1; r < n; ++r) {
      const double f = a[r][col] / a[col][col];
      for (std::size_t j = col; j < n; ++j) {
        a[r][j] -= f * a[col][j];
      }
      b[r] -= f * b[col];
    }
  }
  for (std::size_t i = n; i-- > 0;) {
    double acc = b[i];
    for (std::size_t j = i + 1; j < n; ++j) {
      acc -= a[i][j] * b[j];
    }
    b[i] = acc / a[i][i];
  }
  return true;
}
double inner_objective(double a, double c, double d, double m, double s,
                       std::span<const double> k, std::span<const double> w,
                       std::span<const double> weight) noexcept {
  double sum = 0.0;
  for (std::size_t i = 0; i < k.size(); ++i) {
    const double y = (k[i] - m) / s;
    const double r = a + d * y + c * std::sqrt(y * y + 1.0) - w[i];
    sum += (weight.empty() ? 1.0 : weight[i]) * r * r;
  }
  return sum;
}
// Outer objective in (m, ln s): the inner optimum at the point clamped into
// the box, plus 1e6 x the squared distance outside the box.
struct Box {
  double m_lo, m_hi, ls_lo, ls_hi;
};
struct Outer {
  std::span<const double> k;
  std::span<const double> w;
  std::span<const double> weight;
  double a_max;
  Box box;
  [[nodiscard]] double operator()(double m, double ls) const noexcept {
    const double mc = std::clamp(m, box.m_lo, box.m_hi);
    const double lc = std::clamp(ls, box.ls_lo, box.ls_hi);
    const double dm = m - mc;
    const double dl = ls - lc;
    const auto in = detail::svi_inner_fit(mc, std::exp(lc), k, w, weight, a_max);
    return in.objective + 1e6 * (dm * dm + dl * dl);
  }
};
struct NmResult {
  double m = 0.0;
  double ls = 0.0;
  double f = std::numeric_limits<double>::infinity();
  int iterations = 0;
  bool converged = false;
};
// Nelder-Mead in 2-D with the standard coefficients (reflection 1,
// expansion 2, contraction 0.5, shrink 0.5).
NmResult nelder_mead(const Outer& fn, double m0, double ls0, int max_iter) noexcept {
  struct Vertex {
    double x, y, f;
  };
  std::array<Vertex, 3> v = {{{m0, ls0, 0.0}, {m0 + 0.1, ls0, 0.0}, {m0, ls0 + 0.5, 0.0}}};
  for (auto& p : v) {
    p.f = fn(p.x, p.y);
  }
  NmResult res;
  auto by_f = [](const Vertex& l, const Vertex& r) { return l.f < r.f; };
  for (int it = 0;; ++it) {
    std::sort(v.begin(), v.end(), by_f);
    const double size = std::max(std::hypot(v[1].x - v[0].x, v[1].y - v[0].y),
                                 std::hypot(v[2].x - v[0].x, v[2].y - v[0].y));
    if (v[2].f - v[0].f <= 1e-12 * v[0].f + 1e-30 || size < 1e-10) {
      res.converged = true;
      res.iterations = it;
      break;
    }
    if (it >= max_iter) {
      res.iterations = it;
      break;
    }
    const double cx = 0.5 * (v[0].x + v[1].x);
    const double cy = 0.5 * (v[0].y + v[1].y);
    const Vertex r{2.0 * cx - v[2].x, 2.0 * cy - v[2].y, 0.0};
    const double fr = fn(r.x, r.y);
    if (fr < v[0].f) {
      const Vertex e{cx + 2.0 * (r.x - cx), cy + 2.0 * (r.y - cy), 0.0};
      const double fe = fn(e.x, e.y);
      v[2] = fe < fr ? Vertex{e.x, e.y, fe} : Vertex{r.x, r.y, fr};
      continue;
    }
    if (fr < v[1].f) {
      v[2] = {r.x, r.y, fr};
      continue;
    }
    bool shrink = false;
    if (fr < v[2].f) {
      const Vertex c{cx + 0.5 * (r.x - cx), cy + 0.5 * (r.y - cy), 0.0};
      const double fc = fn(c.x, c.y);
      if (fc <= fr) {
        v[2] = {c.x, c.y, fc};
      } else {
        shrink = true;
      }
    } else {
      const Vertex c{cx + 0.5 * (v[2].x - cx), cy + 0.5 * (v[2].y - cy), 0.0};
      const double fc = fn(c.x, c.y);
      if (fc < v[2].f) {
        v[2] = {c.x, c.y, fc};
      } else {
        shrink = true;
      }
    }
    if (shrink) {
      for (std::size_t i = 1; i < v.size(); ++i) {
        v[i].x = v[0].x + 0.5 * (v[i].x - v[0].x);
        v[i].y = v[0].y + 0.5 * (v[i].y - v[0].y);
        v[i].f = fn(v[i].x, v[i].y);
      }
    }
  }
  std::sort(v.begin(), v.end(), by_f);
  res.m = v[0].x;
  res.ls = v[0].y;
  res.f = v[0].f;
  return res;
}
}  // namespace
namespace detail {
bool svi_inner_feasible(double a, double c, double d, double s, double a_max,
                        double tol) noexcept {
  if (!std::isfinite(a) || !std::isfinite(c) || !std::isfinite(d)) {
    return false;
  }
  const std::array<double, kVars> x = {a, c, d};
  for (const auto& con : zeliade_constraints(s, a_max)) {
    const double gx = con.g[0] * x[0] + con.g[1] * x[1] + con.g[2] * x[2];
    if (gx > con.h + tol) {
      return false;
    }
  }
  return true;
}
SviInner svi_inner_fit(double m, double s, std::span<const double> k, std::span<const double> w,
                       std::span<const double> weight, double a_max) noexcept {
  // Normal equations H x = g for phi_i = (1, z_i, y_i), x = (a, c, d).
  std::array<std::array<double, kVars>, kVars> h{};
  std::array<double, kVars> g{};
  for (std::size_t i = 0; i < k.size(); ++i) {
    const double y = (k[i] - m) / s;
    const std::array<double, kVars> phi = {1.0, std::sqrt(y * y + 1.0), y};
    const double wt = weight.empty() ? 1.0 : weight[i];
    for (std::size_t r = 0; r < kVars; ++r) {
      for (std::size_t c = 0; c < kVars; ++c) {
        h[r][c] += wt * phi[r] * phi[c];
      }
      g[r] += wt * w[i] * phi[r];
    }
  }
  const auto cons = zeliade_constraints(s, a_max);
  const double tol = 1e-12 * (1.0 + 4.0 * s + a_max);
  SviInner best;
  auto consider = [&](const std::array<double, kMaxKkt>& x) {
    if (!svi_inner_feasible(x[0], x[1], x[2], s, a_max, tol)) {
      return;
    }
    const double obj = inner_objective(x[0], x[1], x[2], m, s, k, w, weight);
    if (obj < best.objective) {
      best = {x[0], x[1], x[2], obj};
    }
  };
  // Solves the KKT system of the active set `act` (q constraints).
  auto solve_active = [&](const std::array<int, 3>& act, std::size_t q) {
    const std::size_t n = kVars + q;
    std::array<std::array<double, kMaxKkt>, kMaxKkt> a{};
    std::array<double, kMaxKkt> b{};
    for (std::size_t r = 0; r < kVars; ++r) {
      for (std::size_t c = 0; c < kVars; ++c) {
        a[r][c] = h[r][c];
      }
      b[r] = g[r];
    }
    for (std::size_t j = 0; j < q; ++j) {
      const auto& con = cons[static_cast<std::size_t>(act[j])];
      for (std::size_t c = 0; c < kVars; ++c) {
        a[kVars + j][c] = con.g[c];
        a[c][kVars + j] = con.g[c];
      }
      b[kVars + j] = con.h;
    }
    if (solve_dense(a, b, n)) {
      consider(b);
    }
  };
  solve_active({0, 0, 0}, 0);
  if (best.objective < std::numeric_limits<double>::infinity()) {
    return best;  // the unconstrained optimum is feasible, so it is the answer
  }
  for (int i = 0; i < kConstraints; ++i) {
    solve_active({i, 0, 0}, 1);
    for (int j = i + 1; j < kConstraints; ++j) {
      solve_active({i, j, 0}, 2);
      for (int l = j + 1; l < kConstraints; ++l) {
        solve_active({i, j, l}, 3);
      }
    }
  }
  return best;
}
SviParams svi_from_inner(const SviInner& in, double m, double s) noexcept {
  SviParams p;
  p.a = in.a;
  p.b = in.c / s;
  p.rho = in.c > 0.0 ? in.d / in.c : 0.0;
  p.m = m;
  p.s = s;
  return p;
}
}  // namespace detail
SviFitResult fit_svi(std::span<const double> k, std::span<const double> w, double tau,
                     const SviFitOptions& options) {
  if (k.size() != w.size()) {
    throw std::invalid_argument("fit_svi: k and w differ in length");
  }
  if (!(tau > 0.0) || !std::isfinite(tau)) {
    throw std::invalid_argument("fit_svi: tau must be positive and finite");
  }
  for (std::size_t i = 0; i < k.size(); ++i) {
    if (!std::isfinite(k[i]) || !(w[i] > 0.0) || !std::isfinite(w[i])) {
      throw std::invalid_argument("fit_svi: k must be finite and w positive and finite");
    }
  }
  SviFitResult res;
  res.n_points = static_cast<int>(k.size());
  if (res.n_points < std::max(options.min_points, kVars + 2)) {
    res.status = SviStatus::TooFewPoints;
    return res;
  }
  const auto [kmin_it, kmax_it] = std::minmax_element(k.begin(), k.end());
  const double k_min = *kmin_it;
  const double k_max = *kmax_it;
  const double a_max = *std::max_element(w.begin(), w.end());
  const Box box{k_min - 1.0, k_max + 1.0, std::log(1e-4), std::log(2.0)};
  std::vector<double> weight(k.size(), 1.0);
  Outer fn{k, w, weight, a_max, box};
  // Initial fit: 5 x 4 grid of starts.
  constexpr std::array<double, 4> kStartS = {0.01, 0.05, 0.2, 0.5};
  NmResult best;
  for (int i = 0; i < 5; ++i) {
    const double m0 = k_min + (k_max - k_min) * static_cast<double>(i) / 4.0;
    for (const double s0 : kStartS) {
      const NmResult r = nelder_mead(fn, m0, std::log(s0), options.max_iter);
      if (r.f < best.f) {
        best = r;
      }
    }
  }
  int iterations = best.iterations;
  auto params_of = [&](const NmResult& r) {
    const double m = std::clamp(r.m, box.m_lo, box.m_hi);
    const double s = std::exp(std::clamp(r.ls, box.ls_lo, box.ls_hi));
    return detail::svi_from_inner(detail::svi_inner_fit(m, s, k, w, weight, a_max), m, s);
  };
  SviParams p = params_of(best);
  // Huber IRLS passes.
  for (int pass = 0; pass < options.irls_passes; ++pass) {
    for (std::size_t i = 0; i < k.size(); ++i) {
      const double r = std::abs(svi_w(p, k[i]) - w[i]);
      const double delta = 0.02 * options.huber_volpts * std::sqrt(w[i] * tau);
      weight[i] = r > delta ? delta / r : 1.0;
    }
    best = nelder_mead(fn, best.m, best.ls, options.max_iter);
    iterations += best.iterations;
    p = params_of(best);
  }
  res.p = p;
  res.iterations = iterations;
  double sum_sq = 0.0;
  double max_err = 0.0;
  for (std::size_t i = 0; i < k.size(); ++i) {
    const double fit_vol = std::sqrt(std::max(svi_w(p, k[i]), 0.0) / tau);
    const double err = (fit_vol - std::sqrt(w[i] / tau)) * 100.0;
    sum_sq += err * err;
    max_err = std::max(max_err, std::abs(err));
  }
  res.rmse_vol = std::sqrt(sum_sq / static_cast<double>(k.size()));
  res.max_abs_vol_err = max_err;
  const bool finite = std::isfinite(p.a) && std::isfinite(p.b) && std::isfinite(p.rho) &&
                      std::isfinite(p.m) && std::isfinite(p.s) && std::isfinite(res.rmse_vol);
  if (!finite) {
    res.status = SviStatus::Failed;
  } else {
    res.status = best.converged ? SviStatus::Ok : SviStatus::NotConverged;
  }
  return res;
}
}  // namespace opt
