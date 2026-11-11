# Renders a sample off-screen and compares the result with a reference image.
#
# Inputs (-D): SAMPLE (executable), FRAMES, REFERENCE (png), COMPARE (image_compare),
# OUT_DIR (where the capture and, on failure, the diff are written), and optionally
# SAMPLE_ARGS (a list of arguments before the standard ones),
# TOLERANCE (per-channel, default 8) and MAX_FRACTION (default 0.005). Denser scenes may
# allow a slightly larger tolerance because another driver's MSAA/PCF edges move.

#
# Set GENI_UPDATE_REFERENCES=1 in the environment to overwrite the reference with the
# capture instead of comparing.

file(MAKE_DIRECTORY "${OUT_DIR}")
get_filename_component(name "${REFERENCE}" NAME_WE)
set(actual "${OUT_DIR}/${name}.png")
set(diff "${OUT_DIR}/${name}_diff.png")
file(REMOVE "${actual}" "${diff}")

execute_process(
  COMMAND "${SAMPLE}" ${SAMPLE_ARGS} --hidden --no-overlay --frames ${FRAMES} --screenshot "${actual}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${SAMPLE} exited with ${result}")
endif()
if(NOT EXISTS "${actual}")
  message(FATAL_ERROR "${SAMPLE} did not write ${actual}")
endif()

if("$ENV{GENI_UPDATE_REFERENCES}")
  file(COPY_FILE "${actual}" "${REFERENCE}")
  message(STATUS "Reference updated: ${REFERENCE}")
  return()
endif()

if(NOT EXISTS "${REFERENCE}")
  message(FATAL_ERROR "No reference image at ${REFERENCE}. Run with GENI_UPDATE_REFERENCES=1 to create it.")
endif()

set(compare_args "${actual}" "${REFERENCE}" --diff "${diff}")
if(DEFINED TOLERANCE)
  list(APPEND compare_args --tolerance "${TOLERANCE}")
endif()
if(DEFINED MAX_FRACTION)
  list(APPEND compare_args --max-fraction "${MAX_FRACTION}")
endif()
execute_process(
  COMMAND "${COMPARE}" ${compare_args}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${actual} does not match ${REFERENCE} (see ${diff})")
endif()
