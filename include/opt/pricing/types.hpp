#pragma once
// Option description and Greeks shared by every pricer.
//
// Units, used everywhere (docs/conventions.md repeats them):
//   time T in years; rate r and carry/dividend yield q are annualised,
//   continuously compounded; forward F = S e^{(r-q)T}; discount D = e^{-rT}.
//   delta = dV/dS, gamma = d2V/dS2,
//   vega  = dV/dsigma per 1.00 of vol (divide by 100 for "per vol point"),
//   theta = dV/dt in calendar time = -dV/dT, per year (divide by 365 for
//           "per day"),
//   rho   = dV/dr per 1.00 of rate,
//   vanna = d2V/dS dsigma, volga = d2V/dsigma2.
#include <cstdint>

namespace opt {

enum class OptionType : std::uint8_t { Call = 0, Put = 1 };
enum class Exercise : std::uint8_t { European, American };

struct OptionSpec {
  OptionType type = OptionType::Call;
  double spot = 0.0;
  double strike = 0.0;
  double time = 0.0;         // years
  double rate = 0.0;         // r
  double carry_yield = 0.0;  // q
  double vol = 0.0;          // sigma
};

struct Greeks {
  double price = 0.0;
  double delta = 0.0;
  double gamma = 0.0;
  double vega = 0.0;
  double theta = 0.0;
  double rho = 0.0;
  double vanna = 0.0;
  double volga = 0.0;
};

enum class InputError : std::uint8_t {
  Ok,
  NonPositiveSpot,
  NonPositiveStrike,
  NegativeTime,
  NegativeVol,
  NonFinite,
};

// Boundary check for tools and other untrusted input: every field finite,
// S > 0, K > 0, T >= 0, sigma >= 0. Non-finite is reported first.
[[nodiscard]] InputError validate(const OptionSpec& spec) noexcept;

// +1 for a call, -1 for a put.
[[nodiscard]] constexpr double omega(OptionType type) noexcept {
  return type == OptionType::Call ? 1.0 : -1.0;
}

}  // namespace opt
