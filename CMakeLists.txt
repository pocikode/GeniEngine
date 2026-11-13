cmake_minimum_required(VERSION 3.30)

# When building this repo on its own on Windows, pick up vcpkg automatically so that
# GLFW from vcpkg.json is installed and found without extra arguments.
if(WIN32
   AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND NOT DEFINED CMAKE_TOOLCHAIN_FILE
   AND DEFINED ENV{VCPKG_ROOT})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# The version lives in include/core/Version.h (docs/api-policy.md).
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/include/core/Version.h _geni_version_lines
     REGEX "^#define GENI_VERSION_(MAJOR|MINOR|PATCH|SUFFIX) ")
foreach(_line ${_geni_version_lines})
  if(_line MATCHES "^#define GENI_VERSION_([A-Z]+) \"?([^\"]*)\"?$")
    set(_geni_version_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
  endif()
endforeach()
project(GeniEngine VERSION ${_geni_version_MAJOR}.${_geni_version_MINOR}.${_geni_version_PATCH})
set(GENI_VERSION "${PROJECT_VERSION}${_geni_version_SUFFIX}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GENI_BUILD_TESTS "Build the engine test suite" ${PROJECT_IS_TOP_LEVEL})
option(GENI_BUILD_SAMPLES "Build the programs in samples/" ${PROJECT_IS_TOP_LEVEL})
option(GENI_BUILD_TOOLS "Build the asset cooker and other offline tools" ${PROJECT_IS_TOP_LEVEL})
option(GENI_BUILD_EDITOR "Build the scene editor (geni_editor)" ${PROJECT_IS_TOP_LEVEL})
option(GENI_ENABLE_IMGUI "Build Dear ImGui into the engine and enable the debug overlay" ON)
option(GENI_ENABLE_TRACY "Build the Tracy profiler client into the engine" OFF)
option(GENI_FORCE_ASSERTS "Keep GENI_ASSERT enabled in release builds" OFF)
# Packaged Linux builds (docs/design/0016): libstdc++ and libgcc linked statically, because
# the Steam Linux Runtime's are older than a current GCC's.
option(GENI_STATIC_RUNTIME "Link libstdc++ and libgcc statically into games (Linux, GCC)" OFF)

include(cmake/GeniSubmodules.cmake)
geni_require_submodules(glm nlohmann_json cgltf JoltPhysics VulkanMemoryAllocator SPIRV-Reflect bc7enc_rdo astc-encoder
                        lua miniaudio recastnavigation)

# Vulkan: the loader and headers (Vulkan SDK, Homebrew vulkan-headers + vulkan-loader, or
# apt libvulkan-dev). Shaders are compiled offline by a pinned slangc (cmake/GeniShaders.cmake).
find_package(Vulkan REQUIRED)
include(cmake/GeniShaders.cmake)

# Homebrew puts the validation layer manifest where the loader does not look; debug runs
# point the loader at it (rhi::PrepareWindowSystem). Elsewhere the default search works.
find_path(GENI_VULKAN_LAYER_DIR VkLayer_khronos_validation.json
  PATHS
    /opt/homebrew/share/vulkan/explicit_layer.d
    /usr/local/share/vulkan/explicit_layer.d
  NO_DEFAULT_PATH
)
if(NOT GENI_VULKAN_LAYER_DIR)
  set(GENI_VULKAN_LAYER_DIR "")
endif()

# A game that embeds the engine with add_subdirectory owns the top-level assets/ folder.
set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)
configure_file(
  ${PROJECT_SOURCE_DIR}/config.h.in
  ${PROJECT_BINARY_DIR}/config.h
)

set(SRC_FILES
  src/core/Engine.cpp
  src/core/Application.cpp
  src/core/Assert.cpp
  src/core/CrashHandler.cpp
  src/core/DebugOverlay.cpp
  src/core/FixedTimestep.cpp
  src/core/JobSystem.cpp
  src/core/Log.cpp
  src/assets/AssetID.cpp
  src/assets/AssetManager.cpp
  src/assets/AssetManifest.cpp
  src/assets/CookedFormats.cpp
  src/assets/Ktx2.cpp
  src/input/InputManager.cpp
  src/input/InputActions.cpp
  src/animation/AnimationGraph.cpp
  src/audio/AudioSystem.cpp
  src/audio/MiniaudioImplementation.c
  src/nav/NavigationSystem.cpp
  src/nav/NavMeshBuilder.cpp
  src/script/ScriptSystem.cpp
  src/script/ScriptBindings.cpp
  src/ui/GameUI.cpp
  src/graphics/Texture.cpp
  src/render/Material.cpp
  src/render/Mesh.cpp
  src/render/Cascades.cpp
  src/render/ClusterGrid.cpp
  src/render/Frustum.cpp
  src/render/RenderQueue.cpp
  src/render/Renderer.cpp
  src/render/Shader.cpp
  src/render/TimeOfDay.cpp
  src/render/Weather.cpp
  src/render/ShaderReflection.cpp
  src/scene/GameObject.cpp
  src/scene/Scene.cpp
  src/scene/Picking.cpp
  src/scene/SceneSerializer.cpp
  src/scene/Component.cpp
  src/scene/Reflection.cpp
  src/scene/Skeleton.cpp
  src/scene/components/MeshComponent.cpp
  src/scene/components/SkinnedMeshComponent.cpp
  src/scene/components/CameraComponent.cpp
  src/scene/components/PlayerControllerComponent.cpp
  src/scene/components/LightComponent.cpp
  src/scene/components/AnimationComponent.cpp
  src/scene/components/PhysicsComponent.cpp
  src/scene/components/OrbitCameraComponent.cpp
  src/scene/components/FlyCameraComponent.cpp
  src/scene/components/CharacterControllerComponent.cpp
  src/scene/components/VehicleComponent.cpp
  src/scene/components/ThirdPersonCameraComponent.cpp
  src/scene/components/AnimatorComponent.cpp
  src/scene/components/AudioSourceComponent.cpp
  src/scene/components/NavAgentComponent.cpp
  src/scene/components/ScriptComponent.cpp
  src/scene/components/SpriteComponent.cpp
  src/scene/components/TilemapComponent.cpp
  src/scene/components/TimeOfDayComponent.cpp
  src/scene/components/ParticleEmitterComponent.cpp
  src/scene/components/WeatherComponent.cpp
  src/world/WorldFormat.cpp
  src/world/WorldStreamer.cpp
  src/io/FileSystem.cpp
  src/io/FileWatcher.cpp
  src/io/ImageWriter.cpp
  src/physics/PhysicsManager.cpp
  src/physics/RigidBody.cpp
  src/physics/CharacterController.cpp
  src/physics/Vehicle.cpp
  src/rhi/vulkan/VulkanDevice.cpp
  src/rhi/vulkan/VmaImplementation.cpp
  thirdparty/SPIRV-Reflect/spirv_reflect.c
)

# glfw
find_package(glfw3 CONFIG REQUIRED)

add_library(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/SPIRV-Reflect
)

# The engine's built-in shaders, compiled to SPIR-V and embedded in the library.
geni_embed_shaders(${PROJECT_NAME}
  OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/builtin_shaders
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/default.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/skinned.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/shadow.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/shadow_skinned.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/sky.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/tonemap.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/sprite.slang
)

# glm
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glm)

