# Offline Slang -> SPIR-V compilation (docs/design/0003-slang-shaders.md).
#
# Shaders are Slang, compiled to SPIR-V 1.3 (the Vulkan 1.1 baseline that MoltenVK and
# mainstream Android devices support). The compiler is a pinned slangc release, downloaded
# once into .cache/slang/ at configure time so every machine and CI runner compiles with
# the same version. Set GENI_SLANGC to use another slangc instead.

set(GENI_SLANG_VERSION 2026.20)
set(GENI_SLANGC "" CACHE FILEPATH "slangc to use instead of the pinned release")

# The engine's shader modules (geni/*.slang), importable from any shader compiled here.
set(GENI_SHADER_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/render/shaders")
cmake_path(NORMAL_PATH GENI_SHADER_MODULE_DIR)
set(GENI_EMBED_SPIRV_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/GeniEmbedSpirv.cmake")

function(_geni_find_slangc out)
  if(GENI_SLANGC)
    set(${out} "${GENI_SLANGC}" PARENT_SCOPE)
    return()
  endif()

  # Archive name and SHA-256 per host, from the release page of shader-slang/slang.
  string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" arch)
  if(arch MATCHES "^(arm64|aarch64)$")
    set(arch aarch64)
  elseif(arch MATCHES "^(x86_64|amd64|x64)$")
    set(arch x86_64)
  endif()
  set(v ${GENI_SLANG_VERSION})
  if(CMAKE_HOST_APPLE AND arch STREQUAL "aarch64")
    set(archive "slang-${v}-macos-aarch64.tar.gz")
    set(hash 07fa8833fdbb343c3674c84fdd885910f495f3daaaa0c732b32980151e8b58c4)
  elseif(CMAKE_HOST_APPLE AND arch STREQUAL "x86_64")
    set(archive "slang-${v}-macos-x86_64.tar.gz")
    set(hash b4f01f615dc625f60c593d7d0a4bbd38e05dfa2a34bfd3973017c309d866feaa)
  elseif(CMAKE_HOST_WIN32 AND arch STREQUAL "x86_64")
    set(archive "slang-${v}-windows-x86_64.zip")
    set(hash e6dd32bbddf1720ebb4e679d55e9635ce792865a5bd84f3d2cd9af28647c0174)
  elseif(CMAKE_HOST_WIN32 AND arch STREQUAL "aarch64")
    set(archive "slang-${v}-windows-aarch64.zip")
    set(hash 4f18a7e76a59969bc9bf6ea596bd9e496a3c689af173990ae028b18f03e1ac1a)
  elseif(CMAKE_HOST_UNIX AND arch STREQUAL "x86_64")
    set(archive "slang-${v}-linux-x86_64-glibc-2.28.tar.gz")
    set(hash 84ebd777968caceea26b21bfd014341078be6047be6c5a963a69f984f896cebd)
  elseif(CMAKE_HOST_UNIX AND arch STREQUAL "aarch64")
    set(archive "slang-${v}-linux-aarch64-glibc-2.28.tar.gz")
    set(hash 9959d741713162efdbd863ff6d32400432b6e103eae3a9ee0afbdba6a00c6a9e)
  else()
    message(FATAL_ERROR
      "No pinned Slang release for this host (${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR}). "
      "Install slangc ${GENI_SLANG_VERSION} and configure with -DGENI_SLANGC=/path/to/slangc")
  endif()

  # Shared by every build folder of this checkout, so presets do not download it again.
  string(REGEX REPLACE "\\.(tar\\.gz|zip)$" "" folder "${archive}")
  set(root "${PROJECT_SOURCE_DIR}/.cache/slang/${folder}")
  if(CMAKE_HOST_WIN32)
    set(exe "${root}/bin/slangc.exe")
  else()
    set(exe "${root}/bin/slangc")
  endif()

  if(NOT EXISTS "${exe}")
    set(url "https://github.com/shader-slang/slang/releases/download/v${v}/${archive}")
    set(download "${PROJECT_SOURCE_DIR}/.cache/slang/${archive}")
    message(STATUS "Downloading the Slang compiler ${v} (${archive})")
    file(DOWNLOAD "${url}" "${download}" EXPECTED_HASH SHA256=${hash} STATUS status SHOW_PROGRESS)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE "${download}")
      list(GET status 1 reason)
      message(FATAL_ERROR
        "Could not download ${url}: ${reason}\n"
        "Download it yourself and extract it to ${root}, or configure with "
        "-DGENI_SLANGC=/path/to/slangc")
    endif()
    file(REMOVE_RECURSE "${root}")
    file(ARCHIVE_EXTRACT INPUT "${download}" DESTINATION "${root}")
    file(REMOVE "${download}")
    if(NOT EXISTS "${exe}")
      message(FATAL_ERROR "The Slang archive ${archive} has no bin/slangc")
    endif()
  endif()
  set(${out} "${exe}" PARENT_SCOPE)
endfunction()

_geni_find_slangc(GENI_SLANGC_EXECUTABLE)
message(STATUS "Slang compiler: ${GENI_SLANGC_EXECUTABLE}")

# Matrices are column-major in memory, like glm, and every shader is compiled with the
# engine modules on its import path.
set(GENI_SLANG_FLAGS
  -target spirv -profile spirv_1_3
  -matrix-layout-column-major
  -warnings-as-errors all
  -I "${GENI_SHADER_MODULE_DIR}"
)

# Entry points of a .slang file, as "<function>:<stage>" pairs. Every shader file names
# its entry points vertexMain and fragmentMain; a file with neither is a module.
function(_geni_slang_entry_points file out)
  file(READ "${file}" text)
  set(entries "")
  if(text MATCHES "[ \t\n]vertexMain[ \t]*\\(")
    list(APPEND entries "vertexMain:vertex")
  endif()
  if(text MATCHES "[ \t\n]fragmentMain[ \t]*\\(")
    list(APPEND entries "fragmentMain:fragment")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  set(${out} "${entries}" PARENT_SCOPE)
endfunction()

# Short stage suffix used in output names: vert, frag.
function(_geni_stage_suffix stage out)
  if(stage STREQUAL "vertex")
    set(${out} vert PARENT_SCOPE)
  else()
    set(${out} frag PARENT_SCOPE)
  endif()
endfunction()

# Adds the command that compiles one entry point of <in> to the SPIR-V file <out>.
function(_geni_add_slang_command in out entry stage comment)
  get_filename_component(out_dir "${out}" DIRECTORY)
  # Depfiles (imported modules) live outside the output folder, which samples copy as assets.
  string(MD5 key "${out}")
  set(depfile_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/geni_shader_deps")
  set(depfile "${depfile_dir}/${key}.d")
  # The engine's modules are listed explicitly as well: the depfile only exists after the
  # first compile, and a missing depfile directory used to make slangc skip writing it,
  # so edits to geni/*.slang did not rebuild the shaders that import them.
  file(GLOB_RECURSE engine_modules CONFIGURE_DEPENDS "${GENI_SHADER_MODULE_DIR}/geni/*.slang")
  add_custom_command(
    OUTPUT "${out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}" "${depfile_dir}"
    COMMAND "${GENI_SLANGC_EXECUTABLE}" "${in}" ${GENI_SLANG_FLAGS}
            -entry ${entry} -stage ${stage} -o "${out}" -depfile "${depfile}"
    DEPENDS "${in}" ${engine_modules}
    DEPFILE "${depfile}"
    COMMENT "${comment}"
    VERBATIM
  )
endfunction()

# geni_compile_shaders(<target> SOURCE_DIR <dir> OUTPUT_DIR <dir> SOURCES <file.slang>...)
#
# Compiles each entry point of SOURCE_DIR/<file>.slang to OUTPUT_DIR/<file>.slang.vert.spv
# and OUTPUT_DIR/<file>.slang.frag.spv before <target> builds. A .mat file names the
# .slang source; Shader::Load adds the stage suffix. Files without entry points are
# modules and produce nothing.
function(geni_compile_shaders target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "SOURCE_DIR;OUTPUT_DIR" "SOURCES")
  set(outputs "")
  foreach(rel IN LISTS ARG_SOURCES)
    set(in "${ARG_SOURCE_DIR}/${rel}")
    _geni_slang_entry_points("${in}" entries)
    foreach(pair IN LISTS entries)
      string(REPLACE ":" ";" pair "${pair}")
      list(GET pair 0 entry)
      list(GET pair 1 stage)
      _geni_stage_suffix(${stage} suffix)
      set(out "${ARG_OUTPUT_DIR}/${rel}.${suffix}.spv")
      _geni_add_slang_command("${in}" "${out}" ${entry} ${stage} "Compiling shader ${rel} (${stage})")
      list(APPEND outputs "${out}")
    endforeach()
  endforeach()
  add_custom_target(${target}_shaders DEPENDS ${outputs})
  add_dependencies(${target} ${target}_shaders)
endfunction()

# geni_embed_shaders(<target> OUTPUT_DIR <dir> SOURCES <absolute file.slang>...)
#
# Compiles each entry point into a header OUTPUT_DIR/<name>_<stage>.spv.h (stage: vert,
# frag) that defines `const uint32_t geni_spirv_<name>_<stage>[]`, for shaders built
# into a binary. Include <cstdint> before the header.
function(geni_embed_shaders target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "OUTPUT_DIR" "SOURCES")
  set(outputs "")
  foreach(in IN LISTS ARG_SOURCES)
    get_filename_component(name "${in}" NAME_WE)
    _geni_slang_entry_points("${in}" entries)
    if(NOT entries)
      message(FATAL_ERROR "${in} has no vertexMain or fragmentMain entry point")
    endif()
    foreach(pair IN LISTS entries)
      string(REPLACE ":" ";" pair "${pair}")
      list(GET pair 0 entry)
      list(GET pair 1 stage)
      _geni_stage_suffix(${stage} suffix)
      set(spv "${ARG_OUTPUT_DIR}/${name}_${suffix}.spv")
      set(header "${spv}.h")
      _geni_add_slang_command("${in}" "${spv}" ${entry} ${stage} "Compiling shader ${name}.slang (${stage})")
      add_custom_command(
        OUTPUT "${header}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${spv} -DOUTPUT=${header} -DNAME=geni_spirv_${name}_${suffix}
                -P "${GENI_EMBED_SPIRV_SCRIPT}"
        DEPENDS "${spv}" "${GENI_EMBED_SPIRV_SCRIPT}"
        COMMENT "Embedding shader ${name}_${suffix}"
        VERBATIM
      )
      list(APPEND outputs "${header}")
    endforeach()
  endforeach()
  target_sources(${target} PRIVATE ${outputs})
  target_include_directories(${target} PRIVATE "${ARG_OUTPUT_DIR}")
endfunction()
