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
  test_cooked_assets.cpp
  test_render_math.cpp
  test_rhi.cpp
  test_shader_reflection.cpp
  test_jobs.cpp
  test_assets.cpp
  test_serialization.cpp
  test_ktx2.cpp
  test_scene_editing.cpp
  test_hot_reload.cpp
  test_world.cpp
  test_gameplay.cpp
  test_2d.cpp
)
geni_embed_shaders(geni_tests
  OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/rhi_triangle.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bad_set.slang
    ${PROJECT_SOURCE_DIR}/src/render/shaders/default.slang
)
target_include_directories(geni_tests PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/doctest)
target_link_libraries(geni_tests PRIVATE GeniEngine)
target_compile_definitions(geni_tests PRIVATE GENI_TEST_ASSETS_DIR="${CMAKE_CURRENT_BINARY_DIR}/assets")
set_target_properties(geni_tests PROPERTIES ENABLE_EXPORTS ON)

# Copy the source fixtures and cook them into the build folder. Tests load cooked
# assets through cooked_manifest.json; the runtime has no glTF parser.
geni_cook_assets(geni_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets")

# Test suites whose name starts with "gpu" need a window and a Vulkan 1.1 device.
# Everything else runs anywhere. `ctest -LE gpu` skips the ones that need a GPU.
add_test(NAME unit COMMAND geni_tests --test-suite-exclude=gpu*)
add_test(NAME gpu COMMAND geni_tests --test-suite=gpu* "--test-suite-exclude=gpu rhi")
# "gpu rhi" creates its own Vulkan instance and device, so it runs in its own process:
# with the engine's instance alive at the same time, NVIDIA's driver (615.78, Wayland)
# crashes when the engine later destroys its swapchain.
add_test(NAME gpu_rhi COMMAND geni_tests "--test-suite=gpu rhi")
set_tests_properties(gpu gpu_rhi PROPERTIES LABELS "gpu" TIMEOUT 120)

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

# The cooker is an offline tool; run it once as a test so its CLI stays working.
add_test(NAME asset_cooker
         COMMAND geni_asset_cooker
                 --source-root ${CMAKE_CURRENT_SOURCE_DIR}/assets
                 --output-root ${CMAKE_CURRENT_BINARY_DIR}/cooker_check
                 --all)
set_tests_properties(asset_cooker PROPERTIES TIMEOUT 120 LABELS "tools")


# Editor core (document, undo commands, gizmo maths, project layer): its own executable,
# because it links geni_editor_core. No window or GPU needed.
if(TARGET geni_editor_core)
  add_executable(geni_editor_tests test_editor.cpp)
  target_include_directories(geni_editor_tests PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/doctest)
  target_link_libraries(geni_editor_tests PRIVATE geni_editor_core)
  target_compile_definitions(geni_editor_tests PRIVATE
    GENI_TEST_CMAKE_COMMAND="${CMAKE_COMMAND}"
    GENI_TEST_SLANGC="${GENI_SLANGC_EXECUTABLE}"
    GENI_TEST_SHADER_MODULES="${GENI_SHADER_MODULE_DIR}"
    GENI_TEST_SHADER_SOURCE="${PROJECT_SOURCE_DIR}/samples/sandbox/assets/shaders/lit.slang")
  set_target_properties(geni_editor_tests PROPERTIES ENABLE_EXPORTS ON)
  add_test(NAME editor COMMAND geni_editor_tests)
  set_tests_properties(editor PROPERTIES LABELS "editor" TIMEOUT 120)
endif()
