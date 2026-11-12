#include "EditorTheme.h"

#include <algorithm>
#include <cmath>
#include <system_error>

namespace Geni::Editor
{

namespace
{

ImVec4 Rgb(int r, int g, int b, float a = 1.0f)
{
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, a);
}

ImVec2 Add(ImVec2 a, float x, float y)
{
    return ImVec2(a.x + x, a.y + y);
}

void Arrow(ImDrawList &draw, ImVec2 from, ImVec2 to, ImU32 color, float thickness, float head)
{
    draw.AddLine(from, to, color, thickness);
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::max(std::sqrt(dx * dx + dy * dy), 1e-3f);
    const float ux = dx / length;
    const float uy = dy / length;
    const ImVec2 left(to.x - ux * head - uy * head * 0.7f, to.y - uy * head + ux * head * 0.7f);
    const ImVec2 right(to.x - ux * head + uy * head * 0.7f, to.y - uy * head - ux * head * 0.7f);
    draw.AddTriangleFilled(Add(to, ux * 1.0f, uy * 1.0f), left, right, color);
}

void Cube(ImDrawList &draw, ImVec2 c, float h, ImU32 color, float thickness, bool filled)
{
    // An isometric cube: top face and two side faces.
    const ImVec2 top(c.x, c.y - h);
    const ImVec2 upperLeft(c.x - h * 0.87f, c.y - h * 0.5f);
    const ImVec2 upperRight(c.x + h * 0.87f, c.y - h * 0.5f);
    const ImVec2 middle(c.x, c.y);
    const ImVec2 lowerLeft(c.x - h * 0.87f, c.y + h * 0.5f);
    const ImVec2 lowerRight(c.x + h * 0.87f, c.y + h * 0.5f);
    const ImVec2 bottom(c.x, c.y + h);
    if (filled)
    {
        const ImU32 light = color;
        const ImU32 dark = (color & 0x00ffffffu) | (static_cast<ImU32>(((color >> 24) & 0xff) * 0.55f) << 24);
        draw.AddQuadFilled(top, upperRight, middle, upperLeft, light);
        draw.AddQuadFilled(upperLeft, middle, bottom, lowerLeft, dark);
        draw.AddQuadFilled(middle, upperRight, lowerRight, bottom, dark);
        return;
    }
    const ImVec2 outline[6] = {top, upperRight, lowerRight, bottom, lowerLeft, upperLeft};
    draw.AddPolyline(outline, 6, color, ImDrawFlags_Closed, thickness);
    draw.AddLine(upperLeft, middle, color, thickness);
    draw.AddLine(upperRight, middle, color, thickness);
    draw.AddLine(middle, bottom, color, thickness);
}

} // namespace

void ApplyEditorTheme()
{
    ImGuiStyle &style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 9.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.SeparatorTextBorderSize = 1.0f;
    style.DockingSeparatorSize = 3.0f;

    ImVec4 *c = style.Colors;
    const ImVec4 darkest = Rgb(20, 22, 26);
    const ImVec4 dark = Rgb(27, 29, 34);
    const ImVec4 panel = Rgb(34, 37, 43);
    const ImVec4 frame = Rgb(46, 50, 58);
    const ImVec4 frameHover = Rgb(56, 61, 71);
    const ImVec4 frameActive = Rgb(64, 70, 82);
    const ImVec4 accent = Rgb(72, 140, 230);
    const ImVec4 accentHover = Rgb(92, 158, 240);
    const ImVec4 accentDim = Rgb(52, 98, 160);

    c[ImGuiCol_Text] = Rgb(222, 226, 233);
    c[ImGuiCol_TextDisabled] = Rgb(122, 129, 142);
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = Rgb(38, 41, 48, 0.98f);
    c[ImGuiCol_Border] = Rgb(16, 17, 20);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameHover;
    c[ImGuiCol_FrameBgActive] = frameActive;
    c[ImGuiCol_TitleBg] = dark;
    c[ImGuiCol_TitleBgActive] = dark;
    c[ImGuiCol_TitleBgCollapsed] = dark;
    c[ImGuiCol_MenuBarBg] = dark;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = Rgb(70, 76, 88);
    c[ImGuiCol_ScrollbarGrabHovered] = Rgb(88, 95, 110);
    c[ImGuiCol_ScrollbarGrabActive] = Rgb(104, 112, 128);
    c[ImGuiCol_CheckMark] = accentHover;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = frameHover;
    c[ImGuiCol_ButtonActive] = accentDim;
    c[ImGuiCol_Header] = Rgb(52, 98, 160, 0.75f);
    c[ImGuiCol_HeaderHovered] = Rgb(60, 66, 78);
    c[ImGuiCol_HeaderActive] = accentDim;
    c[ImGuiCol_Separator] = Rgb(18, 19, 23);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentHover;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = accentHover;
    c[ImGuiCol_InputTextCursor] = c[ImGuiCol_Text];
    c[ImGuiCol_Tab] = dark;
    c[ImGuiCol_TabHovered] = frameHover;
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = dark;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview] = Rgb(72, 140, 230, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = darkest;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TableHeaderBg] = dark;
    c[ImGuiCol_TableBorderStrong] = Rgb(18, 19, 23);
    c[ImGuiCol_TableBorderLight] = Rgb(44, 47, 54);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = Rgb(255, 255, 255, 0.025f);
    c[ImGuiCol_TextSelectedBg] = Rgb(72, 140, 230, 0.4f);
    c[ImGuiCol_DragDropTarget] = accentHover;
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_ModalWindowDimBg] = Rgb(0, 0, 0, 0.55f);
}

