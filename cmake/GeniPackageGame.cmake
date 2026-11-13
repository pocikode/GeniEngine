# Lays out one game package (docs/design/0016). Run with cmake -P by the package_<game>
# targets (cmake/GeniPackage.cmake) and by the package tests.
#
# Inputs (-D):
#   EXECUTABLE     the built game; its assets/ and fonts/ folders are next to it
#   NAME           display name (bundle and folder name)
#   BUNDLE_ID      macOS bundle identifier
#   VERSION        engine/game version for the folder name and the marker
#   PLATFORM       e.g. macos-arm64, windows-x86_64, linux-x86_64
#   TARGET_SYSTEM  CMAKE_SYSTEM_NAME of the build (Darwin, Windows, Linux)
#   OUTPUT_DIR     where <NAME without spaces>-<VERSION>-<PLATFORM>/ is (re)created
#   MOLTENVK_ICD   macOS: MoltenVK's ICD manifest (empty: the package relies on the system's)
#   LIBRARY_DIRS   extra folders to find shared libraries in (vcpkg's bin on Windows)
#   DUMPBIN        Windows: dumpbin.exe, to read a program's imports
#   ARCHIVE        ON: also write a .zip (macOS, Windows) or .tar.gz (Linux)

cmake_minimum_required(VERSION 3.30)

foreach(required EXECUTABLE NAME VERSION PLATFORM TARGET_SYSTEM OUTPUT_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "GeniPackageGame.cmake: ${required} is not set")
  endif()
endforeach()
if(NOT EXISTS "${EXECUTABLE}")
  message(FATAL_ERROR "GeniPackageGame.cmake: ${EXECUTABLE} does not exist (build the game first)")
endif()

get_filename_component(build_dir "${EXECUTABLE}" DIRECTORY)
get_filename_component(exe_name "${EXECUTABLE}" NAME)
# Folder and archive names without spaces; the bundle keeps the display name.
string(REGEX REPLACE "[^A-Za-z0-9._-]" "" file_name "${NAME}")
set(package_name "${file_name}-${VERSION}-${PLATFORM}")
set(stage "${OUTPUT_DIR}/${package_name}")
file(REMOVE_RECURSE "${stage}")

# Where things go.
if(TARGET_SYSTEM STREQUAL "Darwin")
  set(bundle "${stage}/${NAME}.app")
  set(exe_dir "${bundle}/Contents/MacOS")
  set(resources_dir "${bundle}/Contents/Resources")
  set(lib_dir "${bundle}/Contents/Frameworks")
elseif(TARGET_SYSTEM STREQUAL "Windows")
  set(exe_dir "${stage}")
  set(resources_dir "${stage}")
  set(lib_dir "${stage}")
else()
  set(exe_dir "${stage}")
  set(resources_dir "${stage}")
  set(lib_dir "${stage}/lib")
endif()
file(MAKE_DIRECTORY "${exe_dir}" "${resources_dir}" "${lib_dir}")

