#pragma once
#include <cstddef>
#include <string_view>

namespace opt {

// Library version, "MAJOR.MINOR.PATCH" (the CMake project version).
[[nodiscard]] std::string_view version() noexcept;

// SIMD backend compiled into this build: "neon", "avx2+fma" or "scalar".
// Selected by the same predefined macros the opt::simd layer uses, so the
// answer follows the compile flags (e.g. -march=native), not the host CPU.
[[nodiscard]] std::string_view simd_backend_name() noexcept;

// Number of doubles per SIMD vector for that backend (1 for scalar).
[[nodiscard]] std::size_t simd_width() noexcept;

// Compiler name and version, e.g. "clang 21.0.0" or "gcc 13.2.0".
[[nodiscard]] std::string_view compiler_id() noexcept;

}  // namespace opt