void LoadEditorFonts(const std::filesystem::path &fontFolder)
{
    ImGuiIO &io = ImGui::GetIO();
    std::error_code ec;
    const std::filesystem::path font = fontFolder / "Roboto-Medium.ttf";
    if (fontFolder.empty() || !std::filesystem::exists(font, ec))
    {
        return;
    }
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    if (ImFont *loaded = io.Fonts->AddFontFromFileTTF(font.string().c_str(), 15.0f, &config))
    {
        io.FontDefault = loaded;
        ImGui::GetStyle().FontSizeBase = 15.0f;
    }
}

ImU32 IconColor(Icon icon)
{
    switch (icon)
    {
    case Icon::Folder:
    case Icon::FolderOpen:
        return IM_COL32(220, 180, 90, 255);
    case Icon::Model:
    case Icon::Mesh:
        return IM_COL32(120, 200, 230, 255);
    case Icon::Texture:
        return IM_COL32(200, 140, 220, 255);
    case Icon::Material:
        return IM_COL32(230, 130, 90, 255);
    case Icon::Animation:
        return IM_COL32(120, 210, 140, 255);
    case Icon::Scene:
        return IM_COL32(230, 200, 110, 255);
    case Icon::Prefab:
        return Theme::Prefab;
    case Icon::World:
        return IM_COL32(110, 200, 190, 255);
    case Icon::Shader:
        return IM_COL32(170, 150, 240, 255);
    case Icon::Camera:
        return IM_COL32(200, 200, 210, 255);
    case Icon::Sun:
    case Icon::Bulb:
    case Icon::Spot:
        return IM_COL32(250, 210, 90, 255);
    case Icon::Person:
        return IM_COL32(120, 200, 230, 255);
    case Icon::Physics:
        return IM_COL32(240, 150, 90, 255);
    case Icon::Warning:
        return Theme::Warning;
    case Icon::Error:
        return Theme::Error;
    case Icon::Info:
        return IM_COL32(150, 180, 230, 255);
    case Icon::Play:
        return IM_COL32(110, 210, 120, 255);
    default:
        return Theme::Text;
    }
}

