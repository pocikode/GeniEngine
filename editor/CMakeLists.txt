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
  core/TimelineEditing.cpp
  core/MaterialEditing.cpp
  core/AnimGraphEditing.cpp
)
target_include_directories(geni_editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(geni_editor_core PUBLIC GeniEngine)

# geni_editor: the Dear ImGui application.
if(GENI_ENABLE_IMGUI)
  add_executable(geni_editor
    app/main.cpp
    app/EditorApp.cpp
    app/EditorAssets.cpp
    app/EditorPanels.cpp
    app/EditorProject.cpp
    app/EditorTheme.cpp
    app/EditorViewport.cpp
    app/EditorTimeline.cpp
    app/EditorMaterial.cpp
    app/EditorAnimGraph.cpp
  )
  target_link_libraries(geni_editor PRIVATE geni_editor_core)
  set_target_properties(geni_editor PROPERTIES ENABLE_EXPORTS ON)

  # The tools hot reload runs. Baked in so the editor works straight from the build
  # tree; --cooker / --slangc override them, and a copy next to the executable is found
  # when these paths are gone (an installed editor).
  target_compile_definitions(geni_editor PRIVATE
    GENI_EDITOR_SLANGC="${GENI_SLANGC_EXECUTABLE}"
    GENI_EDITOR_SHADER_MODULES="${GENI_SHADER_MODULE_DIR}"
    GENI_EDITOR_FONT_DIR="${PROJECT_SOURCE_DIR}/thirdparty/imgui/misc/fonts"
  )
  # The UI font beside the editor too, for a copy of the editor without the source tree.
  add_custom_command(TARGET geni_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:geni_editor>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/thirdparty/imgui/misc/fonts/Roboto-Medium.ttf"
            "$<TARGET_FILE_DIR:geni_editor>/fonts/Roboto-Medium.ttf"
    VERBATIM)
  if(TARGET geni_asset_cooker)
    target_compile_definitions(geni_editor PRIVATE GENI_EDITOR_COOKER="$<TARGET_FILE:geni_asset_cooker>")
    add_dependencies(geni_editor geni_asset_cooker)
  endif()
endif()

# `cmake --build <dir> --target edit` opens the editor on its project manager, and on
# macOS <build>/GeniEngine.app does the same from Finder or the Dock, like Godot.app.
if(TARGET geni_editor)
  add_custom_target(edit
    COMMAND geni_editor
    DEPENDS geni_editor
    USES_TERMINAL
    COMMENT "Opening the GeniEngine editor"
    VERBATIM)

  if(APPLE)
    set(GENI_EDITOR_APP "${CMAKE_BINARY_DIR}/GeniEngine.app")
    file(GENERATE OUTPUT "${GENI_EDITOR_APP}/Contents/Info.plist" CONTENT [=[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>GeniEngine</string>
  <key>CFBundleDisplayName</key><string>GeniEngine</string>
  <key>CFBundleIdentifier</key><string>dev.geniengine.editor</string>
  <key>CFBundleExecutable</key><string>GeniEngine</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.5</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
]=])
    # The bundle runs the editor this build tree makes, so it is always the current one.
    file(GENERATE OUTPUT "${GENI_EDITOR_APP}/Contents/MacOS/GeniEngine"
      CONTENT "#!/bin/sh\nexec \"$<TARGET_FILE:geni_editor>\" \"$@\"\n"
      FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
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
