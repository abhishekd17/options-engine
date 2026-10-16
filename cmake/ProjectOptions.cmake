# Compiler flags for the project (pattern copied from itch-pipeline).
#
# - Sanitizer flags are applied globally (before dependencies are fetched) so
#   that GoogleTest/Benchmark are instrumented too. TSan in particular reports
#   false positives when only part of the program is instrumented.
# - Warnings, -Werror, native tuning and LTO apply to project targets only,
#   through the `opteng_project_options` interface library and the
#   `opteng_configure_target()` helper.
include(CheckCXXCompilerFlag)
include(CheckIPOSupported)
# ---------------------------------------------------------------------------
# Sanitizers (global)
# ---------------------------------------------------------------------------
if(OPTENG_SANITIZER STREQUAL "asan")
  set(_opteng_san_flags -fsanitize=address,undefined -fno-sanitize-recover=all
                        -fno-omit-frame-pointer)
elseif(OPTENG_SANITIZER STREQUAL "tsan")
  set(_opteng_san_flags -fsanitize=thread -fno-omit-frame-pointer)
elseif(NOT OPTENG_SANITIZER STREQUAL "none")
  message(FATAL_ERROR "OPTENG_SANITIZER must be one of: none, asan, tsan (got '${OPTENG_SANITIZER}')")
endif()
if(DEFINED _opteng_san_flags)
  add_compile_options(${_opteng_san_flags})
  add_link_options(${_opteng_san_flags})
  message(STATUS "opteng: sanitizer = ${OPTENG_SANITIZER}")
endif()
# ---------------------------------------------------------------------------
# Project-only options
# ---------------------------------------------------------------------------
add_library(opteng_project_options INTERFACE)
add_library(opt::project_options ALIAS opteng_project_options)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(
    opteng_project_options
    INTERFACE -Wall
              -Wextra
              -Wpedantic
              -Wconversion
              -Wsign-conversion
              -Wshadow
              -Wnon-virtual-dtor
              -Wold-style-cast
              -Wcast-align
              -Woverloaded-virtual
              -Wnull-dereference
              -Wdouble-promotion
              -Wimplicit-fallthrough
              -Werror
              # The compiler must not fuse a*b+c on its own: kernels call fma
              # explicitly where intended, so results match across GCC, Clang
              # and Apple clang, and scalar and SIMD paths run the same ops.
              -ffp-contract=off
              # Lets std::sqrt compile to one instruction on GCC. No code
              # reads errno after a math call, so no result changes.
              -fno-math-errno)
elseif(MSVC)
  target_compile_options(opteng_project_options INTERFACE /W4 /permissive- /WX)
endif()
# Host-CPU tuning. ARM (Apple Silicon, aarch64 Linux) uses -mcpu=native;
# x86-64 uses -march=native. The flag is probed so an unsupported compiler
# fails loudly at configure time instead of silently.
if(OPTENG_NATIVE_ARCH)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(_opteng_native_flag -mcpu=native)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
    set(_opteng_native_flag -march=native)
  endif()
  if(DEFINED _opteng_native_flag)
    check_cxx_compiler_flag(${_opteng_native_flag} OPTENG_HAS_NATIVE_FLAG)
    if(OPTENG_HAS_NATIVE_FLAG)
      target_compile_options(opteng_project_options INTERFACE ${_opteng_native_flag})
      message(STATUS "opteng: native tuning = ${_opteng_native_flag} (${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_CXX_COMPILER_ID})")
    else()
      message(FATAL_ERROR "opteng: ${CMAKE_CXX_COMPILER_ID} does not accept ${_opteng_native_flag}")
    endif()
  else()
    message(WARNING "opteng: no native tuning flag known for ${CMAKE_SYSTEM_PROCESSOR}; skipping")
  endif()
endif()
if(OPTENG_ENABLE_LTO)
  check_ipo_supported(RESULT _opteng_ipo_ok OUTPUT _opteng_ipo_msg LANGUAGES CXX)
  if(NOT _opteng_ipo_ok)
    message(FATAL_ERROR "opteng: LTO requested but not supported: ${_opteng_ipo_msg}")
  endif()
  message(STATUS "opteng: LTO enabled for project targets")
endif()
# Apply project defaults (warnings, -Werror, native tuning, optional LTO) to a
# project target. Never call this on fetched dependency targets.
function(opteng_configure_target target)
  target_link_libraries(${target} PRIVATE opt::project_options)
  if(OPTENG_ENABLE_LTO)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endfunction()
