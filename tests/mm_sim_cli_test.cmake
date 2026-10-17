# cli.mm_sim: runs mm_sim in synthetic and recorded mode.
# Inputs: -DMM_SIM=<exe> -DSERIES=<tests/data/deribit/series> -DWORK=<scratch dir>
include(${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake)

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# Golden run (same values as tests/mm/golden_test.cpp): seed 42, 2,000 steps.
run_expect(
  0
  COMMAND "${MM_SIM}" --mode synthetic --seed 42 --steps 2000 --no-latency
  MATCHES "instruments 10:"
          "fills bid 52 ask 48  hedges 6  limit-blocked sides 0  limit-rejected fills 0"
          "fill hash 0xc351c1a7d762bfe0"
          "final positions: 4 1 -2 -2 4 -2 -4 2 2 1"
          "spread capture +\\+5168\\.3927"
          "total +\\+4688\\.76849"
          "ledger total +\\+4688\\.76849"
          "difference +[0-9.e+-]+  \\(tolerance [0-9.e+-]+: ok\\)"
  OUTPUT first)
if(first MATCHES "latency per quote update")
  message(FATAL_ERROR "--no-latency output contains the latency section")
endif()
# Everything except the latency section is deterministic.
run_expect(0 COMMAND "${MM_SIM}" --mode synthetic --seed 42 --steps 2000 --no-latency OUTPUT second)
if(NOT first STREQUAL second)
  message(FATAL_ERROR "two runs with the same seed printed different output")
endif()

run_expect(
  0
  COMMAND "${MM_SIM}" --steps 300 --events "${WORK}/events.csv"
  MATCHES "events written to" "latency per quote update" "p50 [0-9]+  p90 [0-9]+  p99 [0-9]+  p99.9 [0-9]+  max [0-9]+"
          "per option: p50 ")
file(STRINGS "${WORK}/events.csv" csv_lines)
list(GET csv_lines 0 header)
set(expected "step,t,kind,instrument,side,qty,price,fair,spread_capture,cost")
if(NOT header STREQUAL expected)
  message(FATAL_ERROR "events CSV header is '${header}', expected '${expected}'")
endif()

run_expect(
  0
  COMMAND "${MM_SIM}" --mode recorded --series "${SERIES}" --no-latency
  MATCHES "snapshots 6" "BTC-[0-9]+[A-Z]+[0-9]+-[0-9]+-[PC]" "states 6  fills bid [0-9]+"
          "steps without surface 0" "\\(tolerance [0-9.e+-]+: ok\\)")

run_expect(2 COMMAND "${MM_SIM}" --bogus MATCHES "unknown flag '--bogus'" "usage:")
run_expect(2 COMMAND "${MM_SIM}" --mode sideways MATCHES "--mode")
run_expect(2 COMMAND "${MM_SIM}" --mode recorded MATCHES "needs --series")
run_expect(2 COMMAND "${MM_SIM}" --steps 700000 MATCHES "horizon .* must be shorter than the first expiry")
run_expect(1 COMMAND "${MM_SIM}" --mode recorded --series "${WORK}/no-such-series" MATCHES "error: ")
