#pragma once
// Cox-Ross-Rubinstein binomial tree for European and American options.
//
// dt = T/N, u = e^{sigma sqrt dt}, d = 1/u, p = (e^{(r-q) dt} - d) / (u - d),
// one-step discount e^{-r dt}. p outside (0, 1) (roughly when
// |r - q| sqrt(dt) > sigma) returns ProbabilityOutOfRange.
//
// One backward pass over a single array: O(N) memory, O(N^2) time. Node spots
// S d^i u^{2j} are built per level from S e^{-i sigma sqrt dt} by repeated
// multiplication by u^2, so their rounding error grows linearly in N (about N
// ulp at the top node), far below the tree's own O(1/N) discretisation error.
// American: every node takes max(continuation, intrinsic).
//
// Limits: T == 0 gives intrinsic. sigma == 0 (T > 0) is deterministic: the
// European value is the section 5.2 limit, and the American value is the
// best exercise time on the deterministic path, the maximum of
// e^{-rt} max(omega (S e^{(r-q)t} - K), 0) over t in {0, T, t*}, where t*
// solves r K e^{-rt} = q S e^{-qt} (the only interior stationary point).
//
// Errors: steps outside [1, 1,000,000] or a workspace shorter than steps + 1
// throw std::invalid_argument (programming errors). Invalid option data
// (validate() != Ok) returns InvalidInput.
#include "opt/pricing/types.hpp"

#include <cstdint>
#include <span>

namespace opt {

enum class TreeStatus : std::uint8_t { Ok, InvalidInput, ProbabilityOutOfRange };

struct TreeResult {
  double price;  // NaN unless status == Ok
  TreeStatus status;
};

inline constexpr int kMaxTreeSteps = 1'000'000;

// Uses `workspace` (at least steps + 1 doubles) as scratch: no allocation.
[[nodiscard]] TreeResult crr_price(const OptionSpec& spec, Exercise exercise, int steps,
                                   std::span<double> workspace);

// Allocates steps + 1 doubles.
[[nodiscard]] TreeResult crr_price(const OptionSpec& spec, Exercise exercise, int steps);

}  // namespace opt
