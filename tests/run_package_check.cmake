# Packages a game (cmake/GeniPackageGame.cmake) and checks the result (docs/design/0016).
#
# Inputs (-D): PACKAGE_SCRIPT, OUTPUT_DIR (recreated), RUN (ON: start the packaged game
# from another working directory with --hidden --frames 30, which fails if an asset is
# missing), and everything GeniPackageGame.cmake takes (EXECUTABLE, NAME, VERSION, ...).

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
include("${PACKAGE_SCRIPT}")

file(READ "${OUTPUT_DIR}/${package_name}.run" run_path)
string(STRIP "${run_path}" run_path)
if(NOT EXISTS "${run_path}")
  message(FATAL_ERROR "the packaged executable is missing: ${run_path}")
endif()
foreach(required geni_package.json assets/cooked_manifest.json fonts/Roboto-Medium.ttf)
  if(NOT EXISTS "${resources_dir}/${required}")
    message(FATAL_ERROR "the package has no ${required}")
  endif()
endforeach()
file(GLOB_RECURSE source_only "${resources_dir}/assets/*.gltf" "${resources_dir}/assets/*.glb"
     "${resources_dir}/assets/*.png" "${resources_dir}/assets/*.slang" "${resources_dir}/assets/*.meta")
if(source_only)
  message(FATAL_ERROR "the package carries source-only files: ${source_only}")
endif()
if(TARGET_SYSTEM STREQUAL "Darwin")
  foreach(required Contents/Info.plist Contents/Frameworks/libglfw.3.dylib)
    if(NOT EXISTS "${bundle}/${required}")
      message(FATAL_ERROR "the bundle has no ${required}")
    endif()
  endforeach()
  execute_process(COMMAND codesign --verify --deep --strict "${bundle}" RESULT_VARIABLE signed)
  if(NOT signed EQUAL 0)
    message(FATAL_ERROR "the bundle's signature does not verify")
  endif()
endif()
message(STATUS "package layout OK: ${stage}")

if(RUN)
  # Not the build tree and not the package: only what the package ships may be used.
  set(elsewhere "${OUTPUT_DIR}/run")
  file(MAKE_DIRECTORY "${elsewhere}")
  execute_process(COMMAND "${run_path}" --hidden --frames 30 --screenshot "${elsewhere}/frame.png"
                  WORKING_DIRECTORY "${elsewhere}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
  message("${output}")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "the packaged game exited with ${result}")
  endif()
  if(NOT EXISTS "${elsewhere}/frame.png")
    message(FATAL_ERROR "the packaged game did not render a frame")
  endif()
  if(output MATCHES "\\[(ERROR|WARN) *\\][^\n]*(not found|cannot|Could not|missing)")
    message(FATAL_ERROR "the packaged game could not find something it needs")
  endif()
endif()
