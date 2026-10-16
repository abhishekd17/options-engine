#pragma once
// Cox-Ross-Rubinstein binomial tree for European and American options.
//
// dt = T/N, u = e^{sigma sqrt dt}, d = 1/u, p = (e^{(r-q) dt} - d) / (u - d),
// one-step discount e^{-r dt}. p outside (0, 1) (roughly when
// |r - q| sqrt(dt) > sigma) returns ProbabilityOutOfRange.
//
// One backward pass over a single array: O(N) memory, O(N^2) time.
// Node spots S e^{x} with x = ln S + (2j - i) sigma sqrt dt are clamped to
// x in [-700, 700], so every spot is a normal, finite double (no underflow to
// 0 or overflow to inf, however large sigma sqrt(T N) is). Terminal spots are
// computed one exp per node. For American exercise, each level's unclamped
// nodes start from one exact exp and walk up by repeated multiplication by
// u^2: their rounding error grows linearly in N (about N ulp at the top
// node), far below the tree's own O(1/N) discretisation error.
// The clamp is exact for puts. For a call it would lose the weight of nodes
// above e^{700}, so a call whose clamped nodes could carry weight (share-
// measure mean of ln(S_T / S) plus 8 sd above 700 - ln S, which needs
// sigma sqrt(T) above about 30, a 3000% one-year vol) returns VolTooHigh
// instead of a wrong price. Crr.LargeSigmaSqrtTN* cover both sides.
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
#include <limits>
#include <span>

namespace opt {

enum class TreeStatus : std::uint8_t { Ok, InvalidInput, ProbabilityOutOfRange, VolTooHigh };

struct TreeResult {
  double price = std::numeric_limits<double>::quiet_NaN();  // NaN unless status == Ok
  TreeStatus status = TreeStatus::InvalidInput;
};

inline constexpr int kMaxTreeSteps = 1'000'000;

// Uses `workspace` (at least steps + 1 doubles) as scratch: no allocation.
[[nodiscard]] TreeResult crr_price(const OptionSpec& spec, Exercise exercise, int steps,
                                   std::span<double> workspace);

// Allocates steps + 1 doubles.
[[nodiscard]] TreeResult crr_price(const OptionSpec& spec, Exercise exercise, int steps);

}  // namespace opt
