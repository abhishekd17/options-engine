#include "opt/core/version.hpp"

#include <cstddef>
#include <string_view>

namespace opt {

std::string_view version() noexcept {
  return OPTENG_VERSION_STRING;
}

// Part 1 reports the backend from the same macros simd.hpp will use
// (Part 2 switches this to opt::simd::kNativeWidth).
#if defined(__AVX2__) && defined(__FMA__)
std::string_view simd_backend_name() noexcept {
  return "avx2+fma";
}
std::size_t simd_width() noexcept {
  return 4;
}
#elif defined(__aarch64__)
std::string_view simd_backend_name() noexcept {
  return "neon";
}
std::size_t simd_width() noexcept {
  return 2;
}
#else
std::string_view simd_backend_name() noexcept {
  return "scalar";
}
std::size_t simd_width() noexcept {
  return 1;
}
#endif

#define OPTENG_STR2(x) #x
#define OPTENG_STR(x) OPTENG_STR2(x)

std::string_view compiler_id() noexcept {
#if defined(__clang__)
#if defined(__apple_build_version__)
  return "apple clang " OPTENG_STR(__clang_major__) "." OPTENG_STR(__clang_minor__) "." OPTENG_STR(
      __clang_patchlevel__);
#else
  return "clang " OPTENG_STR(__clang_major__) "." OPTENG_STR(__clang_minor__) "." OPTENG_STR(
      __clang_patchlevel__);
#endif
#elif defined(__GNUC__)
  return "gcc " OPTENG_STR(__GNUC__) "." OPTENG_STR(__GNUC_MINOR__) "." OPTENG_STR(
      __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  return "msvc " OPTENG_STR(_MSC_VER);
#else
  return "unknown";
#endif
}

#undef OPTENG_STR
#undef OPTENG_STR2

}  // namespace opt
