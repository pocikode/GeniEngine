# Runs the editor through tests/editor_smoke.txt on a copy of a sample's assets, so the
# session can save scenes and prefabs without touching the source tree. The copy starts
# without cooked assets: the editor has to cook the project itself before it can open it.
#
# Inputs (-D): EDITOR (geni_editor), ASSETS (source assets folder), WORK_DIR (scratch
# folder, recreated), SCENE (asset path to open), SCRIPT (the session), and optionally
# EXPECT_FILES.

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${ASSETS}/" DESTINATION "${WORK_DIR}/assets")

execute_process(
  COMMAND "${EDITOR}"
          --assets "${WORK_DIR}/assets" --cooked "${WORK_DIR}/cooked"
          --hidden --no-hot-reload --width 1280 --height 720
          --scene "${SCENE}" --script "${SCRIPT}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "the editor session failed (exit code ${result})")
endif()
# EXPECT_FILES (optional): what the session must have written, relative to the assets.
if(NOT DEFINED EXPECT_FILES)
  set(EXPECT_FILES scenes/copy.gscene prefabs/lamp.gprefab)
endif()
foreach(written ${EXPECT_FILES})
  if(NOT EXISTS "${WORK_DIR}/assets/${written}")
    message(FATAL_ERROR "the editor session did not write ${written}")
  endif()
endforeach()
