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
opteng_add_test_exe(opt_vol_tests vol/svi_test.cpp vol/svi_fit_test.cpp vol/arbitrage_test.cpp
                    vol/surface_test.cpp vol/snapshot_fit_test.cpp)
# snapshot_fit_test reads the committed Deribit snapshot, so it needs the loader.
target_link_libraries(opt_vol_tests PRIVATE opt::deribit)
opteng_add_test_exe(opt_deribit_tests deribit/snapshot_test.cpp)
target_link_libraries(opt_deribit_tests PRIVATE opt::deribit)

opteng_add_test_exe(
  opt_mm_tests
  mm/golden_test.cpp
  mm/quoter_test.cpp
  mm/limits_test.cpp
  mm/hedger_test.cpp
  mm/pnl_test.cpp
  mm/fill_model_test.cpp
  mm/recorded_test.cpp
  mm/synthetic_test.cpp)
target_link_libraries(opt_mm_tests PRIVATE opt::mm_recorded)
target_include_directories(opt_mm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Replaces the global operator new/delete to count allocations, so it is a
# separate executable: the replacement must not leak into the other tests.
opteng_add_test_exe(opt_alloc_tests alloc_test.cpp)
# opt::mm links opt::batch publicly; naming both makes Apple ld warn about a
# duplicate library.
target_link_libraries(opt_alloc_tests PRIVATE opt::mm)
target_include_directories(opt_alloc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME opt_info.runs COMMAND opt_info)
set_tests_properties(
  opt_info.runs
  PROPERTIES PASS_REGULAR_EXPRESSION
             "options-engine ${PROJECT_VERSION}\nsimd backend: (neon|avx2\\+fma|scalar) \\(width [124]\\)")
add_test(
  NAME cli.vol_surface
  COMMAND
    ${CMAKE_COMMAND} -DVOL_SURFACE=$<TARGET_FILE:vol_surface>
    -DDATA=${PROJECT_SOURCE_DIR}/tests/data/deribit -DWORK=${CMAKE_CURRENT_BINARY_DIR}/vol_surface_work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/vol_surface_cli_test.cmake)

add_test(
  NAME cli.mm_sim
  COMMAND
    ${CMAKE_COMMAND} -DMM_SIM=$<TARGET_FILE:mm_sim> -DSERIES=${PROJECT_SOURCE_DIR}/tests/data/deribit/series
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/mm_sim_work -P ${CMAKE_CURRENT_SOURCE_DIR}/mm_sim_cli_test.cmake)
