cmake_minimum_required(VERSION 3.30)

include(FetchContent)

project(OrbisEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)
configure_file(
  ${PROJECT_SOURCE_DIR}/config.h.in
  ${PROJECT_BINARY_DIR}/config.h
)

set(SRC_FILES
  src/core/Engine.cpp
  src/core/Application.cpp
  src/input/InputManager.cpp
  src/graphics/ShaderProgram.cpp
  src/graphics/GraphicsAPI.cpp
  src/render/Material.cpp
  src/render/Mesh.cpp
  src/render/RenderQueue.cpp
  src/scene/GameObject.cpp
  src/scene/Scene.cpp
  src/scene/Component.cpp
  src/scene/components/MeshComponent.cpp
  src/scene/components/CameraComponent.cpp
  src/scene/components/PlayerControllerComponent.cpp
  src/io/FileSystem.cpp
)

include_directories(include)
add_library(${PROJECT_NAME} ${SRC_FILES})

find_package(PkgConfig REQUIRED)

# glfw
pkg_check_modules(GLFW REQUIRED glfw3)
include_directories(${GLFW_INCLUDE_DIRS})

# glew
pkg_check_modules(GLEW REQUIRED glew)
include_directories(${GLEW_INCLUDE_DIRS})

# glm
FetchContent_Declare(
	glm
	GIT_REPOSITORY	https://github.com/g-truc/glm.git
	GIT_TAG 	a532f5b1cf27d6a3c099437e6959cf7e398a0a67 #refs/tags/1.0.2
)
FetchContent_MakeAvailable(glm)

# stb
include_directories(thirdparty/stb)

target_link_libraries(
  ${PROJECT_NAME}
  ${GLFW_LIBRARIES}
  ${GLEW_LIBRARIES}
  glm::glm
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework OpenGL")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_BINARY_DIR})

target_link_directories(
  ${PROJECT_NAME}
  PUBLIC
  ${GLFW_LIBRARY_DIRS}
  ${GLEW_LIBRARY_DIRS}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD 
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${ASSETS_DIR}
  ${CMAKE_BINARY_DIR}/assets
)