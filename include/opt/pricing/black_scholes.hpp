#pragma once
// Black-Scholes-Merton price and analytic Greeks (reference implementation).
//
// One kernel in forward form, with omega = +1 (call) / -1 (put):
//   sd = sigma sqrt(T), d1 = (ln(F/K) + sd^2/2) / sd, d2 = d1 - sd,
//   V  = D omega (F N(omega d1) - K N(omega d2)).
// Greeks are the textbook closed forms (Hull ch. 19, Haug ch. 2) written with
// omega, so calls and puts share one branch-free formula. Puts are computed
// from N(-d) directly, never through put-call parity (parity would subtract
// nearly equal numbers in the deep tail and lose every digit).
//
// Limits are defined, not left to NaN:
//   T == 0: price = intrinsic max(omega(S-K), 0); delta = omega in the money,
//     0 out of the money, omega/2 at S == K; all other Greeks 0.
//   sigma == 0 (T > 0): price = D max(omega(F-K), 0); in the money delta =
//     omega e^{-qT}, rho = omega K T D, theta = omega (q S e^{-qT} - r K D);
//     out of the money those are 0; at F == K they are half the in-the-money
//     values. gamma = vega = vanna = volga = 0.
// Preconditions (asserted in debug builds; validate() is the runtime check):
// S, K > 0, T >= 0, sigma >= 0, everything finite.
#include "opt/pricing/types.hpp"

namespace opt {

// Black-76 on a forward: price and Greeks with respect to the forward F,
// discount factor D = e^{-rT}. delta = dV/dF, gamma = d2V/dF2,
// vega = dV/dsigma, vanna = d2V/dF dsigma, volga = d2V/dsigma2,
// theta = -dV/dT and rho = dV/dr, both holding F fixed (r = -ln(D)/T), so
// rho = -T V and theta = r V - D F phi(d1) sigma / (2 sqrt T).
// Preconditions: F, K > 0, T >= 0, D > 0, sigma >= 0, all finite.
[[nodiscard]] Greeks black76(OptionType type, double forward, double strike, double time,
                             double discount, double vol) noexcept;

// Undiscounted Black price omega (F N(omega d1) - K N(omega d2)) for
// T > 0, sigma >= 0. The implied-vol solver inverts exactly this function.
[[nodiscard]] double black_undiscounted(OptionType type, double forward, double strike,
                                        double time, double vol) noexcept;

[[nodiscard]] double bsm_price(const OptionSpec& spec) noexcept;
[[nodiscard]] Greeks bsm_greeks(const OptionSpec& spec) noexcept;

// The forward S e^{(r-q)T} and discount e^{-rT} that bsm_price uses, so other
// code (the IV solver) can reproduce its inputs bit for bit.
[[nodiscard]] double bsm_forward(const OptionSpec& spec) noexcept;
[[nodiscard]] double bsm_discount(const OptionSpec& spec) noexcept;

}  // namespace opt
