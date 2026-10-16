#pragma once
// Reference standard normal functions (use libm).
//
// norm_cdf uses erfc so the lower tail keeps full relative accuracy:
// N(x) = erfc(-x / sqrt 2) / 2 is never computed as 1 - N(-x).
//
// norm_inv_cdf is Wichura's algorithm AS241 (PPND16): M. J. Wichura, "The
// Percentage Points of the Normal Distribution", Applied Statistics 37(3),
// 477-484, 1988. Relative accuracy about 1e-16. The body is a template on the
// log function so the Part 2 fast path can reuse it with fast_log.
#include <cmath>
#include <numbers>

namespace opt {

inline constexpr double kInvSqrt2Pi = 0.398942280401432677939946059934;  // 1/sqrt(2 pi)

[[nodiscard]] inline double norm_pdf(double x) noexcept {
  return kInvSqrt2Pi * std::exp(-0.5 * x * x);
}

[[nodiscard]] inline double norm_cdf(double x) noexcept {
  return 0.5 * std::erfc(-x / std::numbers::sqrt2);
}

namespace detail {

// AS241 PPND16. Precondition: 0 < p < 1 (p == 0 or 1 gives -inf / +inf via
// log(0); NaN propagates).
template <class LogFn>
[[nodiscard]] inline double as241(double p, LogFn log_fn) noexcept {
  const double q = p - 0.5;
  if (std::abs(q) <= 0.425) {
    const double r = 0.180625 - q * q;
    return q *
           (((((((r * 2509.0809287301226727 + 33430.575583588128105) * r +
                 67265.770927008700853) *
                    r +
                45921.953931549871457) *
                   r +
               13731.693765509461125) *
                  r +
              1971.5909503065514427) *
                 r +
             133.14166789178437745) *
                r +
            3.387132872796366608) /
           (((((((r * 5226.495278852854561 + 28729.085735721942674) * r + 39307.89580009271061) *
                    r +
                21213.794301586595867) *
                   r +
               5394.1960214247511077) *
                  r +
              687.1870074920579083) *
                 r +
             42.313330701600911252) *
                r +
            1.0);
  }
  double r = q < 0.0 ? p : 1.0 - p;
  r = std::sqrt(-log_fn(r));
  double val = 0.0;
  if (r <= 5.0) {
    r -= 1.6;
    val = (((((((r * 7.7454501427834140764e-4 + 0.0227238449892691845833) * r +
                0.24178072517745061177) *
                   r +
               1.27045825245236838258) *
                  r +
              3.64784832476320460504) *
                 r +
             5.7694972214606914055) *
                r +
            4.6303378461565452959) *
               r +
           1.42343711074968357734) /
          (((((((r * 1.05075007164441684324e-9 + 5.475938084995344946e-4) * r +
                0.0151986665636164571966) *
                   r +
               0.14810397642748007459) *
                  r +
              0.68976733498510000455) *
                 r +
             1.6763848301838038494) *
                r +
            2.05319162663775882187) *
               r +
           1.0);
  } else {
    r -= 5.0;
    val = (((((((r * 2.01033439929228813265e-7 + 2.71155556874348757815e-5) * r +
                0.0012426609473880784386) *
                   r +
               0.026532189526576123093) *
                  r +
              0.29656057182850489123) *
                 r +
             1.7848265399172913358) *
                r +
            5.4637849111641143699) *
               r +
           6.6579046435011037772) /
          (((((((r * 2.04426310338993978564e-15 + 1.4215117583164458887e-7) * r +
                1.8463183175100546818e-5) *
                   r +
               7.868691311456132591e-4) *
                  r +
              0.0148753612908506148525) *
                 r +
             0.13692988092273580531) *
                r +
            0.59983220655588793769) *
               r +
           1.0);
  }
  return q < 0.0 ? -val : val;
}

}  // namespace detail

// Inverse standard normal CDF, Phi^{-1}(p), for 0 < p < 1.
[[nodiscard]] inline double norm_inv_cdf(double p) noexcept {
  return detail::as241(p, [](double v) noexcept { return std::log(v); });
}

}  // namespace opt
