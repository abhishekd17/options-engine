# cli.vol_surface: runs vol_surface on the committed Deribit snapshot.
# Inputs: -DVOL_SURFACE=<exe> -DDATA=<tests/data/deribit> -DWORK=<scratch dir>
include(${CMAKE_CURRENT_LIST_DIR}/cli_common.cmake)
file(GLOB snapshots LIST_DIRECTORIES true "${DATA}/BTC-*")
list(LENGTH snapshots n)
if(NOT n EQUAL 1)
  message(FATAL_ERROR "expected exactly one BTC-* snapshot in ${DATA}, found ${n}")
endif()
list(GET snapshots 0 snap)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(num "[-+]?[0-9.]+(e[-+]?[0-9]+)?")
run_expect(
  0
  COMMAND "${VOL_SURFACE}" "${snap}" --csv "${WORK}/fit.csv" --svg "${WORK}/fit.svg"
  MATCHES "currency BTC  valuation [0-9-]+T[0-9:.]+Z  index price ${num}"
          "OTM \\|iv_mark - mark_iv\\| +n=[0-9]+ median ${num}"
          "expiry [0-9]+[A-Z]+[0-9]+ +tau ${num} .*not fitted: TooShort"
          "expiry [0-9]+[A-Z]+[0-9]+ +tau ${num} .*status Ok"
          "fit rmse ${num} vol pts  max ${num} vol pts"
          "butterfly [0-9]+[A-Z]+[0-9]+ +min g ${num} at k ${num}  violations [0-9]+/401"
          "calendar  [0-9]+[A-Z]+[0-9]+ -> [0-9]+[A-Z]+[0-9]+  min \\(w2 - w1\\) ${num}"
          "total: [0-9]+ of [0-9]+ expiries fitted \\(Ok [0-9]+\\)"
  OUTPUT out)
file(STRINGS "${WORK}/fit.csv" csv_lines)
list(GET csv_lines 0 header)
set(expected "expiry,tau,strike,k,type,iv_bid,iv_ask,iv_mid,mark_iv,iv_fit,resid_volpts,used_in_fit")
if(NOT header STREQUAL expected)
  message(FATAL_ERROR "CSV header is '${header}', expected '${expected}'")
endif()
list(LENGTH csv_lines rows)
if(rows LESS 100)
  message(FATAL_ERROR "CSV has only ${rows} lines")
endif()
file(READ "${WORK}/fit.svg" svg)
if(NOT svg MATCHES "^<svg .*<polyline .*</svg>")
  message(FATAL_ERROR "SVG output is not a plot")
endif()
run_expect(1 COMMAND "${VOL_SURFACE}" "${WORK}/no-such-snapshot" MATCHES "error: .*cannot open")
run_expect(2 COMMAND "${VOL_SURFACE}" "${snap}" --bogus MATCHES "unknown flag '--bogus'" "usage:")
run_expect(2 COMMAND "${VOL_SURFACE}" MATCHES "missing <snapshot_dir>")
run_expect(2 COMMAND "${VOL_SURFACE}" "${snap}" --min-points 2 MATCHES "--min-points")
