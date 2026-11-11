# The editor (ROADMAP M4, docs/design/0006-editor.md).
#
# geni_editor_core: document, undo commands, editor camera, gizmo maths and the project
# layer (cooking, shader compiling, hot reload). No ImGui and no GPU, so it is unit-tested
# without a window (tests/test_editor.cpp).
add_library(geni_editor_core STATIC
  core/CommandStack.cpp
  core/EditorCommands.cpp
  core/EditorDocument.cpp
  core/EditorCamera.cpp
  core/Gizmo.cpp
  core/Project.cpp
)
target_include_directories(geni_editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(geni_editor_core PUBLIC GeniEngine)

# geni_editor: the Dear ImGui application.
if(GENI_ENABLE_IMGUI)
  add_executable(geni_editor
    app/main.cpp
    app/EditorApp.cpp
    app/EditorPanels.cpp
    app/EditorViewport.cpp
  )
  target_link_libraries(geni_editor PRIVATE geni_editor_core)
  set_target_properties(geni_editor PROPERTIES ENABLE_EXPORTS ON)

  # The tools hot reload runs. Baked in so the editor works straight from the build
  # tree; --cooker / --slangc override them, and a copy next to the executable is found
  # when these paths are gone (an installed editor).
  target_compile_definitions(geni_editor PRIVATE
    GENI_EDITOR_SLANGC="${GENI_SLANGC_EXECUTABLE}"
    GENI_EDITOR_SHADER_MODULES="${GENI_SHADER_MODULE_DIR}"
  )
  if(TARGET geni_asset_cooker)
    target_compile_definitions(geni_editor PRIVATE GENI_EDITOR_COOKER="$<TARGET_FILE:geni_asset_cooker>")
    add_dependencies(geni_editor geni_asset_cooker)
  endif()
endif()

# geni_add_editor_target(<sample>)
#
# `cmake --build <dir> --target edit_<sample>` opens samples/<sample>/assets in the
# editor, with the sample's build folder as the cooked folder, so what the editor cooks
# and compiles is what the sample then loads.
function(geni_add_editor_target sample)
  if(TARGET geni_editor AND TARGET ${sample})
    add_custom_target(edit_${sample}
      COMMAND geni_editor
              --assets "${PROJECT_SOURCE_DIR}/samples/${sample}/assets"
              --cooked "$<TARGET_FILE_DIR:${sample}>/assets"
      DEPENDS geni_editor ${sample}
      USES_TERMINAL
      COMMENT "Opening samples/${sample} in the editor"
      VERBATIM)
  endif()
endfunction()
