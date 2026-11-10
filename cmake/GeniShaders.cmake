# Offline GLSL -> SPIR-V compilation (docs/design/0001-rhi-and-vulkan-backend.md).
#
# Shaders are GLSL 450 targeting Vulkan 1.1. The compiler is glslangValidator, from the
# Vulkan SDK, Homebrew (`brew install glslang`) or apt (`glslang-tools`).

find_program(GENI_GLSLANG
  NAMES glslangValidator glslang
  HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin"
)
if(NOT GENI_GLSLANG)
  message(FATAL_ERROR
    "glslangValidator was not found. Install it with one of:\n"
    "  macOS:   brew install glslang\n"
    "  Ubuntu:  sudo apt install glslang-tools\n"
    "  Windows: the Vulkan SDK (sets VULKAN_SDK)")
endif()

set(GENI_GLSLANG_FLAGS -V --target-env vulkan1.1 --quiet)

# geni_compile_shaders(<target> SOURCE_DIR <dir> OUTPUT_DIR <dir> SOURCES <file>...)
#
# Compiles each SOURCE_DIR/<file> to OUTPUT_DIR/<file>.spv before <target> builds.
# Asset loaders read "<path>.spv" for a shader named "<path>" in a .mat file.
function(geni_compile_shaders target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "SOURCE_DIR;OUTPUT_DIR" "SOURCES")
  set(outputs "")
  foreach(rel IN LISTS ARG_SOURCES)
    set(in "${ARG_SOURCE_DIR}/${rel}")
    set(out "${ARG_OUTPUT_DIR}/${rel}.spv")
    get_filename_component(out_dir "${out}" DIRECTORY)
    add_custom_command(
      OUTPUT "${out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
      COMMAND "${GENI_GLSLANG}" ${GENI_GLSLANG_FLAGS} -o "${out}" "${in}"
      DEPENDS "${in}"
      COMMENT "Compiling shader ${rel}"
      VERBATIM
    )
    list(APPEND outputs "${out}")
  endforeach()
  add_custom_target(${target}_shaders DEPENDS ${outputs})
  add_dependencies(${target} ${target}_shaders)
endfunction()

# geni_embed_shaders(<target> OUTPUT_DIR <dir> SOURCES <absolute file>...)
#
# Compiles each shader into a header OUTPUT_DIR/<name>_<stage>.spv.h that defines
# `const uint32_t geni_spirv_<name>_<stage>[]`, for shaders built into a binary.
# Include <cstdint> before the header.
function(geni_embed_shaders target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "OUTPUT_DIR" "SOURCES")
  set(outputs "")
  foreach(in IN LISTS ARG_SOURCES)
    get_filename_component(name "${in}" NAME_WE)
    get_filename_component(ext "${in}" LAST_EXT)
    string(SUBSTRING "${ext}" 1 -1 stage)
    set(out "${ARG_OUTPUT_DIR}/${name}_${stage}.spv.h")
    add_custom_command(
      OUTPUT "${out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_OUTPUT_DIR}"
      COMMAND "${GENI_GLSLANG}" ${GENI_GLSLANG_FLAGS} --vn "geni_spirv_${name}_${stage}" -o "${out}" "${in}"
      DEPENDS "${in}"
      COMMENT "Embedding shader ${name}.${stage}"
      VERBATIM
    )
    list(APPEND outputs "${out}")
  endforeach()
  target_sources(${target} PRIVATE ${outputs})
  target_include_directories(${target} PRIVATE "${ARG_OUTPUT_DIR}")
endfunction()
