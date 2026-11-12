#pragma once

// The editor's look: colours, fonts and the small vector icons drawn with ImGui draw
// lists (no icon font is vendored).

#include <imgui.h>

#include <filesystem>

namespace Geni::Editor
{

enum class Icon
{
    Move,
    Rotate,
    Scale,
    Play,
    Pause,
    Stop,
    Step,
    Globe,
    Cube,
    Magnet,
    Folder,
    FolderOpen,
    File,
    Model,
    Mesh,
    Texture,
    Material,
    Animation,
    Scene,
    Prefab,
    World,
    Shader,
    Empty,
    Camera,
    Sun,
    Bulb,
    Spot,
    Person,
    Physics,
    Component,
    Eye,
    EyeOff,
    Plus,
    Search,
    Dots,
    Info,
    Warning,
    Error,
    Grid,
    Trash,
    Refresh,
};

namespace Theme
{
// Palette, shared by panels that draw by hand.
constexpr ImU32 Accent = IM_COL32(72, 140, 230, 255);
constexpr ImU32 AccentDim = IM_COL32(52, 98, 160, 255);
constexpr ImU32 Text = IM_COL32(222, 226, 233, 255);
constexpr ImU32 TextDim = IM_COL32(140, 147, 160, 255);
constexpr ImU32 Prefab = IM_COL32(110, 170, 255, 255);
constexpr ImU32 AxisX = IM_COL32(232, 72, 85, 255);
constexpr ImU32 AxisY = IM_COL32(130, 205, 60, 255);
constexpr ImU32 AxisZ = IM_COL32(64, 132, 240, 255);
constexpr ImU32 Warning = IM_COL32(240, 190, 70, 255);
constexpr ImU32 Error = IM_COL32(240, 90, 90, 255);
constexpr ImU32 PanelDark = IM_COL32(26, 28, 33, 255);
} // namespace Theme

// Colours and metrics. Call once after ImGui is up.
void ApplyEditorTheme();
// The UI font (Roboto) from `fontFolder`, at a size that reads on high-DPI screens too.
// Keeps ImGui's default font when the file is missing.
void LoadEditorFonts(const std::filesystem::path &fontFolder);

// The colour an icon is normally drawn in (type colours: blue for prefabs, yellow for
// lights, and so on).
ImU32 IconColor(Icon icon);
// Draws `icon` centred on `center`, fitting a square of `size` pixels.
void DrawIcon(ImDrawList &draw, Icon icon, ImVec2 center, float size, ImU32 color);
// Inline icon at the cursor, the height of a text line, then SameLine.
void IconText(Icon icon, ImU32 color = 0);
// A square toolbar button with an icon. `active` draws it pressed.
bool IconButton(const char *id, Icon icon, bool active = false, const char *tooltip = nullptr, float size = 0.0f);
// A spinning arc for work in progress.
void Spinner(ImDrawList &draw, ImVec2 center, float radius, ImU32 color, double time);

} // namespace Geni::Editor
