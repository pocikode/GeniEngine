#include "ui/GameUI.h"

#include "core/Log.h"
#include "input/InputActions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <glm/geometric.hpp>

#if defined(GENI_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace Geni
{

#if defined(GENI_ENABLE_IMGUI)

namespace
{
ImU32 Color(const glm::vec4 &c)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
}

constexpr float kCanvasHeight = 1080.0f;
} // namespace

bool GameUI::Init(const std::string &fontPath)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        m_available = false;
        return false;
    }
    ImGuiIO &io = ImGui::GetIO();
    // The debug overlay keeps ImGui's own font: it must be the first one added.
    if (io.Fonts->Fonts.Size == 0)
    {
        io.Fonts->AddFontDefault();
    }
    std::error_code ec;
    if (!fontPath.empty() && std::filesystem::exists(fontPath, ec))
    {
        ImFontConfig config;
        config.OversampleH = 2;
        m_font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 32.0f, &config);
    }
    if (!m_font)
    {
        GENI_LOG_WARN("Game UI: font '%s' not found; using Dear ImGui's default font", fontPath.c_str());
        m_font = io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0] : nullptr;
    }
    m_available = m_font != nullptr;
    return m_available;
}

void GameUI::Shutdown()
{
    m_available = false;
    m_font = nullptr;
    m_menus.clear();
}

void GameUI::BeginFrame(const InputActions &actions, bool pointer)
{
    if (!m_available || ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }
    const ImGuiIO &io = ImGui::GetIO();
    const float height = std::max(1.0f, io.DisplaySize.y);
    m_scale = height / kCanvasHeight;
    m_canvas = glm::vec2(io.DisplaySize.x / m_scale, kCanvasHeight);
    const bool overImGuiWindow = io.WantCaptureMouse || !pointer;
    const glm::vec2 mouse(io.MousePos.x / m_scale, io.MousePos.y / m_scale);
    m_mouseMoved = glm::length(mouse - m_lastMouse) > 0.5f && io.MousePos.x > -1e5f;
    m_lastMouse = mouse;
    m_mouse = overImGuiWindow ? glm::vec2(-1e6f) : mouse;
    m_click = !overImGuiWindow && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    m_up = actions.WasPressed("ui_up");
    m_down = actions.WasPressed("ui_down");
    m_left = actions.WasPressed("ui_left");
    m_right = actions.WasPressed("ui_right");
    m_accept = actions.WasPressed("ui_accept");
}

glm::vec2 GameUI::Place(UiAnchor anchor, const glm::vec2 &offset, const glm::vec2 &size) const
{
    // Top-left corner of a box of `size` placed at `anchor` + `offset`.
    float x = 0.0f;
    float y = 0.0f;
    switch (anchor)
    {
    case UiAnchor::TopLeft:
    case UiAnchor::Left:
    case UiAnchor::BottomLeft:
        x = offset.x;
        break;
    case UiAnchor::Top:
    case UiAnchor::Center:
    case UiAnchor::Bottom:
        x = 0.5f * (m_canvas.x - size.x) + offset.x;
        break;
    case UiAnchor::TopRight:
    case UiAnchor::Right:
    case UiAnchor::BottomRight:
        x = m_canvas.x - size.x - offset.x;
        break;
    }
    switch (anchor)
    {
    case UiAnchor::TopLeft:
    case UiAnchor::Top:
    case UiAnchor::TopRight:
        y = offset.y;
        break;
    case UiAnchor::Left:
    case UiAnchor::Center:
    case UiAnchor::Right:
        y = 0.5f * (m_canvas.y - size.y) + offset.y;
        break;
    case UiAnchor::BottomLeft:
    case UiAnchor::Bottom:
    case UiAnchor::BottomRight:
        y = m_canvas.y - size.y - offset.y;
        break;
    }
    return glm::vec2(x, y);
}

float GameUI::MeasureText(const std::string &text, float size) const
{
    if (!m_available)
    {
        return 0.0f;
    }
    const ImFont *font = static_cast<const ImFont *>(m_font);
    const ImVec2 measured = const_cast<ImFont *>(font)->CalcTextSizeA(size * m_scale, 1e9f, 0.0f, text.c_str());
    return measured.x / m_scale;
}

