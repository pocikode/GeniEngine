# geni_add_sample(<name> <sources>...)
#
# Builds samples/<name> as an executable linked against the engine. If the sample has an
# assets/ folder it is copied next to the executable, and its source location is passed
# in as GENI_SAMPLE_ASSETS_DIR so that edits show up without a rebuild when running from
# the build tree. Shaders under assets/shaders are compiled to SPIR-V into the copy next
# to the executable, where FileSystem::ResolveAssetPath finds them.
function(geni_add_sample name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE GeniEngine)
  # Function names in crash-report stack traces on Linux.
  set_target_properties(${name} PROPERTIES ENABLE_EXPORTS ON)

  set(assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  if(EXISTS "${assets}")
    target_compile_definitions(${name} PRIVATE GENI_SAMPLE_ASSETS_DIR="${assets}")
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${assets}" "$<TARGET_FILE_DIR:${name}>/assets"
    )

    file(GLOB_RECURSE shaders CONFIGURE_DEPENDS RELATIVE "${assets}" "${assets}/*.vert" "${assets}/*.frag")
    if(shaders)
      set(compiled "${CMAKE_CURRENT_BINARY_DIR}/compiled_assets")
      geni_compile_shaders(${name} SOURCE_DIR "${assets}" OUTPUT_DIR "${compiled}" SOURCES ${shaders})
      # Both copies hang off the shader target, so editing a shader alone is enough:
      # without that, the assets next to the executable kept the previous SPIR-V until
      # the sample relinked.
      add_custom_command(TARGET ${name}_shaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${assets}" "$<TARGET_FILE_DIR:${name}>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${compiled}" "$<TARGET_FILE_DIR:${name}>/assets"
      )
    endif()
  endif()
endfunction()

add_subdirectory(sandbox)

add_subdirectory(city)
