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