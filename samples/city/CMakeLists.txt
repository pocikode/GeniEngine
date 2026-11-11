# The reference game (ROADMAP rule 2) and the M2 performance benchmark.
#
# `city` runs the reference game's first block: a cooked scene with 10,000 buildings
# (nine meshes, one per material), 100 point lights and a sun. Interactive when run
# normally; --budget-ms turns it into the CI performance check.
geni_add_sample(city main.cpp)

# Frame-time budget (ROADMAP M2, rule 4). The default is loose because CI may run on
# software Vulkan (lavapipe) and on shared runners; the target on real hardware is
# 16.6 ms. Override it for a release build on real hardware:
#   cmake --preset release -DGENI_BENCHMARK_BUDGET_MS=16.6
set(GENI_BENCHMARK_BUDGET_MS 2000 CACHE STRING "Frame-time budget for the city block benchmark, in milliseconds")
# Slowest frame allowed while the block loads (ROADMAP M3: no loading hitches). Loose for
# the same reason; on real hardware use about two frames, e.g. -DGENI_LOAD_FRAME_BUDGET_MS=33.
set(GENI_LOAD_FRAME_BUDGET_MS 4000 CACHE STRING "Slowest allowed frame while the city block loads, in milliseconds")

# 640x360 keeps the software Vulkan run inside the CTest timeout; the scene is the same
# 10,000 objects and 100 lights. Run the sample at 1280x720 for the hardware number.
add_test(NAME benchmark_city
         COMMAND city --hidden --width 640 --height 360 --frames 24 --warmup 6
                 --budget-ms ${GENI_BENCHMARK_BUDGET_MS}
                 --load-budget-ms ${GENI_LOAD_FRAME_BUDGET_MS})
set_tests_properties(benchmark_city PROPERTIES LABELS "gpu;perf" TIMEOUT 300)

# Screenshot comparison: the instanced, cascaded, tonemapped path must look the same
# every run (see tests/run_screenshot.cmake).
if(GENI_BUILD_TESTS)
  add_test(NAME screenshot_city
           COMMAND ${CMAKE_COMMAND}
             -DSAMPLE=$<TARGET_FILE:city>
             -DFRAMES=60
             -DREFERENCE=${PROJECT_SOURCE_DIR}/tests/reference/city_f60.png
             -DCOMPARE=$<TARGET_FILE:image_compare>
             -DOUT_DIR=${CMAKE_BINARY_DIR}/screenshots
             # A dense scene has many shadow/MSAA edges; another driver's rasterizer may
             # move more than 0.5% of the pixels by a few levels.
             -DTOLERANCE=12
             -DMAX_FRACTION=0.02
             -P ${PROJECT_SOURCE_DIR}/tests/run_screenshot.cmake)
  set_tests_properties(screenshot_city PROPERTIES LABELS "gpu" TIMEOUT 300)
endif()

# The editor on this sample (ROADMAP M4): `cmake --build <dir> --target edit_city`.
if(COMMAND geni_add_editor_target)
  geni_add_editor_target(city)
endif()

# The editor itself, driven through a scripted session on the city block: open the scene,
# select, move, undo, duplicate, make and unpack a prefab, play and stop, save a copy.
# Every step is checked by an `expect`; a failed one exits with code 5. It runs on a copy
# of the assets (tests/run_editor_smoke.cmake), which the editor cooks itself.
if(TARGET geni_editor)
  set(editor_args
    --assets "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    --cooked "$<TARGET_FILE_DIR:city>/assets"
    --hidden --no-hot-reload --width 1280 --height 720
    --scene scenes/city.gscene)

  add_test(NAME editor_smoke
           COMMAND ${CMAKE_COMMAND}
             -DEDITOR=$<TARGET_FILE:geni_editor>
             -DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/assets
             -DWORK_DIR=${CMAKE_BINARY_DIR}/editor_smoke
             -DSCENE=scenes/city.gscene
             -DSCRIPT=${PROJECT_SOURCE_DIR}/tests/editor_smoke.txt
             -P ${PROJECT_SOURCE_DIR}/tests/run_editor_smoke.cmake)
  set_tests_properties(editor_smoke PROPERTIES LABELS "gpu" TIMEOUT 300)

  if(GENI_BUILD_TESTS)
    add_test(NAME screenshot_editor
             COMMAND ${CMAKE_COMMAND}
               -DSAMPLE=$<TARGET_FILE:geni_editor>
               "-DSAMPLE_ARGS=${editor_args}"
               -DREFERENCE=${PROJECT_SOURCE_DIR}/tests/reference/editor_city.png
               -DCOMPARE=$<TARGET_FILE:image_compare>
               -DOUT_DIR=${CMAKE_BINARY_DIR}/screenshots
               -DTOLERANCE=12
               -DMAX_FRACTION=0.02
               -P ${PROJECT_SOURCE_DIR}/tests/run_editor_screenshot.cmake)
    set_tests_properties(screenshot_editor PROPERTIES LABELS "gpu" TIMEOUT 300)
  endif()
endif()