void GameUI::Text(const std::string &text, UiAnchor anchor, const glm::vec2 &offset, float size, const glm::vec4 &color)
{
    if (!m_available || text.empty())
    {
        return;
    }
    ImFont *font = static_cast<ImFont *>(m_font);
    const ImVec2 measured = font->CalcTextSizeA(size * m_scale, 1e9f, 0.0f, text.c_str());
    const glm::vec2 box(measured.x / m_scale, measured.y / m_scale);
    const glm::vec2 at = Place(anchor, offset, box) * m_scale;
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    // A soft shadow keeps text readable over a bright sky.
    draw->AddText(font, size * m_scale, ImVec2(at.x + 2.0f * m_scale, at.y + 2.0f * m_scale),
                  Color(glm::vec4(0.0f, 0.0f, 0.0f, 0.55f * color.a)), text.c_str());
    draw->AddText(font, size * m_scale, ImVec2(at.x, at.y), Color(color), text.c_str());
}

void GameUI::Panel(UiAnchor anchor, const glm::vec2 &offset, const glm::vec2 &size, const glm::vec4 &color, float rounding)
{
    if (!m_available)
    {
        return;
    }
    const glm::vec2 at = Place(anchor, offset, size) * m_scale;
    const glm::vec2 end = at + size * m_scale;
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(at.x, at.y), ImVec2(end.x, end.y), Color(color), (rounding < 0.0f ? m_style.rounding : rounding) * m_scale
    );
}

void GameUI::Bar(UiAnchor anchor, const glm::vec2 &offset, const glm::vec2 &size, float fraction,
                 const glm::vec4 &color, const glm::vec4 &background)
{
    if (!m_available)
    {
        return;
    }
    const float r = 0.5f * size.y;
    Panel(anchor, offset, size, background, r);
    const glm::vec2 at = Place(anchor, offset, size) * m_scale;
    const float width = size.x * std::clamp(fraction, 0.0f, 1.0f) * m_scale;
    if (width > 1.0f)
    {
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(at.x, at.y), ImVec2(at.x + width, at.y + size.y * m_scale), Color(color), r * m_scale
        );
    }
}

void GameUI::Fade(float alpha, const glm::vec4 &color)
{
    if (!m_available || alpha <= 0.0f)
    {
        return;
    }
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), io.DisplaySize, Color(glm::vec4(color.r, color.g, color.b, std::min(alpha, 1.0f)))
    );
}

void GameUI::BeginMenu(const std::string &id, const std::string &title, float width)
{
    m_menu = id;
    m_menuTitle = title;
    m_menuWidth = width;
    m_items.clear();
    m_inMenu = true;
    MenuState &state = m_menus[id];
    if (state.itemCount > 0)
    {
        if (m_down)
        {
            state.focus = (state.focus + 1) % state.itemCount;
        }
        if (m_up)
        {
            state.focus = (state.focus + state.itemCount - 1) % state.itemCount;
        }
    }
}

int GameUI::AddItem(Item item, bool &activated, int &nudge)
{
    activated = false;
    nudge = 0;
    const int index = static_cast<int>(m_items.size());
    m_items.push_back(item);
    if (!m_available || !m_inMenu)
    {
        return index;
    }
    MenuState &state = m_menus[m_menu];
    // Layout from the previous frame's height (the column is centred).
    const float titleHeight = m_menuTitle.empty() ? 0.0f : m_style.titleSize + m_style.itemSpacing * 2.0f;
    const glm::vec2 box(m_menuWidth, state.height);
    const glm::vec2 corner = Place(UiAnchor::Center, glm::vec2(0.0f), box);
    const glm::vec2 itemAt(
        corner.x + m_style.padding,
        corner.y + m_style.padding + titleHeight + static_cast<float>(index) * (m_style.itemHeight + m_style.itemSpacing)
    );
    const glm::vec2 itemSize(m_menuWidth - 2.0f * m_style.padding, m_style.itemHeight);
    const bool hovered = m_mouse.x >= itemAt.x && m_mouse.x <= itemAt.x + itemSize.x && m_mouse.y >= itemAt.y &&
                         m_mouse.y <= itemAt.y + itemSize.y;
    if (item.kind == 3)
    {
        return index;
    }
    if (hovered && m_mouseMoved)
    {
        state.focus = index;
    }
    if (state.focus != index)
    {
        return index;
    }
    if (m_accept || (hovered && m_click))
    {
        activated = true;
        if (item.kind == 1 && hovered && m_click)
        {
            // A click on a slider moves it towards the clicked side.
            nudge = m_mouse.x < itemAt.x + 0.5f * itemSize.x ? -1 : 1;
            activated = false;
        }
    }
    if (m_left)
    {
        nudge = -1;
    }
    if (m_right)
    {
        nudge = 1;
    }
    return index;
}

