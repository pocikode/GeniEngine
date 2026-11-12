# Runs the editor hidden on a project, captures its window and compares the capture with
# a reference image, like tests/run_screenshot.cmake does for the samples.
#
# Inputs (-D): SAMPLE (geni_editor), SAMPLE_ARGS (its arguments, a list), REFERENCE (png),
# COMPARE (image_compare), OUT_DIR, and optionally TOLERANCE and MAX_FRACTION.
# GENI_UPDATE_REFERENCES=1 in the environment overwrites the reference instead.

file(MAKE_DIRECTORY "${OUT_DIR}")
get_filename_component(name "${REFERENCE}" NAME_WE)
set(actual "${OUT_DIR}/${name}.png")
set(diff "${OUT_DIR}/${name}_diff.png")
file(REMOVE "${actual}" "${diff}")

# A fixed view with one building selected, so the capture shows the panels, the scene in
# its viewport, a selection box and the gizmo.
execute_process(
  COMMAND "${SAMPLE}" ${SAMPLE_ARGS}
          --run "camera 60 45 110 28 -20"
          --run "select Ground"
          --run "wait 3"
          --run "screenshot ${actual}"
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
execute_process(COMMAND "${COMPARE}" ${compare_args} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${actual} does not match ${REFERENCE} (see ${diff})")
endif()
