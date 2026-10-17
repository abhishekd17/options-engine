#include "opt/core/version.hpp"

#include "opt/simd/simd.hpp"

#include <cstddef>
#include <string_view>

namespace opt {

std::string_view version() noexcept {
  return OPTENG_VERSION_STRING;
}

std::string_view simd_backend_name() noexcept {
  return simd::kNativeName;
}

std::size_t simd_width() noexcept {
  return simd::kNativeWidth;
}

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
