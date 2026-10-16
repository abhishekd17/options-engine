#pragma once
// Neumaier's improved Kahan summation: the running sum plus a compensation
// term that collects the low-order bits each addition rounds away. The error
// of the result is about 1 ulp of the sum, independent of the number of terms
// (plain summation grows like n ulp). Used for PnL buckets and MC sums.
#include <cmath>

namespace opt {

class NeumaierSum {
 public:
  constexpr NeumaierSum() noexcept = default;
  explicit constexpr NeumaierSum(double initial) noexcept : sum_(initial) {}

  void add(double x) noexcept {
    const double t = sum_ + x;
    if (std::abs(sum_) >= std::abs(x)) {
      comp_ += (sum_ - t) + x;  // low bits of x were lost
    } else {
      comp_ += (x - t) + sum_;  // low bits of sum_ were lost
    }
    sum_ = t;
  }

  [[nodiscard]] double value() const noexcept { return sum_ + comp_; }

 private:
  double sum_ = 0.0;
  double comp_ = 0.0;
};

}  // namespace opt
