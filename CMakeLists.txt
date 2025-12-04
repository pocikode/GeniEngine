cmake_minimum_required(VERSION 3.10)

project(OrbisEngine)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC_FILES
  src/core/Engine.cpp
  src/core/Application.cpp
  src/input/InputManager.cpp
  src/graphics/ShaderProgram.cpp
  src/graphics/GraphicsAPI.cpp
  src/render/Material.cpp
  src/render/Mesh.cpp
  src/render/RenderQueue.cpp
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

target_link_libraries(
  ${PROJECT_NAME}
  ${GLFW_LIBRARIES}
  ${GLEW_LIBRARIES}
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework OpenGL")
endif()

target_link_directories(
  ${PROJECT_NAME}
  PUBLIC
  ${GLFW_LIBRARY_DIRS}
  ${GLEW_LIBRARY_DIRS}
)
