# New test files must be listed here by hand, like engine sources in the root CMakeLists.txt.
add_executable(geni_tests
  main.cpp
  TestEngine.cpp
  test_animation.cpp
  test_fixed_timestep.cpp
  test_log_assert.cpp
  test_scene.cpp
  test_transform.cpp
  test_engine_loop.cpp
  test_gltf.cpp
  test_rhi.cpp
)
geni_embed_shaders(geni_tests
  OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/rhi_triangle.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/rhi_triangle.frag
)
target_include_directories(geni_tests PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/doctest)
target_link_libraries(geni_tests PRIVATE GeniEngine)
target_compile_definitions(geni_tests PRIVATE GENI_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
set_target_properties(geni_tests PROPERTIES ENABLE_EXPORTS ON)

# Test suites whose name starts with "gpu" need a window and an OpenGL 3.3 context.
# Everything else runs anywhere. `ctest -LE gpu` skips the ones that need a GPU.
add_test(NAME unit COMMAND geni_tests --test-suite-exclude=gpu*)
add_test(NAME gpu COMMAND geni_tests --test-suite=gpu*)
set_tests_properties(gpu PROPERTIES LABELS "gpu" TIMEOUT 120)

# Crash handler: a helper program dies on purpose and must leave a report behind.
add_executable(geni_crash_probe crash_probe.cpp)
target_link_libraries(geni_crash_probe PRIVATE GeniEngine)
set_target_properties(geni_crash_probe PROPERTIES ENABLE_EXPORTS ON)

foreach(mode segfault abort assert exception)
  # The probe is meant to die, which CTest would count as a failure, so a script runs
  # it and checks that it died and left a complete report.
  add_test(NAME crash_report_${mode}
           COMMAND ${CMAKE_COMMAND}
             -DPROBE=$<TARGET_FILE:geni_crash_probe>
             -DMODE=${mode}
             -DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/crash_reports/${mode}
             -P ${CMAKE_CURRENT_SOURCE_DIR}/run_crash_probe.cmake)
  set_tests_properties(crash_report_${mode} PROPERTIES TIMEOUT 60)
endforeach()
