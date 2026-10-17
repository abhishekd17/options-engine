#pragma once
// Market state handed to the simulator each step, and the MarketSource
// contract (design 8.2).
#include "opt/vol/surface.hpp"
#include <concepts>
#include <cstdint>
#include <span>

namespace opt::mm {

struct MarketState {
  double t_years = 0.0;  // time since the start of the simulation
  double spot = 0.0;     // hedge instrument price (index), USD
  const VolSurface* surface = nullptr;  // forwards come from the surface
  // false when the source could not build a fresh surface for this step; the
  // surface pointer then holds the last good one, used for marking only.
  bool surface_ok = true;
  // false when the source's in-run static-arbitrage guard failed at this step.
  bool arbitrage_ok = true;
  // Per instrument (in the simulator's order): 1 = listed in this state. An
  // empty span means every instrument is listed. Unlisted instruments keep
  // their last mark and are not quoted.
  std::span<const std::uint8_t> listed;
};

// A source yields one state per call and returns false at the end of data.
template <class S>
concept MarketSource = requires(S s, MarketState& m) {
  { s.next(m) } -> std::same_as<bool>;
};

}  // namespace opt::mm
