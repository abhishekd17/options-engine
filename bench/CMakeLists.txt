add_executable(iv_bench iv_bench.cpp)
target_link_libraries(iv_bench PRIVATE opt::pricing benchmark::benchmark_main)
opteng_configure_target(iv_bench)

add_executable(tree_mc_bench tree_mc_bench.cpp)
target_link_libraries(tree_mc_bench PRIVATE opt::pricing benchmark::benchmark_main)
opteng_configure_target(tree_mc_bench)

# bench.smoke: one iteration of every benchmark, so the bench code is compiled
# and run (also under ASan) by ctest.
if(OPTENG_BUILD_TESTS)
  foreach(bench_exe IN ITEMS iv_bench tree_mc_bench)
    add_test(NAME bench.smoke.${bench_exe} COMMAND ${bench_exe} --benchmark_min_time=1x)
  endforeach()
endif()
