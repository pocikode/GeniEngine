# The reference game (ROADMAP rule 2) and the M2 performance benchmark.
#
# `city` runs the reference game's first block: a cooked scene with 10,000 buildings
# (nine meshes, one per material), 100 point lights and a sun. Interactive when run
# normally; --budget-ms turns it into the CI performance check.
geni_add_sample(city main.cpp WorldMode.cpp PlayMode.cpp)
# One command for a distributable build (docs/design/0016): cmake --build <dir> --target package_city
geni_add_package(city NAME "Geni City")

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

# The streamed world (ROADMAP M5, docs/design/0007): fly through the middle of the 4 km
# metro world at 60 m/s, one 1/60 s step per frame, and fail on a hitch (exit 6), on a
# nearby cell that was not loaded in time (exit 7) or on going over the memory budget
# (exit 8). The hitch budget is loose for software Vulkan in CI; on real hardware use
# about two frames, e.g. -DGENI_WORLD_HITCH_MS=33. The full crossing is
#   city --world --route
set(GENI_WORLD_HITCH_MS 4000 CACHE STRING "Slowest allowed frame while flying through the streamed world, in milliseconds")
add_test(NAME benchmark_world
         COMMAND city --world --route --speed 60 --distance 480 --hidden --width 640 --height 360
                 --hitch-ms ${GENI_WORLD_HITCH_MS})
set_tests_properties(benchmark_world PROPERTIES LABELS "gpu;perf" TIMEOUT 600)

# The reference game (ROADMAP M6): `city --play --autopilot` plays itself through the
# input actions (walk to the car, get in, drive, brake, get out, walk on) among
# pedestrians and traffic, and fails with exit code 9 when a step does not happen or 10 on
# a Lua error. Every frame is a 1/60 s step, so it does not depend on the machine's speed.
add_test(NAME play_city
         COMMAND city --play --autopilot --hidden --width 480 --height 270)
set_tests_properties(play_city PROPERTIES LABELS "gpu;game" TIMEOUT 900)

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

  # The world from 600 m south-west of its centre, looking at downtown: full cells near
  # the camera, HLOD cells behind them, mesh LODs, camera-relative rendering. The camera
  # does not move, so everything the streamer loads before the first frame is all there is.
  add_test(NAME screenshot_world
           COMMAND ${CMAKE_COMMAND}
             -DSAMPLE=$<TARGET_FILE:city>
             "-DSAMPLE_ARGS=--world;--route;--speed;0;--distance;1200"
             -DFRAMES=10
             -DREFERENCE=${PROJECT_SOURCE_DIR}/tests/reference/world_metro.png
             -DCOMPARE=$<TARGET_FILE:image_compare>
             -DOUT_DIR=${CMAKE_BINARY_DIR}/screenshots
             -DTOLERANCE=12
             -DMAX_FRACTION=0.02
             -P ${PROJECT_SOURCE_DIR}/tests/run_screenshot.cmake)
  set_tests_properties(screenshot_world PROPERTIES LABELS "gpu" TIMEOUT 300)
  # The same view at 17:45 with a stopped TimeOfDayComponent (docs/design/0018): low sun,
  # sunset sky and fog.
  add_test(NAME screenshot_world_dusk
           COMMAND ${CMAKE_COMMAND}
             -DSAMPLE=$<TARGET_FILE:city>
             "-DSAMPLE_ARGS=--world;--route;--speed;0;--distance;1200;--time;17.75"
             -DFRAMES=10
             -DREFERENCE=${PROJECT_SOURCE_DIR}/tests/reference/world_metro_dusk.png
             -DCOMPARE=$<TARGET_FILE:image_compare>
             -DOUT_DIR=${CMAKE_BINARY_DIR}/screenshots
             -DTOLERANCE=12
             -DMAX_FRACTION=0.02
             -P ${PROJECT_SOURCE_DIR}/tests/run_screenshot.cmake)
  set_tests_properties(screenshot_world_dusk PROPERTIES LABELS "gpu" TIMEOUT 300)
endif()

if(GENI_BUILD_TESTS)
  # The game's title screen: the streamed street, the player, the car and the game UI
  # (docs/design/0015), without pedestrians or traffic so the frame is the same each run.
  add_test(NAME screenshot_play
           COMMAND ${CMAKE_COMMAND}
             -DSAMPLE=$<TARGET_FILE:city>
             "-DSAMPLE_ARGS=--play;--title-shot;--no-crowd;--no-traffic"
             -DFRAMES=40
             -DREFERENCE=${PROJECT_SOURCE_DIR}/tests/reference/play_title.png
             -DCOMPARE=$<TARGET_FILE:image_compare>
             -DOUT_DIR=${CMAKE_BINARY_DIR}/screenshots
             -DTOLERANCE=12
             -DMAX_FRACTION=0.02
             -P ${PROJECT_SOURCE_DIR}/tests/run_screenshot.cmake)
  set_tests_properties(screenshot_play PROPERTIES LABELS "gpu;game" TIMEOUT 300)
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
