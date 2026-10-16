# Helpers shared by the CLI test scripts (run with cmake -P).
# Runs a command; fails unless it exits with `expected_rc` and its stdout
# plus stderr matches every regex in MATCHES. OUTPUT <var> stores that
# combined output in <var> for further checks.
function(run_expect expected_rc)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT" "COMMAND;MATCHES")
  execute_process(COMMAND ${arg_COMMAND} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(all "${out}${err}")
  if(NOT rc STREQUAL "${expected_rc}")
    message(FATAL_ERROR "exit ${rc}, expected ${expected_rc}: ${arg_COMMAND}\n${all}")
  endif()
  foreach(regex IN LISTS arg_MATCHES)
    if(NOT all MATCHES "${regex}")
      message(FATAL_ERROR "output of ${arg_COMMAND} does not match '${regex}':\n${all}")
    endif()
  endforeach()
  if(arg_OUTPUT)
    set(${arg_OUTPUT} "${all}" PARENT_SCOPE)
  endif()
  message(STATUS "ok (exit ${rc}): ${arg_COMMAND}")
endfunction()
