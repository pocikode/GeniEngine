# Geni Plains, the 2D sample (docs/design/0017): a tilemap level, the player on the
# character controller, crates on 2D physics, animated coins and parallax backgrounds.
# The level is assets/scenes/level.gscene, generated once by tools/gen_2d_assets.py and
# edited in the editor from then on: cmake --build <dir> --target edit_platformer
geni_add_sample(platformer main.cpp)
geni_add_package(platformer NAME "Geni Plains")

# Plays the level through the input actions (run right, jump at walls, gaps and crates)
# and fails with exit code 9 if it does not reach the end or collects under a quarter of the
# coins. Every frame is one 1/60 s step, so the run is the same on any machine.
add_test(NAME play_platformer COMMAND platformer --autopilot --hidden --width 640 --height 360)
set_tests_properties(play_platformer PROPERTIES LABELS "gpu;game" TIMEOUT 300)

# 10,000 independent animated sprites on top of the level (docs/design/0017 "To verify":
# 60 fps on the development Mac). The default budget is loose for software Vulkan in CI;
# on real hardware use -DGENI_SPRITE_BUDGET_MS=16.6.
set(GENI_SPRITE_BUDGET_MS 2000 CACHE STRING "Frame-time budget for the 10,000-sprite benchmark, in milliseconds")
add_test(NAME benchmark_sprites
         COMMAND platformer --benchmark 10000 --frames 90 --hidden --width 640 --height 360
                 --budget-ms ${GENI_SPRITE_BUDGET_MS})
set_tests_properties(benchmark_sprites PROPERTIES LABELS "gpu;perf" TIMEOUT 300)

if(GENI_BUILD_TESTS)
  # The start of the level: tiles, parallax, the hero, a crate, coins and the HUD.
  add_test(NAME screenshot_platformer
           COMMAND ${CMAKE_COMMAND}
             -DSAMPLE=$<TARGET_FILE:platformer>
             -DFRAMES=30
             -DREFERENCE=${PROJECT_SOURCE_DIR}/tests/reference/platformer_f30.png
             -DCOMPARE=$<TARGET_FILE:image_compare>
             -DOUT_DIR=${CMAKE_BINARY_DIR}/screenshots
             -DTOLERANCE=12
             -DMAX_FRACTION=0.01
             -P ${PROJECT_SOURCE_DIR}/tests/run_screenshot.cmake)
  set_tests_properties(screenshot_platformer PROPERTIES LABELS "gpu" TIMEOUT 120)
endif()

if(COMMAND geni_add_editor_target)
  geni_add_editor_target(platformer)
endif()

# The editor on the 2D level: move a crate, change tilemap and sprite properties, play and
# stop, save a copy (tests/editor_smoke_2d.txt).
if(TARGET geni_editor)
  add_test(NAME editor_smoke_2d
           COMMAND ${CMAKE_COMMAND}
             -DEDITOR=$<TARGET_FILE:geni_editor>
             -DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/assets
             -DWORK_DIR=${CMAKE_BINARY_DIR}/editor_smoke_2d
             -DSCENE=scenes/level.gscene
             -DSCRIPT=${PROJECT_SOURCE_DIR}/tests/editor_smoke_2d.txt
             -DEXPECT_FILES=scenes/copy.gscene
             -P ${PROJECT_SOURCE_DIR}/tests/run_editor_smoke.cmake)
  set_tests_properties(editor_smoke_2d PROPERTIES LABELS "gpu" TIMEOUT 300)
endif()
