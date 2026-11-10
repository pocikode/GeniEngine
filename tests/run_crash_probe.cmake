# Runs geni_crash_probe in one crash mode and fails unless it died and produced a
# complete crash report on stderr and in a file.
# Arguments (-D): PROBE, MODE, REPORT_DIR

file(REMOVE_RECURSE "${REPORT_DIR}")
file(MAKE_DIRECTORY "${REPORT_DIR}")

execute_process(
  COMMAND "${PROBE}" "${MODE}" "${REPORT_DIR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output
)
message("${output}")

if(result STREQUAL "0")
  message(FATAL_ERROR "The probe exited normally; it was supposed to crash (mode '${MODE}').")
endif()

foreach(expected "==== GeniEngine crash report ====" "Stack trace:" "probe about to crash" "==== end of crash report ====")
  string(FIND "${output}" "${expected}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Crash output is missing '${expected}'.")
  endif()
endforeach()

file(GLOB reports "${REPORT_DIR}/geni_crash_*.log")
list(LENGTH reports count)
if(NOT count EQUAL 1)
  message(FATAL_ERROR "Expected one crash report file in ${REPORT_DIR}, found ${count}.")
endif()

file(READ "${reports}" contents)
string(FIND "${contents}" "==== end of crash report ====" position)
if(position EQUAL -1)
  message(FATAL_ERROR "The crash report file is incomplete: ${reports}")
endif()