# stb
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb)

# nlohmann json
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann_json)

# Jolt Physics (docs/design/0008). Double precision, so bodies live in world coordinates
# and origin rebasing never moves them. Only src/physics/ includes Jolt.
set(DOUBLE_PRECISION ON CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
# The engine derives from Jolt's interfaces and is built with RTTI and exceptions.
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
# The rest of the build uses the DLL runtime on Windows.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(JPH_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/JoltPhysics/Build ${CMAKE_BINARY_DIR}/JoltPhysics EXCLUDE_FROM_ALL)

# Lua 5.4 (docs/design/0013), compiled as C++ so errors unwind with exceptions. Only
# src/script/ and the script component include it.
set(GENI_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lua)
set(GENI_LUA_SOURCES)
foreach(name lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump lfunc lgc linit liolib llex
             lmathlib lmem loadlib lobject lopcodes loslib lparser lstate lstring lstrlib ltable ltablib ltm
             lundump lutf8lib lvm lzio)
  list(APPEND GENI_LUA_SOURCES ${GENI_LUA_DIR}/${name}.c)
endforeach()
set_source_files_properties(${GENI_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(geni_lua STATIC ${GENI_LUA_SOURCES})
target_include_directories(geni_lua PUBLIC ${GENI_LUA_DIR})
if(UNIX)
  target_compile_definitions(geni_lua PRIVATE LUA_USE_POSIX)
endif()
target_compile_options(geni_lua PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-w>)

# Recast, Detour and DetourCrowd (docs/design/0014). Only src/nav/ includes them.
set(GENI_RECAST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/recastnavigation)
file(GLOB GENI_RECAST_SOURCES
  ${GENI_RECAST_DIR}/Recast/Source/*.cpp
  ${GENI_RECAST_DIR}/Detour/Source/*.cpp
  ${GENI_RECAST_DIR}/DetourCrowd/Source/*.cpp)
add_library(geni_recast STATIC ${GENI_RECAST_SOURCES})
target_include_directories(geni_recast PUBLIC
  ${GENI_RECAST_DIR}/Recast/Include
  ${GENI_RECAST_DIR}/Detour/Include
  ${GENI_RECAST_DIR}/DetourCrowd/Include)
target_compile_options(geni_recast PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-w>)

find_package(Threads REQUIRED)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
    glm::glm
    nlohmann_json::nlohmann_json
    glfw
  PRIVATE
    Jolt
    Vulkan::Vulkan
    geni_lua
    geni_recast
    Threads::Threads
)

# miniaudio (docs/design/0012): one translation unit, only src/audio/ includes it.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniaudio)
if(UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()
if(APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()

# The game UI's default font (docs/design/0015); samples also copy it next to themselves.
target_compile_definitions(${PROJECT_NAME} PRIVATE
  GENI_DEFAULT_UI_FONT="${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/misc/fonts/Roboto-Medium.ttf")

# Only src/rhi/vulkan/ includes these (docs/design/0001).
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/VulkanMemoryAllocator/include
)
# Third-party code compiled into the engine: keep its warnings out of the build log.
set_source_files_properties(src/rhi/vulkan/VmaImplementation.cpp thirdparty/SPIRV-Reflect/spirv_reflect.c
  src/audio/MiniaudioImplementation.c PROPERTIES COMPILE_OPTIONS
  "$<$<OR:$<CXX_COMPILER_ID:Clang,AppleClang,GNU>,$<C_COMPILER_ID:Clang,AppleClang,GNU>>:-w>")

if(WIN32)
  # Stack traces in the crash handler.
  target_link_libraries(${PROJECT_NAME} PRIVATE dbghelp)
elseif(UNIX)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
endif()

# Dear ImGui (debug overlay; later the editor)
if(GENI_ENABLE_IMGUI)
  geni_require_submodules(imgui)
  set(GENI_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
  add_library(geni_imgui STATIC
    ${GENI_IMGUI_DIR}/imgui.cpp
    ${GENI_IMGUI_DIR}/imgui_demo.cpp
    ${GENI_IMGUI_DIR}/imgui_draw.cpp
    ${GENI_IMGUI_DIR}/imgui_tables.cpp
    ${GENI_IMGUI_DIR}/imgui_widgets.cpp
    ${GENI_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${GENI_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
  )
  target_include_directories(geni_imgui PUBLIC ${GENI_IMGUI_DIR})
  target_link_libraries(geni_imgui PUBLIC glfw PRIVATE Vulkan::Vulkan)

  # PUBLIC so games can call ImGui:: between Application::Update and Render.
  target_link_libraries(${PROJECT_NAME} PUBLIC geni_imgui)
  target_compile_definitions(${PROJECT_NAME} PUBLIC GENI_ENABLE_IMGUI)
endif()

# Tracy profiler
if(GENI_ENABLE_TRACY)
  geni_require_submodules(tracy)
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  # Collect nothing until the Tracy UI connects, so an instrumented build can run all day.
  set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
  # The engine installs its own crash handler (core/CrashHandler.h).
  set(TRACY_NO_CRASH_HANDLER ON CACHE BOOL "" FORCE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tracy EXCLUDE_FROM_ALL)

  target_link_libraries(${PROJECT_NAME} PUBLIC Tracy::TracyClient)
  target_compile_definitions(${PROJECT_NAME} PUBLIC GENI_ENABLE_TRACY)
endif()

if(GENI_FORCE_ASSERTS)
  target_compile_definitions(${PROJECT_NAME} PUBLIC GENI_ENABLE_ASSERTS=1)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_BINARY_DIR})

# Copies the embedding game's assets next to its build. Skipped when there is no such
# folder, which is the case when this repo is built on its own (samples copy their own).
if(EXISTS ${ASSETS_DIR})
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${ASSETS_DIR}
    ${CMAKE_BINARY_DIR}/assets
  )
endif()

if(GENI_BUILD_TESTS OR GENI_BUILD_SAMPLES)
  enable_testing()
endif()

if(GENI_BUILD_TESTS)
  # Standalone: compares a capture with a reference image (tests/run_screenshot.cmake).
  add_executable(image_compare tools/image_compare.cpp)
  target_include_directories(image_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb)
endif()

if(GENI_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

# After tools/ (the editor runs the cooker) and before samples/ and tests/ (which add
# editor targets and editor tests).
if(GENI_BUILD_EDITOR)
  add_subdirectory(editor)
endif()

# Packaging a game build (docs/design/0016): geni_add_package(<target>).
include(cmake/GeniPackage.cmake)

if(GENI_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

if(GENI_BUILD_TESTS)
  geni_require_submodules(doctest)
  add_subdirectory(tests)
endif()
