# geni_require_submodules(<name>...)
#
# Makes sure thirdparty/<name> is checked out. On a fresh clone the submodule folders
# are empty; rather than failing with a confusing "no CMakeLists.txt" error, fetch the
# missing ones so that configure works without a separate `git submodule update` step.
function(geni_require_submodules)
  set(missing "")
  foreach(name IN LISTS ARGN)
    set(dir "${PROJECT_SOURCE_DIR}/thirdparty/${name}")
    file(GLOB entries "${dir}/*")
    if(NOT entries)
      list(APPEND missing "thirdparty/${name}")
    endif()
  endforeach()

  if(NOT missing)
    return()
  endif()

  find_package(Git QUIET)
  if(NOT GIT_FOUND OR NOT EXISTS "${PROJECT_SOURCE_DIR}/.git")
    message(FATAL_ERROR
      "Missing third-party sources: ${missing}\n"
      "This is not a git checkout (or git was not found), so they cannot be fetched "
      "automatically. Clone the repository with git and run:\n"
      "  git submodule update --init --recursive")
  endif()

  message(STATUS "Fetching git submodules: ${missing}")
  # Shallow first: the pinned commits are all that is needed, and some histories are large.
  execute_process(
    COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive --depth 1 -- ${missing}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE result
  )
  if(NOT result EQUAL 0)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive -- ${missing}
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      RESULT_VARIABLE result
    )
  endif()

  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "Could not fetch: ${missing}\n"
      "Run this yourself and configure again:\n"
      "  git submodule update --init --recursive")
  endif()
endfunction()