# The executable.
file(COPY "${EXECUTABLE}" DESTINATION "${exe_dir}" FILE_PERMISSIONS
     OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(packaged_exe "${exe_dir}/${exe_name}")

# Assets: everything the build put next to the game except what only the cooker reads.
# Sounds ship as their cooked desktop variants (docs/design/0012), not the source WAVs.
if(EXISTS "${build_dir}/assets")
  file(COPY "${build_dir}/assets/" DESTINATION "${resources_dir}/assets"
       PATTERN "*.wav" EXCLUDE
       REGEX "/cooked/audio/(mobile|web)$" EXCLUDE
       PATTERN "*.gltf" EXCLUDE
       PATTERN "*.glb" EXCLUDE
       PATTERN "*.bin" EXCLUDE
       PATTERN "*.png" EXCLUDE
       PATTERN "*.jpg" EXCLUDE
       PATTERN "*.jpeg" EXCLUDE
       PATTERN "*.slang" EXCLUDE
       PATTERN "*.meta" EXCLUDE
       PATTERN "*.py" EXCLUDE
       PATTERN ".DS_Store" EXCLUDE)
endif()
if(EXISTS "${build_dir}/fonts")
  file(COPY "${build_dir}/fonts/" DESTINATION "${resources_dir}/fonts")
endif()

# The marker FileSystem::IsPackaged looks for.
string(TIMESTAMP built "%Y-%m-%dT%H:%M:%SZ" UTC)
file(WRITE "${resources_dir}/geni_package.json"
"{\n  \"name\": \"${NAME}\",\n  \"version\": \"${VERSION}\",\n  \"platform\": \"${PLATFORM}\",\n  \"executable\": \"${exe_name}\",\n  \"built\": \"${built}\"\n}\n")

# Shared libraries that are not part of the operating system.
if(TARGET_SYSTEM STREQUAL "Darwin")
  set(pre_exclude)
  set(post_exclude "^/usr/lib/" "^/System/")
elseif(TARGET_SYSTEM STREQUAL "Windows")
  # vulkan-1.dll comes with the GPU driver; the rest is Windows itself.
  set(pre_exclude "^api-ms-" "^ext-ms-" "^vulkan-1\\.dll$")
  set(post_exclude "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss]ystem32[/\\\\]" "[/\\\\]WINDOWS[/\\\\]")
else()
  # From the system: libc, the Vulkan loader and drivers, X11/Wayland, the C++ runtime
  # (linked statically with GENI_STATIC_RUNTIME). GLFW is shipped.
  set(pre_exclude "^ld-linux" "^libc\\.so" "^libm\\.so" "^libdl\\.so" "^libpthread\\.so" "^librt\\.so"
                  "^libvulkan\\.so" "^libX" "^libxcb" "^libxkb" "^libwayland" "^libdecor" "^libGL"
                  "^libstdc\\+\\+\\.so" "^libgcc_s\\.so" "^libasound" "^libpulse")
  set(post_exclude)
endif()
if(TARGET_SYSTEM STREQUAL "Windows" AND DUMPBIN)
  set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
  set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "dumpbin")
  set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${DUMPBIN}")
endif()
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${EXECUTABLE}"
  RESOLVED_DEPENDENCIES_VAR libraries
  UNRESOLVED_DEPENDENCIES_VAR unresolved
  PRE_EXCLUDE_REGEXES ${pre_exclude}
  POST_EXCLUDE_REGEXES ${post_exclude}
  DIRECTORIES ${LIBRARY_DIRS})
foreach(library ${unresolved})
  message(WARNING "package ${NAME}: could not find ${library}; it must be installed where the game runs")
endforeach()

set(shipped_libraries)
foreach(library ${libraries})
  get_filename_component(library_name "${library}" NAME)
  # Resolve symlinks (libglfw.3.dylib -> libglfw.3.5.dylib) but keep the name the game
  # asks for.
  file(REAL_PATH "${library}" real)
  file(COPY_FILE "${real}" "${lib_dir}/${library_name}")
  file(CHMOD "${lib_dir}/${library_name}" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
       OWNER_EXECUTE GROUP_EXECUTE WORLD_EXECUTE)
  list(APPEND shipped_libraries "${library_name}")
  message(STATUS "package ${NAME}: ships ${library_name}")
endforeach()

