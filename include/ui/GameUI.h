#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Geni
{

class InputActions;

// Where an element is placed from: a corner, an edge's middle or the centre of the
// screen. The offset moves it inwards from there (right and down are positive).
enum class UiAnchor
{
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight
};

struct UiStyle
{
    glm::vec4 text{0.96f, 0.96f, 0.97f, 1.0f};
    glm::vec4 textDim{0.70f, 0.72f, 0.76f, 1.0f};
    glm::vec4 panel{0.05f, 0.06f, 0.08f, 0.78f};
    glm::vec4 accent{1.00f, 0.71f, 0.20f, 1.0f};
    glm::vec4 item{0.14f, 0.15f, 0.18f, 0.92f};
    glm::vec4 itemFocused{1.00f, 0.71f, 0.20f, 1.0f};
    glm::vec4 itemFocusedText{0.06f, 0.06f, 0.07f, 1.0f};
    float titleSize = 64.0f;
    float itemSize = 34.0f;
    float itemHeight = 64.0f;
    float itemSpacing = 12.0f;
    float padding = 36.0f;
    float rounding = 14.0f;
};

// The game's UI (docs/design/0015): immediate-mode text, panels, bars and menus on a
// virtual canvas 1080 units high (the width follows the window's aspect ratio), drawn
// with Dear ImGui's draw lists under every ImGui window. Menus are navigated with the
// ui_up/ui_down/ui_left/ui_right/ui_accept actions and with the mouse. Every call is a
// no-op when the engine runs without Dear ImGui.
class GameUI
{
  public:
    // `fontPath`: a TrueType font (empty: Dear ImGui's default font).
    bool Init(const std::string &fontPath);
    void Shutdown();
    bool IsAvailable() const
    {
        return m_available;
    }

    // Engine calls this once per frame before Application::Update. Without
    // `pointer` the mouse neither hovers nor clicks (a hidden or unfocused window).
    void BeginFrame(const InputActions &actions, bool pointer = true);

    // Canvas size in UI units (height is always 1080).
    glm::vec2 GetCanvasSize() const
    {
        return m_canvas;
    }

    void Text(const std::string &text, UiAnchor anchor, const glm::vec2 &offset, float size = 32.0f,
              const glm::vec4 &color = glm::vec4(1.0f));
    void Panel(UiAnchor anchor, const glm::vec2 &offset, const glm::vec2 &size, const glm::vec4 &color,
               float rounding = -1.0f);
    void Bar(UiAnchor anchor, const glm::vec2 &offset, const glm::vec2 &size, float fraction,
             const glm::vec4 &color, const glm::vec4 &background);
    // Darkens (or fades) the whole screen.
    void Fade(float alpha, const glm::vec4 &color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    // Width of `text` at `size`, in UI units.
    float MeasureText(const std::string &text, float size) const;

    // A vertical menu centred on the screen. Items are laid out in call order; the
    // focused one is highlighted. Activation is reported by the item call.
    void BeginMenu(const std::string &id, const std::string &title = std::string(), float width = 560.0f);
    bool Button(const std::string &label);
    // Changes `value` by `step` with ui_left/ui_right or a click on either half.
    bool Slider(const std::string &label, float &value, float minValue, float maxValue, float step = 0.1f);
    bool Toggle(const std::string &label, bool &value);
    // A line of text that cannot be focused.
    void Label(const std::string &text);
    void EndMenu();
    // Index of the focused item of a menu (for tests and scripted runs).
    int GetFocus(const std::string &menu) const;
    void SetFocus(const std::string &menu, int index);

    UiStyle &GetStyle()
    {
        return m_style;
    }

  private:
    struct MenuState
    {
        int focus = 0;
        int itemCount = 0;
        float height = 0.0f;
    };
    struct Item
    {
        std::string label;
        int kind = 0; // 0 button, 1 slider, 2 toggle, 3 label
        float fraction = 0.0f;
        bool on = false;
    };

    glm::vec2 Place(UiAnchor anchor, const glm::vec2 &offset, const glm::vec2 &size) const;
    // Shared by Button, Slider and Toggle: returns the item's index, and whether it was
    // activated (accept or click) and nudged left or right this frame.
    int AddItem(Item item, bool &activated, int &nudge);

    bool m_available = false;
    void *m_font = nullptr; // ImFont*
    UiStyle m_style;
    glm::vec2 m_canvas{1920.0f, 1080.0f};
    float m_scale = 1.0f;
    glm::vec2 m_mouse{-1.0f};
    bool m_mouseMoved = false;
    bool m_click = false;
    bool m_up = false;
    bool m_down = false;
    bool m_left = false;
    bool m_right = false;
    bool m_accept = false;
    glm::vec2 m_lastMouse{-1.0f};

    std::unordered_map<std::string, MenuState> m_menus;
    std::string m_menu;
    std::string m_menuTitle;
    float m_menuWidth = 0.0f;
    std::vector<Item> m_items;
    bool m_inMenu = false;
};

} // namespace Geni
