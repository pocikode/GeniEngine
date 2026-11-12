geni_add_sample(sandbox main.cpp)
geni_add_package(sandbox NAME "Geni Sandbox")

# Smoke test: the sample must start, simulate and render without a visible window.
add_test(NAME sample_sandbox COMMAND sandbox --hidden --frames 120)
set_tests_properties(sample_sandbox PROPERTIES LABELS "gpu" TIMEOUT 120)

# Screenshot comparison against tests/reference/ (see tests/run_screenshot.cmake).
# Frame 1 is the scene as set up; by frame 120 the crates have fallen and the bar has animated.
if(GENI_BUILD_TESTS)
  foreach(frames 1 120)
    add_test(NAME screenshot_sandbox_f${frames}
             COMMAND ${CMAKE_COMMAND}
               -DSAMPLE=$<TARGET_FILE:sandbox>
               -DFRAMES=${frames}
               -DREFERENCE=${PROJECT_SOURCE_DIR}/tests/reference/sandbox_f${frames}.png
               -DCOMPARE=$<TARGET_FILE:image_compare>
               -DOUT_DIR=${CMAKE_BINARY_DIR}/screenshots
               -P ${PROJECT_SOURCE_DIR}/tests/run_screenshot.cmake)
    set_tests_properties(screenshot_sandbox_f${frames} PROPERTIES LABELS "gpu" TIMEOUT 120)
  endforeach()
endif()


if(COMMAND geni_add_editor_target)
  geni_add_editor_target(sandbox)
endif()

# Packaging (docs/design/0016): the sandbox packaged into the build tree, its layout
# checked (no GPU), and the packaged program run from another folder (GPU).
if(GENI_BUILD_TESTS)
  _geni_package_common_args(package_args)
  foreach(mode layout run)
    set(run OFF)
    set(labels "package")
    if(mode STREQUAL "run")
      set(run ON)
      set(labels "package;gpu")
    endif()
    add_test(NAME package_${mode}
             COMMAND ${CMAKE_COMMAND}
               -DPACKAGE_SCRIPT=${GENI_PACKAGE_SCRIPT}
               -DEXECUTABLE=$<TARGET_FILE:sandbox>
               "-DNAME=Geni Sandbox"
               -DBUNDLE_ID=org.geniengine.sandbox
               -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/package_${mode}
               -DRUN=${run}
               ${package_args}
               -P ${PROJECT_SOURCE_DIR}/tests/run_package_check.cmake)
    set_tests_properties(package_${mode} PROPERTIES LABELS "${labels}" TIMEOUT 300)
  endforeach()
endif()
