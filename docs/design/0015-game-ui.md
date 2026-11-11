# 0015: Game UI

Milestone: M6 · Status: Done · Last updated: 2026-11-11

## Problem

The only UI is Dear ImGui's debug look (the overlay and the editor). The reference game
needs a title menu, a pause menu with volume settings, and a HUD (speed, prompts such as
"Press E to drive", messages), usable with a gamepad as well as the mouse, and callable
from Lua.

**Done** means a game-facing UI module draws styled text, panels, bars and menus anchored to
the screen, navigates menus with the `ui_*` input actions (keyboard and gamepad) and with
the mouse, scales with the window height, and has the same API in C++ and Lua.

## Approach

**An immediate-mode API of our own (`ui/GameUI.h`) drawn through Dear ImGui's draw lists.**
ImGui is already in the engine, already rendered by the RHI, and since 1.92 has dynamic
font atlases, so any text size renders crisply. The game UI uses only `ImDrawList` (shapes,
text) and the font atlas: no ImGui windows, widgets or navigation. Layout, hit testing,
focus and style are ours, so replacing the backend later (a dedicated 2D renderer for M8's
WebGPU or for performance) does not change the API.

- Coordinates are in a **virtual 1080-pixel-high canvas** scaled to the window, with anchors
  (top-left, centre, bottom-right, …) so a HUD sits at the same place at any size.
- **Primitives**: `Text`, `Panel` (rounded rectangle), `Bar` (progress), `Image` later.
- **Menus**: `BeginMenu(id, title)`, `Button(label)`, `Slider(label, value, min, max)`,
  `Toggle`, `EndMenu`. Items are laid out in a column; one item has focus, moved by
  `ui_up`/`ui_down`, activated with `ui_accept`, sliders changed with `ui_left`/`ui_right`,
  and the mouse focuses what it hovers. The menu remembers focus between frames by ID.
- **Style**: one `UiStyle` (font sizes, colours, padding), dark translucent panels and an
  accent colour; the font is Roboto from `thirdparty/imgui/misc/fonts`, copied next to
  each sample.
- Runs inside the ImGui frame the engine already opens (Application::Update to Render) and
  draws on the background list, under the debug overlay. Requires
  `EngineConfig::debugOverlay`; the reference game turns the overlay on but hidden (F1).

## Rejected

- **ImGui windows and widgets styled as a game UI.** Quick, but focus, gamepad navigation
  and window behaviour are the debug tool's, and the look leaks through.
- **RmlUi (HTML/CSS) or Noesis.** Capable, but a large dependency, its own renderer
  interface and a document format before the reference game has five screens.
- **Our own 2D renderer and font rasteriser now.** Needed eventually for M8, not for v0.1;
  the API is designed so it can be swapped in.

## To verify

- Text cost with a busy HUD (hundreds of glyphs) on mobile.
- Builds without `GENI_ENABLE_IMGUI`: every call is a no-op and the game still plays.
