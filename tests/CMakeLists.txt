include(GoogleTest)

# Every test executable: project warnings, the committed test-data path, and
# test discovery at ctest time (PRE_TEST) so a crash in discovery does not
# break the build.
function(opteng_add_test_exe name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE GTest::gtest_main)
  target_compile_definitions(${name} PRIVATE OPTENG_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
  opteng_configure_target(${name})
  gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()

opteng_add_test_exe(
  opt_core_tests
  core/rng_test.cpp
  core/normal_test.cpp
  core/compensated_sum_test.cpp
  core/latency_histogram_test.cpp
  core/args_test.cpp
  core/version_test.cpp
  core/simd_test.cpp
  core/fast_math_test.cpp)
target_link_libraries(opt_core_tests PRIVATE opt::core opt_tool_args)

opteng_add_test_exe(
  opt_pricing_tests pricing/black_scholes_test.cpp pricing/implied_vol_test.cpp pricing/binomial_test.cpp
  pricing/monte_carlo_test.cpp)
target_link_libraries(opt_pricing_tests PRIVATE opt::pricing)

opteng_add_test_exe(opt_batch_tests batch/engine_test.cpp batch/thread_pool_test.cpp)
target_link_libraries(opt_batch_tests PRIVATE opt::batch)

# Replaces the global operator new/delete to count allocations, so it is a
# separate executable: the replacement must not leak into the other tests.
opteng_add_test_exe(opt_alloc_tests alloc_test.cpp)
target_link_libraries(opt_alloc_tests PRIVATE opt::batch)
target_include_directories(opt_alloc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME opt_info.runs COMMAND opt_info)
set_tests_properties(
  opt_info.runs
  PROPERTIES PASS_REGULAR_EXPRESSION
             "options-engine ${PROJECT_VERSION}\nsimd backend: (neon|avx2\\+fma|scalar) \\(width [124]\\)")