void DrawIcon(ImDrawList &draw, Icon icon, ImVec2 c, float size, ImU32 color)
{
    const float h = size * 0.5f;
    const float t = std::max(1.0f, size * 0.09f); // line thickness
    switch (icon)
    {
    case Icon::Move:
        Arrow(draw, c, Add(c, 0, -h), color, t, h * 0.38f);
        Arrow(draw, c, Add(c, 0, h), color, t, h * 0.38f);
        Arrow(draw, c, Add(c, -h, 0), color, t, h * 0.38f);
        Arrow(draw, c, Add(c, h, 0), color, t, h * 0.38f);
        break;
    case Icon::Rotate:
    {
        const float r = h * 0.72f;
        draw.PathArcTo(c, r, 3.14159f * 0.15f, 3.14159f * 1.75f, 20);
        draw.PathStroke(color, ImDrawFlags_None, t);
        const ImVec2 end(c.x + r * std::cos(3.14159f * 0.15f), c.y + r * std::sin(3.14159f * 0.15f));
        draw.AddTriangleFilled(Add(end, -h * 0.35f, -h * 0.05f), Add(end, h * 0.3f, -h * 0.2f), Add(end, h * 0.05f, h * 0.38f), color);
        break;
    }
    case Icon::Scale:
        draw.AddRect(Add(c, -h, -h * 0.2f), Add(c, h * 0.2f, h), color, 1.0f, 0, t);
        Arrow(draw, Add(c, -h * 0.1f, h * 0.1f), Add(c, h, -h), color, t, h * 0.42f);
        break;
    case Icon::Play:
        draw.AddTriangleFilled(Add(c, -h * 0.55f, -h * 0.75f), Add(c, h * 0.8f, 0), Add(c, -h * 0.55f, h * 0.75f), color);
        break;
    case Icon::Pause:
        draw.AddRectFilled(Add(c, -h * 0.6f, -h * 0.7f), Add(c, -h * 0.15f, h * 0.7f), color, 1.0f);
        draw.AddRectFilled(Add(c, h * 0.15f, -h * 0.7f), Add(c, h * 0.6f, h * 0.7f), color, 1.0f);
        break;
    case Icon::Stop:
        draw.AddRectFilled(Add(c, -h * 0.62f, -h * 0.62f), Add(c, h * 0.62f, h * 0.62f), color, 2.0f);
        break;
    case Icon::Step:
        draw.AddTriangleFilled(Add(c, -h * 0.7f, -h * 0.7f), Add(c, h * 0.4f, 0), Add(c, -h * 0.7f, h * 0.7f), color);
        draw.AddRectFilled(Add(c, h * 0.45f, -h * 0.7f), Add(c, h * 0.75f, h * 0.7f), color);
        break;
    case Icon::Globe:
    case Icon::World:
        draw.AddCircle(c, h * 0.85f, color, 24, t);
        draw.AddEllipse(c, ImVec2(h * 0.38f, h * 0.85f), color, 0.0f, 20, t);
        draw.AddLine(Add(c, -h * 0.85f, 0), Add(c, h * 0.85f, 0), color, t);
        break;
    case Icon::Cube:
        Cube(draw, c, h * 0.9f, color, t, false);
        break;
    case Icon::Magnet:
    {
        const float r = h * 0.55f;
        draw.PathArcTo(Add(c, 0, h * 0.05f), r, 0.0f, 3.14159f, 16);
        draw.PathStroke(color, ImDrawFlags_None, t * 2.2f);
        draw.AddLine(Add(c, -r, h * 0.05f), Add(c, -r, -h * 0.7f), color, t * 2.2f);
        draw.AddLine(Add(c, r, h * 0.05f), Add(c, r, -h * 0.7f), color, t * 2.2f);
        draw.AddRectFilled(Add(c, -r - t * 1.1f, -h * 0.85f), Add(c, -r + t * 1.1f, -h * 0.55f), IM_COL32(230, 80, 80, 255));
        draw.AddRectFilled(Add(c, r - t * 1.1f, -h * 0.85f), Add(c, r + t * 1.1f, -h * 0.55f), IM_COL32(230, 80, 80, 255));
        break;
    }
    case Icon::Folder:
    case Icon::FolderOpen:
        draw.AddRectFilled(Add(c, -h * 0.9f, -h * 0.7f), Add(c, -h * 0.1f, -h * 0.35f), color, 1.5f);
        draw.AddRectFilled(Add(c, -h * 0.9f, -h * 0.5f), Add(c, h * 0.9f, h * 0.7f), color, 2.0f);
        if (icon == Icon::FolderOpen)
        {
            draw.AddRectFilled(Add(c, -h * 0.7f, -h * 0.2f), Add(c, h * 0.9f, h * 0.7f), IM_COL32(255, 255, 255, 50), 2.0f);
        }
        break;
    case Icon::File:
    case Icon::Shader:
    {
        const ImVec2 points[5] = {Add(c, -h * 0.65f, -h * 0.85f), Add(c, h * 0.25f, -h * 0.85f), Add(c, h * 0.65f, -h * 0.45f),
                                  Add(c, h * 0.65f, h * 0.85f), Add(c, -h * 0.65f, h * 0.85f)};
        draw.AddPolyline(points, 5, color, ImDrawFlags_Closed, t);
        if (icon == Icon::Shader)
        {
            draw.AddLine(Add(c, -h * 0.3f, -h * 0.1f), Add(c, -h * 0.5f, h * 0.2f), color, t);
            draw.AddLine(Add(c, -h * 0.5f, h * 0.2f), Add(c, -h * 0.3f, h * 0.5f), color, t);
            draw.AddLine(Add(c, h * 0.3f, -h * 0.1f), Add(c, h * 0.5f, h * 0.2f), color, t);
            draw.AddLine(Add(c, h * 0.5f, h * 0.2f), Add(c, h * 0.3f, h * 0.5f), color, t);
        }
        else
        {
            draw.AddLine(Add(c, -h * 0.35f, h * 0.05f), Add(c, h * 0.35f, h * 0.05f), color, t);
            draw.AddLine(Add(c, -h * 0.35f, h * 0.4f), Add(c, h * 0.35f, h * 0.4f), color, t);
        }
        break;
    }
    case Icon::Model:
        Cube(draw, c, h * 0.9f, color, t, true);
        break;
    case Icon::Mesh:
    {
        const ImVec2 a = Add(c, 0, -h * 0.85f);
        const ImVec2 b = Add(c, -h * 0.85f, h * 0.7f);
        const ImVec2 d = Add(c, h * 0.85f, h * 0.7f);
        const ImVec2 m = Add(c, 0, h * 0.15f);
        draw.AddTriangle(a, b, d, color, t);
        draw.AddLine(a, m, color, t);
        draw.AddLine(b, m, color, t);
        draw.AddLine(d, m, color, t);
        break;
    }
    case Icon::Texture:
        draw.AddRect(Add(c, -h * 0.85f, -h * 0.75f), Add(c, h * 0.85f, h * 0.75f), color, 2.0f, 0, t);
        draw.AddTriangleFilled(Add(c, -h * 0.65f, h * 0.55f), Add(c, -h * 0.15f, -h * 0.15f), Add(c, h * 0.3f, h * 0.55f), color);
        draw.AddTriangleFilled(Add(c, h * 0.05f, h * 0.55f), Add(c, h * 0.35f, h * 0.15f), Add(c, h * 0.65f, h * 0.55f), color);
        draw.AddCircleFilled(Add(c, h * 0.4f, -h * 0.35f), h * 0.15f, color);
        break;
    case Icon::Material:
        draw.AddCircleFilled(c, h * 0.82f, color, 24);
        draw.AddCircleFilled(Add(c, -h * 0.28f, -h * 0.28f), h * 0.25f, IM_COL32(255, 255, 255, 120), 16);
        break;
    case Icon::Animation:
        draw.AddRect(Add(c, -h * 0.85f, -h * 0.65f), Add(c, h * 0.85f, h * 0.65f), color, 2.0f, 0, t);
        for (int i = 0; i < 3; ++i)
        {
            const float x = -h * 0.55f + static_cast<float>(i) * h * 0.55f;
            draw.AddRectFilled(Add(c, x - h * 0.1f, -h * 0.55f), Add(c, x + h * 0.1f, -h * 0.35f), color);
            draw.AddRectFilled(Add(c, x - h * 0.1f, h * 0.35f), Add(c, x + h * 0.1f, h * 0.55f), color);
        }
        draw.AddTriangleFilled(Add(c, -h * 0.2f, -h * 0.25f), Add(c, h * 0.3f, 0), Add(c, -h * 0.2f, h * 0.25f), color);
        break;
    case Icon::Scene:
        draw.AddRect(Add(c, -h * 0.85f, -h * 0.4f), Add(c, h * 0.85f, h * 0.8f), color, 2.0f, 0, t);
        draw.AddQuadFilled(Add(c, -h * 0.85f, -h * 0.85f), Add(c, h * 0.85f, -h * 0.85f), Add(c, h * 0.85f, -h * 0.5f), Add(c, -h * 0.85f, -h * 0.5f), color);
        draw.AddTriangleFilled(Add(c, -h * 0.2f, -h * 0.05f), Add(c, h * 0.35f, h * 0.2f), Add(c, -h * 0.2f, h * 0.45f), color);
        break;
    case Icon::Prefab:
        Cube(draw, c, h * 0.9f, color, t, true);
        break;
    case Icon::Empty:
        draw.AddCircle(c, h * 0.45f, color, 16, t);
        draw.AddLine(Add(c, -h * 0.85f, 0), Add(c, -h * 0.45f, 0), color, t);
        draw.AddLine(Add(c, h * 0.45f, 0), Add(c, h * 0.85f, 0), color, t);
        break;
    case Icon::Camera:
        draw.AddRectFilled(Add(c, -h * 0.85f, -h * 0.45f), Add(c, h * 0.3f, h * 0.55f), color, 2.0f);
        draw.AddTriangleFilled(Add(c, h * 0.3f, h * 0.05f), Add(c, h * 0.9f, -h * 0.4f), Add(c, h * 0.9f, h * 0.5f), color);
        draw.AddCircleFilled(Add(c, -h * 0.55f, -h * 0.65f), h * 0.22f, color);
        draw.AddCircleFilled(Add(c, -h * 0.05f, -h * 0.65f), h * 0.22f, color);
        break;
    case Icon::Sun:
        draw.AddCircleFilled(c, h * 0.42f, color, 16);
        for (int i = 0; i < 8; ++i)
        {
            const float angle = static_cast<float>(i) * 0.785398f;
            const float x = std::cos(angle);
            const float y = std::sin(angle);
            draw.AddLine(Add(c, x * h * 0.62f, y * h * 0.62f), Add(c, x * h * 0.92f, y * h * 0.92f), color, t);
        }
        break;
    case Icon::Bulb:
        draw.AddCircleFilled(Add(c, 0, -h * 0.2f), h * 0.55f, color, 16);
        draw.AddRectFilled(Add(c, -h * 0.25f, h * 0.3f), Add(c, h * 0.25f, h * 0.85f), color, 1.0f);
        break;
    case Icon::Spot:
        draw.AddTriangleFilled(Add(c, 0, -h * 0.85f), Add(c, -h * 0.8f, h * 0.65f), Add(c, h * 0.8f, h * 0.65f), (color & 0x00ffffffu) | 0x90000000u);
        draw.AddCircleFilled(Add(c, 0, -h * 0.65f), h * 0.3f, color, 12);
        break;
    case Icon::Person:
        draw.AddCircleFilled(Add(c, 0, -h * 0.5f), h * 0.32f, color, 12);
        draw.PathArcTo(Add(c, 0, h * 0.85f), h * 0.75f, 3.14159f, 3.14159f * 2.0f, 16);
        draw.PathFillConvex(color);
        break;
    case Icon::Physics:
        draw.AddCircle(c, h * 0.8f, color, 20, t);
        Arrow(draw, c, Add(c, 0, h * 0.75f), color, t, h * 0.3f);
        draw.AddCircleFilled(c, h * 0.2f, color);
        break;
    case Icon::Component:
        draw.AddRect(Add(c, -h * 0.75f, -h * 0.75f), Add(c, h * 0.75f, h * 0.75f), color, 3.0f, 0, t);
        draw.AddRectFilled(Add(c, -h * 0.3f, -h * 0.3f), Add(c, h * 0.3f, h * 0.3f), color, 1.0f);
        break;
    case Icon::Eye:
    case Icon::EyeOff:
        draw.AddEllipse(c, ImVec2(h * 0.9f, h * 0.5f), color, 0.0f, 20, t);
        draw.AddCircleFilled(c, h * 0.25f, color, 12);
        if (icon == Icon::EyeOff)
        {
            draw.AddLine(Add(c, -h * 0.8f, h * 0.8f), Add(c, h * 0.8f, -h * 0.8f), color, t * 1.4f);
        }
        break;
    case Icon::Plus:
        draw.AddLine(Add(c, -h * 0.7f, 0), Add(c, h * 0.7f, 0), color, t * 1.5f);
        draw.AddLine(Add(c, 0, -h * 0.7f), Add(c, 0, h * 0.7f), color, t * 1.5f);
        break;
    case Icon::Search:
        draw.AddCircle(Add(c, -h * 0.15f, -h * 0.15f), h * 0.5f, color, 16, t * 1.3f);
        draw.AddLine(Add(c, h * 0.22f, h * 0.22f), Add(c, h * 0.75f, h * 0.75f), color, t * 1.8f);
        break;
    case Icon::Dots:
        for (int i = -1; i <= 1; ++i)
        {
            draw.AddCircleFilled(Add(c, 0, static_cast<float>(i) * h * 0.55f), h * 0.15f, color, 8);
        }
        break;
    case Icon::Info:
        draw.AddCircleFilled(c, h * 0.85f, color, 20);
        draw.AddRectFilled(Add(c, -h * 0.1f, -h * 0.15f), Add(c, h * 0.1f, h * 0.5f), IM_COL32(20, 22, 26, 255));
        draw.AddCircleFilled(Add(c, 0, -h * 0.42f), h * 0.12f, IM_COL32(20, 22, 26, 255));
        break;
    case Icon::Warning:
        draw.AddTriangleFilled(Add(c, 0, -h * 0.85f), Add(c, h * 0.9f, h * 0.75f), Add(c, -h * 0.9f, h * 0.75f), color);
        draw.AddRectFilled(Add(c, -h * 0.08f, -h * 0.35f), Add(c, h * 0.08f, h * 0.25f), IM_COL32(20, 22, 26, 255));
        draw.AddCircleFilled(Add(c, 0, h * 0.48f), h * 0.1f, IM_COL32(20, 22, 26, 255));
        break;
    case Icon::Error:
        draw.AddCircleFilled(c, h * 0.85f, color, 20);
        draw.AddLine(Add(c, -h * 0.35f, -h * 0.35f), Add(c, h * 0.35f, h * 0.35f), IM_COL32(20, 22, 26, 255), t * 1.6f);
        draw.AddLine(Add(c, -h * 0.35f, h * 0.35f), Add(c, h * 0.35f, -h * 0.35f), IM_COL32(20, 22, 26, 255), t * 1.6f);
        break;
    case Icon::Grid:
        for (int i = 0; i < 4; ++i)
        {
            const float o = -h * 0.8f + static_cast<float>(i) * h * 0.533f;
            draw.AddLine(Add(c, o, -h * 0.8f), Add(c, o, h * 0.8f), color, t);
            draw.AddLine(Add(c, -h * 0.8f, o), Add(c, h * 0.8f, o), color, t);
        }
        break;
    case Icon::Trash:
        draw.AddLine(Add(c, -h * 0.8f, -h * 0.55f), Add(c, h * 0.8f, -h * 0.55f), color, t);
        draw.AddRect(Add(c, -h * 0.25f, -h * 0.85f), Add(c, h * 0.25f, -h * 0.55f), color, 1.0f, 0, t);
        draw.AddRect(Add(c, -h * 0.6f, -h * 0.4f), Add(c, h * 0.6f, h * 0.85f), color, 2.0f, 0, t);
        break;
    case Icon::Refresh:
    {
        const float r = h * 0.7f;
        draw.PathArcTo(c, r, -3.14159f * 0.4f, 3.14159f * 1.3f, 20);
        draw.PathStroke(color, ImDrawFlags_None, t);
        const ImVec2 end(c.x + r * std::cos(-3.14159f * 0.4f), c.y + r * std::sin(-3.14159f * 0.4f));
        draw.AddTriangleFilled(Add(end, -h * 0.35f, -h * 0.2f), Add(end, h * 0.3f, -h * 0.05f), Add(end, -h * 0.05f, h * 0.35f), color);
        break;
    }
    }
}

void IconText(Icon icon, ImU32 color)
{
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    DrawIcon(*ImGui::GetWindowDrawList(), icon, ImVec2(position.x + size * 0.5f, position.y + size * 0.5f), size * 0.85f, color ? color : IconColor(icon));
    ImGui::SameLine();
}

bool IconButton(const char *id, Icon icon, bool active, const char *tooltip, float size)
{
    const float side = size > 0.0f ? size : ImGui::GetFrameHeight();
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    }
    const bool pressed = ImGui::Button(id, ImVec2(side, side));
    ImGui::PopStyleColor(2);
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    ImU32 color = active ? IM_COL32(255, 255, 255, 255) : Theme::Text;
    if (disabled)
    {
        color = Theme::TextDim;
    }
    DrawIcon(*ImGui::GetWindowDrawList(), icon, ImVec2((low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f), side * 0.55f, color);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

void Spinner(ImDrawList &draw, ImVec2 center, float radius, ImU32 color, double time)
{
    const float start = static_cast<float>(std::fmod(time * 6.0, 6.2831853));
    draw.PathArcTo(center, radius, start, start + 4.2f, 20);
    draw.PathStroke(color, ImDrawFlags_None, std::max(1.5f, radius * 0.3f));
}

} // namespace Geni::Editor