bool GameUI::Button(const std::string &label)
{
    Item item;
    item.label = label;
    bool activated = false;
    int nudge = 0;
    AddItem(item, activated, nudge);
    return activated;
}

bool GameUI::Slider(const std::string &label, float &value, float minValue, float maxValue, float step)
{
    Item item;
    item.label = label;
    item.kind = 1;
    bool activated = false;
    int nudge = 0;
    const int index = AddItem(item, activated, nudge);
    bool changed = false;
    if (nudge != 0)
    {
        const float next = std::clamp(value + static_cast<float>(nudge) * step, minValue, maxValue);
        changed = next != value;
        value = next;
    }
    m_items[static_cast<size_t>(index)].fraction = maxValue > minValue ? (value - minValue) / (maxValue - minValue) : 0.0f;
    return changed;
}

bool GameUI::Toggle(const std::string &label, bool &value)
{
    Item item;
    item.label = label;
    item.kind = 2;
    bool activated = false;
    int nudge = 0;
    const int index = AddItem(item, activated, nudge);
    if (activated || nudge != 0)
    {
        value = !value;
    }
    m_items[static_cast<size_t>(index)].on = value;
    return activated || nudge != 0;
}

void GameUI::Label(const std::string &text)
{
    Item item;
    item.label = text;
    item.kind = 3;
    bool activated = false;
    int nudge = 0;
    AddItem(item, activated, nudge);
}

void GameUI::EndMenu()
{
    if (!m_inMenu)
    {
        return;
    }
    m_inMenu = false;
    MenuState &state = m_menus[m_menu];
    state.itemCount = static_cast<int>(m_items.size());
    // Never rest on a label.
    for (int guard = 0; guard < state.itemCount && m_items[static_cast<size_t>(state.focus % std::max(1, state.itemCount))].kind == 3; ++guard)
    {
        state.focus = (state.focus + (m_up ? state.itemCount - 1 : 1)) % state.itemCount;
    }
    state.focus = state.itemCount > 0 ? std::clamp(state.focus, 0, state.itemCount - 1) : 0;
    const float titleHeight = m_menuTitle.empty() ? 0.0f : m_style.titleSize + m_style.itemSpacing * 2.0f;
    state.height = 2.0f * m_style.padding + titleHeight +
                   static_cast<float>(m_items.size()) * (m_style.itemHeight + m_style.itemSpacing) - m_style.itemSpacing;
    if (!m_available)
    {
        return;
    }

    const glm::vec2 box(m_menuWidth, state.height);
    const glm::vec2 corner = Place(UiAnchor::Center, glm::vec2(0.0f), box);
    Panel(UiAnchor::Center, glm::vec2(0.0f), box, m_style.panel);
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    ImFont *font = static_cast<ImFont *>(m_font);
    auto text = [&](const std::string &value, glm::vec2 at, float size, const glm::vec4 &color, float align) {
        const ImVec2 measured = font->CalcTextSizeA(size * m_scale, 1e9f, 0.0f, value.c_str());
        const glm::vec2 pixel = at * m_scale - glm::vec2(measured.x * align, measured.y * 0.5f);
        draw->AddText(font, size * m_scale, ImVec2(pixel.x, pixel.y), Color(color), value.c_str());
    };
    if (!m_menuTitle.empty())
    {
        text(m_menuTitle, glm::vec2(corner.x + 0.5f * m_menuWidth, corner.y + m_style.padding + 0.5f * m_style.titleSize),
             m_style.titleSize, m_style.accent, 0.5f);
    }
    const float titleBlock = titleHeight;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const Item &item = m_items[i];
        const glm::vec2 at(
            corner.x + m_style.padding,
            corner.y + m_style.padding + titleBlock + static_cast<float>(i) * (m_style.itemHeight + m_style.itemSpacing)
        );
        const glm::vec2 size(m_menuWidth - 2.0f * m_style.padding, m_style.itemHeight);
        const float midY = at.y + 0.5f * size.y;
        if (item.kind == 3)
        {
            text(item.label, glm::vec2(at.x + 0.5f * size.x, midY), m_style.itemSize * 0.8f, m_style.textDim, 0.5f);
            continue;
        }
        const bool focused = static_cast<int>(i) == state.focus;
        draw->AddRectFilled(
            ImVec2(at.x * m_scale, at.y * m_scale), ImVec2((at.x + size.x) * m_scale, (at.y + size.y) * m_scale),
            Color(focused ? m_style.itemFocused : m_style.item), m_style.rounding * 0.7f * m_scale
        );
        const glm::vec4 color = focused ? m_style.itemFocusedText : m_style.text;
        if (item.kind == 0)
        {
            text(item.label, glm::vec2(at.x + 0.5f * size.x, midY), m_style.itemSize, color, 0.5f);
            continue;
        }
        text(item.label, glm::vec2(at.x + 24.0f, midY), m_style.itemSize, color, 0.0f);
        const float right = at.x + size.x - 24.0f;
        if (item.kind == 1)
        {
            // A small track with a fill, and the value as a percentage.
            const float trackWidth = 160.0f;
            const glm::vec2 track(right - trackWidth, midY - 5.0f);
            draw->AddRectFilled(
                ImVec2(track.x * m_scale, track.y * m_scale),
                ImVec2((track.x + trackWidth) * m_scale, (track.y + 10.0f) * m_scale),
                Color(glm::vec4(color.r, color.g, color.b, 0.25f)), 5.0f * m_scale
            );
            draw->AddRectFilled(
                ImVec2(track.x * m_scale, track.y * m_scale),
                ImVec2((track.x + trackWidth * item.fraction) * m_scale, (track.y + 10.0f) * m_scale), Color(color),
                5.0f * m_scale
            );
            char percent[16];
            std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(std::lround(item.fraction * 100.0f)));
            text(percent, glm::vec2(track.x - 20.0f, midY), m_style.itemSize * 0.8f, color, 1.0f);
        }
        else
        {
            text(item.on ? "On" : "Off", glm::vec2(right, midY), m_style.itemSize, color, 1.0f);
        }
    }
}

