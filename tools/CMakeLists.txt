# Offline tools. The asset cooker is the only place cgltf, stb_image and the texture
# encoders are compiled; the engine links none of them (docs/design/0004).

# astc-encoder: one SIMD build for the machine that runs the cooker. ASTCENC_INVARIANCE
# (on by default) makes every ISA produce the same blocks, so cooked files do not depend
# on which machine cooked them. Override with -DGENI_ASTCENC_ISA=none|sse4.1|avx2|neon.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
  set(geni_astc_default_isa avx2)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
  set(geni_astc_default_isa neon)
else()
  set(geni_astc_default_isa none)
endif()
set(GENI_ASTCENC_ISA ${geni_astc_default_isa} CACHE STRING "astc-encoder SIMD build used by the asset cooker")
set_property(CACHE GENI_ASTCENC_ISA PROPERTY STRINGS none sse4.1 avx2 neon)

set(ASTCENC_CLI OFF CACHE BOOL "" FORCE)
set(ASTCENC_UNIVERSAL_BUILD OFF CACHE BOOL "" FORCE)
set(ASTCENC_INVARIANCE ON CACHE BOOL "" FORCE)
set(ASTCENC_UNITTEST OFF CACHE BOOL "" FORCE)
set(ASTCENC_SHAREDLIB OFF CACHE BOOL "" FORCE)
foreach(isa NATIVE NONE SSE2 SSE41 AVX2 NEON SVE_128 SVE_256)
  set(ASTCENC_ISA_${isa} OFF CACHE BOOL "" FORCE)
endforeach()
if(GENI_ASTCENC_ISA STREQUAL "avx2")
  set(ASTCENC_ISA_AVX2 ON CACHE BOOL "" FORCE)
elseif(GENI_ASTCENC_ISA STREQUAL "sse4.1")
  set(ASTCENC_ISA_SSE41 ON CACHE BOOL "" FORCE)
elseif(GENI_ASTCENC_ISA STREQUAL "neon")
  set(ASTCENC_ISA_NEON ON CACHE BOOL "" FORCE)
elseif(GENI_ASTCENC_ISA STREQUAL "none")
  set(ASTCENC_ISA_NONE ON CACHE BOOL "" FORCE)
else()
  message(FATAL_ERROR "GENI_ASTCENC_ISA must be none, sse4.1, avx2 or neon (got '${GENI_ASTCENC_ISA}')")
endif()
add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/astc-encoder ${CMAKE_BINARY_DIR}/astc-encoder EXCLUDE_FROM_ALL)

# meshoptimizer: mesh LODs and HLOD simplification (docs/design/0007). Cooker only.
geni_require_submodules(meshoptimizer)
add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/meshoptimizer ${CMAKE_BINARY_DIR}/meshoptimizer EXCLUDE_FROM_ALL)

add_library(geni_cook STATIC
  asset_cooker/Cooker.cpp
  asset_cooker/MeshProcessing.cpp
  asset_cooker/WorldCooker.cpp
  asset_cooker/AudioCooker.cpp
  ${PROJECT_SOURCE_DIR}/src/world/WorldFormat.cpp
  ${PROJECT_SOURCE_DIR}/src/nav/NavMeshBuilder.cpp
  asset_cooker/Ktx2Writer.cpp
  asset_cooker/TextureCooker.cpp
  ${PROJECT_SOURCE_DIR}/src/assets/AssetID.cpp
  ${PROJECT_SOURCE_DIR}/src/assets/CookedFormats.cpp
  ${PROJECT_SOURCE_DIR}/thirdparty/bc7enc_rdo/bc7enc.cpp
  ${PROJECT_SOURCE_DIR}/thirdparty/bc7enc_rdo/rgbcx.cpp
)
target_include_directories(geni_cook PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/thirdparty/glm
  ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json/include
  ${PROJECT_SOURCE_DIR}/thirdparty
  ${PROJECT_SOURCE_DIR}/thirdparty/cgltf
  ${PROJECT_SOURCE_DIR}/thirdparty/stb
  ${PROJECT_SOURCE_DIR}/thirdparty/bc7enc_rdo
  ${PROJECT_SOURCE_DIR}/thirdparty/astc-encoder/Source
)
# astc-encoder uses C++20; the cooker is a separate target so the engine stays on C++17.
target_compile_features(geni_cook PRIVATE cxx_std_20)
if(NOT MSVC)
  # bc7enc_rdo's block code is written for MSVC-style aliasing rules.
  set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/thirdparty/bc7enc_rdo/bc7enc.cpp
    ${PROJECT_SOURCE_DIR}/thirdparty/bc7enc_rdo/rgbcx.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-strict-aliasing;-w")
endif()
# geni_recast: navigation tiles cooked per world cell (docs/design/0014).
target_link_libraries(geni_cook PRIVATE astcenc-${GENI_ASTCENC_ISA}-static meshoptimizer geni_recast Threads::Threads)
target_include_directories(geni_cook PUBLIC ${PROJECT_SOURCE_DIR}/include)

add_executable(geni_asset_cooker asset_cooker/main.cpp)
target_link_libraries(geni_asset_cooker PRIVATE geni_cook)

# geni_cook_assets(<target> <source_dir> <output_dir>)
# Copies the source assets into the output folder, then cooks every glTF, image and sound in
# place. Runs before the target is linked and again only when a source asset changes.
function(geni_cook_assets target source_dir output_dir)
  file(GLOB_RECURSE cook_inputs CONFIGURE_DEPENDS
    "${source_dir}/*.gltf" "${source_dir}/*.glb"
    "${source_dir}/*.png" "${source_dir}/*.jpg" "${source_dir}/*.jpeg"
    "${source_dir}/*.bin" "${source_dir}/*.meta"
    "${source_dir}/*.gscene" "${source_dir}/*.gprefab" "${source_dir}/*.gworld"
    "${source_dir}/*.wav")
  set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${target}_assets.stamp")
  add_custom_command(OUTPUT "${stamp}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${source_dir}" "${output_dir}"
    COMMAND geni_asset_cooker --source-root "${source_dir}" --output-root "${output_dir}" --all
    COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
    DEPENDS geni_asset_cooker ${cook_inputs}
    COMMENT "Cooking assets for ${target}"
    VERBATIM)
  add_custom_target(${target}_cooked_assets DEPENDS "${stamp}")
  add_dependencies(${target} ${target}_cooked_assets)
endfunction()

if(GENI_BUILD_TESTS)
  # Cooker tests: their own executable, because the engine does not link the cooker.
  add_executable(geni_cook_tests
    ${PROJECT_SOURCE_DIR}/tests/test_cooker.cpp
    ${PROJECT_SOURCE_DIR}/src/assets/Ktx2.cpp
  )
  target_include_directories(geni_cook_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/asset_cooker
    ${PROJECT_SOURCE_DIR}/thirdparty/doctest
    ${PROJECT_SOURCE_DIR}/thirdparty/glm
    ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json/include
  )
  target_compile_definitions(geni_cook_tests PRIVATE
    GENI_COOKER_TEST_SOURCE="${PROJECT_SOURCE_DIR}/tests/assets"
    GENI_COOKER_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/cooker_tests")
  target_link_libraries(geni_cook_tests PRIVATE geni_cook)
  add_test(NAME cooker COMMAND geni_cook_tests)
  set_tests_properties(cooker PROPERTIES LABELS "tools" TIMEOUT 120)
endif()
