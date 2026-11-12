# Packaging a game build (docs/design/0016).
#
#   geni_add_package(<target> [NAME <display name>] [BUNDLE_ID <reverse-dns id>])
#
# adds the target `package_<target>`, which builds the game and its cooked assets and
# writes build/<preset>/packages/<name>-<version>-<platform>/ plus a .zip (macOS,
# Windows) or .tar.gz (Linux) of it. The layout is made by cmake/GeniPackageGame.cmake.

set(GENI_PACKAGE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/GeniPackageGame.cmake" CACHE INTERNAL "")

# The platform part of package names.
if(APPLE)
  set(_geni_package_os macos)
elseif(WIN32)
  set(_geni_package_os windows)
else()
  set(_geni_package_os linux)
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _geni_package_arch)
if(_geni_package_arch MATCHES "^(amd64|x86_64|x64)$")
  set(_geni_package_arch x86_64)
elseif(_geni_package_arch MATCHES "^(arm64|aarch64)$")
  set(_geni_package_arch arm64)
endif()
set(GENI_PACKAGE_PLATFORM "${_geni_package_os}-${_geni_package_arch}" CACHE INTERNAL "")

# macOS: MoltenVK is not linked; the Vulkan loader finds it through an ICD manifest, which
# the package carries inside the bundle.
if(APPLE)
  find_file(GENI_MOLTENVK_ICD MoltenVK_icd.json
    PATHS
      /opt/homebrew/etc/vulkan/icd.d
      /usr/local/etc/vulkan/icd.d
      /opt/homebrew/share/vulkan/icd.d
      /usr/local/share/vulkan/icd.d
      "$ENV{VULKAN_SDK}/share/vulkan/icd.d"
      "$ENV{VULKAN_SDK}/etc/vulkan/icd.d"
    NO_DEFAULT_PATH)
endif()

# Windows: GET_RUNTIME_DEPENDENCIES reads imports with dumpbin, which sits next to cl.exe
# but is only on PATH in a Visual Studio shell.
if(MSVC)
  get_filename_component(_geni_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_program(GENI_DUMPBIN dumpbin HINTS "${_geni_compiler_dir}")
endif()

# Arguments for GeniPackageGame.cmake that do not depend on the game.
function(_geni_package_common_args out)
  set(${out}
    -DTARGET_SYSTEM=${CMAKE_SYSTEM_NAME}
    -DPLATFORM=${GENI_PACKAGE_PLATFORM}
    -DVERSION=${GENI_VERSION}
    "-DMOLTENVK_ICD=${GENI_MOLTENVK_ICD}"
    "-DDUMPBIN=${GENI_DUMPBIN}"
    # Where Windows DLLs from vcpkg live; GET_RUNTIME_DEPENDENCIES searches it.
    "-DLIBRARY_DIRS=$<$<BOOL:${VCPKG_INSTALLED_DIR}>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
    PARENT_SCOPE)
endfunction()

function(geni_add_package target)
  cmake_parse_arguments(PKG "" "NAME;BUNDLE_ID" "" ${ARGN})
  if(NOT PKG_NAME)
    set(PKG_NAME "${target}")
  endif()
  if(NOT PKG_BUNDLE_ID)
    set(PKG_BUNDLE_ID "org.geniengine.${target}")
  endif()
  _geni_package_common_args(common)

  add_custom_target(package_${target}
    COMMAND ${CMAKE_COMMAND}
      -DEXECUTABLE=$<TARGET_FILE:${target}>
      "-DNAME=${PKG_NAME}"
      -DBUNDLE_ID=${PKG_BUNDLE_ID}
      -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/packages
      -DARCHIVE=ON
      ${common}
      -P ${GENI_PACKAGE_SCRIPT}
    USES_TERMINAL
    COMMENT "Packaging ${PKG_NAME} for ${GENI_PACKAGE_PLATFORM}"
    VERBATIM)
  # The game, then its assets: copied and cooked next to the executable (geni_add_sample).
  add_dependencies(package_${target} ${target})
  foreach(dependency ${target}_shaders ${target}_cooked_assets)
    if(TARGET ${dependency})
      add_dependencies(package_${target} ${dependency})
    endif()
  endforeach()
endfunction()
