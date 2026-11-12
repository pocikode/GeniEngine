# 0009: Input actions and gamepads

Milestone: M6 · Status: Done · Last updated: 2026-11-11

## Problem

`input/InputManager` holds raw GLFW key and mouse state, and every component reads
GLFW key codes directly (`PlayerControllerComponent`, `FlyCameraComponent`). There is no
gamepad support, no way to tell "pressed this frame" from "held", and no way to rebind a
control without editing C++. The reference game needs the same "jump" or "accelerate"
from a keyboard, a mouse and a gamepad, and the scripting layer (0013) needs a name it
can ask about rather than a key code.

**Done** means gameplay code asks for named actions (`jump`, `move_x`, `throttle`), each
bound to any mix of keys, mouse buttons, mouse motion, gamepad buttons and gamepad axes;
bindings load from a JSON file in the game's assets; gamepads are polled every frame;
and the whole path is testable without a window by injecting device state.

## Approach

**Raw state stays in `InputManager`**, which gains a previous-frame copy of keys and mouse
buttons (`WasKeyPressed` / `WasKeyReleased`), the mouse delta, and up to four gamepads in
GLFW's standard layout (`GamepadState`: 15 buttons, 6 axes, from `glfwGetGamepadState`, so
GLFW's built-in SDL mapping database names the buttons). `Engine::Tick` polls gamepads
right after `glfwPollEvents`, except in a hidden window (a test or a capture), so a
controller on the desk cannot steer a test. Tests call `SetGamepadState` instead.

**Actions live in `input/InputActions.h` (`InputActions`, owned by the engine).** An action
is a name, a kind (`Button` or `Axis`) and a list of bindings. A binding is a control
string plus a scale:

| Control | Example | Value |
|---|---|---|
| `key:<name>` | `key:W`, `key:Space`, `key:LeftShift` | 0 or 1 |
| `mouse:<button>` | `mouse:Left` | 0 or 1 |
| `mouse:DeltaX`, `mouse:DeltaY`, `mouse:Wheel` | | pixels or notches this frame |
| `pad:<button>` | `pad:A`, `pad:RB`, `pad:DpadUp` | 0 or 1 |
| `pad:<axis>` | `pad:LeftX`, `pad:RT` | −1..1 (triggers 0..1), dead zone applied |

An axis is the sum of `value × scale` over its bindings, clamped to −1..1 unless the
action says `"clamp": false` (mouse look). A button is down when any binding is above
`pressThreshold` (0.5), so an analogue trigger can be a button. Pressed and released are
edges against the previous frame. The engine updates actions once per frame after polling;
gameplay that runs in fixed steps latches a pressed edge until a step consumes it (the
character's jump does this).

The engine starts with a default map (`InputActions::LoadDefaults`: walking, looking,
driving, menus), and a game replaces or extends it from JSON:

```json
{ "actions": {
    "jump":     { "type": "button", "bindings": ["key:Space", "pad:A"] },
    "move_x":   { "type": "axis", "bindings": [{"control": "key:D"}, {"control": "key:A", "scale": -1}, "pad:LeftX"] },
    "look_x":   { "type": "axis", "clamp": false, "bindings": [{"control": "mouse:DeltaX", "scale": 0.1}, {"control": "pad:RightX", "scale": 3}] }
} }
```

`InputActions` also tracks the last device that produced input (keyboard and mouse, or
gamepad; a press past the threshold switches it, stick noise does not) so UI prompts can
show the right glyph text. While a Dear ImGui text field or window has the keyboard (in a
tool), keys and mouse do not feed actions. `SetOverride(action, value)` forces an action
regardless of devices: replays, AI drivers and the city's scripted test run use the same
path as a player.

## Rejected

- **Callbacks per action** (Unreal's enhanced input style). Polling by name is simpler to
  bind to Lua and to test, and the game loop already runs once per frame.
- **SDL for input.** GLFW already gives gamepads with SDL's mapping database; a second
  windowing library for input alone is not worth it until mobile (M7) needs touch.
- **Contexts and priorities** (menu map over gameplay map). The reference game toggles
  `InputActions::SetGroupEnabled("gameplay" / "ui")` instead; full contexts can wait for users.

## To verify

- Gamepad mappings on the Steam Deck under Steam Input, which presents an Xbox pad.
- Whether one frame of latency on mouse look is noticeable at 30 fps (mobile).