if(TARGET_SYSTEM STREQUAL "Darwin")
  # MoltenVK and its ICD manifest, inside the bundle where the Vulkan loader looks.
  if(MOLTENVK_ICD AND EXISTS "${MOLTENVK_ICD}")
    file(READ "${MOLTENVK_ICD}" icd)
    string(JSON icd_library GET "${icd}" ICD library_path)
    get_filename_component(icd_dir "${MOLTENVK_ICD}" DIRECTORY)
    if(NOT IS_ABSOLUTE "${icd_library}")
      set(icd_library "${icd_dir}/${icd_library}")
    endif()
    file(REAL_PATH "${icd_library}" moltenvk)
    file(COPY_FILE "${moltenvk}" "${lib_dir}/libMoltenVK.dylib")
    file(CHMOD "${lib_dir}/libMoltenVK.dylib" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    list(APPEND shipped_libraries libMoltenVK.dylib)
    # Relative to Contents/Resources/vulkan/icd.d.
    string(JSON icd SET "${icd}" ICD library_path "\"../../../Frameworks/libMoltenVK.dylib\"")
    file(WRITE "${resources_dir}/vulkan/icd.d/MoltenVK_icd.json" "${icd}\n")
    message(STATUS "package ${NAME}: ships libMoltenVK.dylib and its ICD manifest")
  else()
    message(WARNING "package ${NAME}: MoltenVK's ICD manifest was not found; the game needs MoltenVK installed")
  endif()

  # Every Mach-O file in the bundle refers to the shipped libraries through @rpath.
  set(machos "${packaged_exe}")
  foreach(library_name ${shipped_libraries})
    list(APPEND machos "${lib_dir}/${library_name}")
  endforeach()
  foreach(macho ${machos})
    execute_process(COMMAND otool -L "${macho}" OUTPUT_VARIABLE linked COMMAND_ERROR_IS_FATAL ANY)
    string(REPLACE "\n" ";" linked "${linked}")
    foreach(line ${linked})
      if(line MATCHES "^[ \t]+([^ \t]+) \\(compatibility")
        set(dependency "${CMAKE_MATCH_1}")
        get_filename_component(dependency_name "${dependency}" NAME)
        if(dependency_name IN_LIST shipped_libraries AND NOT dependency MATCHES "^@rpath/")
          execute_process(COMMAND install_name_tool -change "${dependency}" "@rpath/${dependency_name}" "${macho}"
                          ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
        endif()
      endif()
    endforeach()
    if(NOT macho STREQUAL packaged_exe)
      get_filename_component(macho_name "${macho}" NAME)
      execute_process(COMMAND install_name_tool -id "@rpath/${macho_name}" "${macho}" ERROR_QUIET
                      COMMAND_ERROR_IS_FATAL ANY)
    endif()
  endforeach()
  execute_process(COMMAND install_name_tool -add_rpath "@executable_path/../Frameworks" "${packaged_exe}"
                  ERROR_QUIET)

  file(WRITE "${bundle}/Contents/Info.plist"
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
  <key>CFBundleName</key><string>${NAME}</string>
  <key>CFBundleDisplayName</key><string>${NAME}</string>
  <key>CFBundleIdentifier</key><string>${BUNDLE_ID}</string>
  <key>CFBundleExecutable</key><string>${exe_name}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
")

  # Changing install names breaks signatures, and arm64 runs nothing unsigned: sign ad hoc,
  # libraries first. A Developer ID signature and notarisation are the publisher's step.
  foreach(library_name ${shipped_libraries})
    execute_process(COMMAND codesign --force --sign - "${lib_dir}/${library_name}" OUTPUT_QUIET ERROR_QUIET
                    COMMAND_ERROR_IS_FATAL ANY)
  endforeach()
  execute_process(COMMAND codesign --force --sign - "${bundle}" OUTPUT_QUIET ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
  set(run_path "${packaged_exe}")
elseif(TARGET_SYSTEM STREQUAL "Linux")
  # The game finds lib/ through its $ORIGIN/lib run path (geni_add_sample sets it).
  set(run_path "${packaged_exe}")
else()
  set(run_path "${packaged_exe}")
endif()

# For the package tests: where to start the game.
file(WRITE "${OUTPUT_DIR}/${package_name}.run" "${run_path}\n")

if(ARCHIVE)
  if(TARGET_SYSTEM STREQUAL "Darwin")
    set(archive "${OUTPUT_DIR}/${package_name}.zip")
    file(REMOVE "${archive}")
    # ditto keeps the bundle's symlinks, permissions and signature intact.
    execute_process(COMMAND ditto -c -k --keepParent "${NAME}.app" "${archive}"
                    WORKING_DIRECTORY "${stage}" COMMAND_ERROR_IS_FATAL ANY)
  elseif(TARGET_SYSTEM STREQUAL "Windows")
    set(archive "${OUTPUT_DIR}/${package_name}.zip")
    file(REMOVE "${archive}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar cf "${archive}" --format=zip "${package_name}"
                    WORKING_DIRECTORY "${OUTPUT_DIR}" COMMAND_ERROR_IS_FATAL ANY)
  else()
    set(archive "${OUTPUT_DIR}/${package_name}.tar.gz")
    file(REMOVE "${archive}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar czf "${archive}" "${package_name}"
                    WORKING_DIRECTORY "${OUTPUT_DIR}" COMMAND_ERROR_IS_FATAL ANY)
  endif()
  message(STATUS "package ${NAME}: ${archive}")
endif()
message(STATUS "package ${NAME}: ${stage}")
