# geni_add_sample(<name> <sources>...)
#
# Builds samples/<name> as an executable linked against the engine. If the sample has an
# assets/ folder it is copied next to the executable, and its source location is passed
# in as GENI_SAMPLE_ASSETS_DIR so that edits show up without a rebuild when running from
# the build tree.
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
  endif()
endfunction()

add_subdirectory(sandbox)
