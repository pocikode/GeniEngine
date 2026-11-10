cmake_minimum_required(VERSION 3.30)

# When building this repo on its own on Windows, pick up vcpkg automatically so that
# GLFW and GLEW from vcpkg.json are installed and found without extra arguments.
if(WIN32
   AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND NOT DEFINED CMAKE_TOOLCHAIN_FILE
   AND DEFINED ENV{VCPKG_ROOT})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(GeniEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GENI_BUILD_TESTS "Build the engine test suite" ${PROJECT_IS_TOP_LEVEL})
option(GENI_BUILD_SAMPLES "Build the programs in samples/" ${PROJECT_IS_TOP_LEVEL})
option(GENI_ENABLE_IMGUI "Build Dear ImGui into the engine and enable the debug overlay" ON)
option(GENI_ENABLE_TRACY "Build the Tracy profiler client into the engine" OFF)
option(GENI_FORCE_ASSERTS "Keep GENI_ASSERT enabled in release builds" OFF)

include(cmake/GeniSubmodules.cmake)
geni_require_submodules(glm nlohmann_json cgltf bullet3)

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
  src/core/Log.cpp
  src/input/InputManager.cpp
  src/graphics/ShaderProgram.cpp
  src/graphics/GraphicsAPI.cpp
  src/graphics/Texture.cpp
  src/render/Material.cpp
  src/render/Mesh.cpp
  src/render/RenderQueue.cpp
  src/scene/GameObject.cpp
  src/scene/Scene.cpp
  src/scene/Component.cpp
  src/scene/Skeleton.cpp
  src/scene/components/MeshComponent.cpp
  src/scene/components/SkinnedMeshComponent.cpp
  src/scene/components/CameraComponent.cpp
  src/scene/components/PlayerControllerComponent.cpp
  src/scene/components/LightComponent.cpp
  src/scene/components/AnimationComponent.cpp
  src/scene/components/PhysicsComponent.cpp
  src/scene/components/OrbitCameraComponent.cpp
  src/io/FileSystem.cpp
  src/physics/PhysicsManager.cpp
  src/physics/Collider.cpp
  src/physics/RigidBody.cpp
)

# glfw
find_package(glfw3 CONFIG REQUIRED)

# glew
find_package(GLEW REQUIRED)

# OpenGL
find_package(OpenGL REQUIRED)

add_library(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${GLEW_INCLUDE_DIRS} ${GLFW_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cgltf)

# glm
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glm)

# stb
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb)

# nlohmann json
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann_json)

# cgltf
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cgltf)

# bullet3
option(BUILD_UNIT_TESTS OFF)
option(BUILD_BULLET2_DEMOS OFF)
option(BUILD_EXTRAS OFF)
option(BUILD_SHARED_LIBS OFF)
# EXCLUDE_FROM_ALL: only the three Bullet libraries linked below get built.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/bullet3 EXCLUDE_FROM_ALL)
target_include_directories(${PROJECT_NAME} PRIVATE thirdparty/bullet3/src)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC
    glm::glm
    nlohmann_json::nlohmann_json
    glfw
    GLEW::GLEW
  PRIVATE
    LinearMath
    BulletCollision
    BulletDynamics
)

if(APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE "-framework OpenGL")
else()
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenGL::GL)
endif()

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
    ${GENI_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(geni_imgui PUBLIC ${GENI_IMGUI_DIR})
  target_link_libraries(geni_imgui PUBLIC glfw)
  if(NOT APPLE)
    target_link_libraries(geni_imgui PUBLIC OpenGL::GL)
  endif()

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

if(GENI_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

if(GENI_BUILD_TESTS)
  geni_require_submodules(doctest)
  add_subdirectory(tests)
endif()