int GameUI::GetFocus(const std::string &menu) const
{
    auto it = m_menus.find(menu);
    return it == m_menus.end() ? 0 : it->second.focus;
}

void GameUI::SetFocus(const std::string &menu, int index)
{
    m_menus[menu].focus = std::max(0, index);
}

#else // GENI_ENABLE_IMGUI

bool GameUI::Init(const std::string &)
{
    return false;
}
void GameUI::Shutdown()
{
}
void GameUI::BeginFrame(const InputActions &, bool)
{
}
glm::vec2 GameUI::Place(UiAnchor, const glm::vec2 &offset, const glm::vec2 &) const
{
    return offset;
}
float GameUI::MeasureText(const std::string &, float) const
{
    return 0.0f;
}
void GameUI::Text(const std::string &, UiAnchor, const glm::vec2 &, float, const glm::vec4 &)
{
}
void GameUI::Panel(UiAnchor, const glm::vec2 &, const glm::vec2 &, const glm::vec4 &, float)
{
}
void GameUI::Bar(UiAnchor, const glm::vec2 &, const glm::vec2 &, float, const glm::vec4 &, const glm::vec4 &)
{
}
void GameUI::Fade(float, const glm::vec4 &)
{
}
void GameUI::BeginMenu(const std::string &, const std::string &, float)
{
}
int GameUI::AddItem(Item, bool &activated, int &nudge)
{
    activated = false;
    nudge = 0;
    return 0;
}
bool GameUI::Button(const std::string &)
{
    return false;
}
bool GameUI::Slider(const std::string &, float &, float, float, float)
{
    return false;
}
bool GameUI::Toggle(const std::string &, bool &)
{
    return false;
}
void GameUI::Label(const std::string &)
{
}
void GameUI::EndMenu()
{
}
int GameUI::GetFocus(const std::string &) const
{
    return 0;
}
void GameUI::SetFocus(const std::string &, int)
{
}

#endif // GENI_ENABLE_IMGUI

} // namespace Geni
