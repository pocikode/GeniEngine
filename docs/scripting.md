# Lua API reference

GeniEngine runs Lua 5.4 (design: [0013](design/0013-lua-scripting.md)). A script file is
attached to an object with a `ScriptComponent`; it returns a class table, and each
component gets an instance of it:

```lua
local Spinner = {}

function Spinner:start()             -- once, before the first update
  self.speed = self.speed or 90       -- fields can come from the component's `params` JSON
end

function Spinner:update(dt)           -- every frame
  self.object:set_yaw(self.object:yaw() + self.speed * dt)
end

function Spinner:fixed_update(dt)     -- every fixed step, before physics (60 Hz)
end

function Spinner:late_update(dt)      -- every frame, after every object's update
end

function Spinner:destroy()            -- when the object or the component goes
end

return Spinner
```

All hooks are optional. `self.object` is the object the script is on. An error in a hook
is logged with a traceback and stops that component until the file is saved again. Saved
files reload while the game runs; instances keep their fields.

`require("name")` loads `scripts/name.lua` (dots are folders) once. The standard
libraries are `string`, `table`, `math`, `utf8`, `coroutine` and `os.clock`/`os.time`;
there is no file or process access.

Positions are **scene coordinates** unless a name says *world*: in a streamed world the
scene's origin moves every kilometre, so keep long-lived positions in world coordinates
(`object:world_coords()`, `scene.to_world`, `scene.to_scene`).

## vec3

`vec3(x, y, z)` makes a vector; `v.x`, `v.y`, `v.z` read it. Operators: `+`, `-`, unary
`-`, `*` (by a number, or component-wise), `/` (by a number), `==`, `tostring`.

| Function | Returns |
|---|---|
| `v:length()`, `v:length2()` | length, squared length |
| `v:normalized()` | unit vector (zero stays zero) |
| `v:flat()` | the vector with `y = 0` |
| `v:copy()` | a copy |
| `vec3.dot(a, b)`, `vec3.cross(a, b)` | |
| `vec3.distance(a, b)`, `vec3.lerp(a, b, t)` | |

`math.clamp(v, lo, hi)`, `math.lerp(a, b, t)` and `math.sign(v)` are added to `math`.
Rotations are tables `{x, y, z, w}`; colours are `{r, g, b, a}` or `{1, 0.5, 0, 1}`.

## Objects

Handles are safe to keep: a handle to a destroyed object reports `valid() == false` and
any other call raises an error.

| Method | |
|---|---|
| `o:valid()`, `o:id()` | |
| `o:name()`, `o:set_name(s)` | |
| `o:active()`, `o:set_active(b)` | an inactive object and its children do nothing |
| `o:destroy()` | at the end of the frame |
| `o:parent()`, `o:set_parent(p or nil)`, `o:children()`, `o:find(name)` | `find` searches the subtree |
| `o:position()`, `o:set_position(v)` | local to the parent |
| `o:world_position()`, `o:set_world_position(v)` | scene coordinates |
| `o:world_coords()` | world coordinates (double precision) |
| `o:rotation()`, `o:set_rotation(q)` | |
| `o:yaw()`, `o:set_yaw(degrees)` | heading; 0 faces -Z |
| `o:forward()`, `o:right()` | world directions |
| `o:scale()`, `o:set_scale(v or number)` | |
| `o:get(type)` | a component, or `nil` |
| `o:add(type, {property = value, ...})` | adds and returns a component |
| `o:remove(type)` | |

## Components

`o:get("LightComponent")` returns a proxy. Every reflected property (the fields the
inspector shows) reads and writes by name: `light.range = 12`, `print(car.mass)`.
`c:object()`, `c:type()` and `c:valid()` work on every component. Types with behaviour
add methods:

| Component | Methods |
|---|---|
| `CharacterControllerComponent` | `move(direction, sprint)`, `jump()`, `teleport(position)`, `set_enabled(b)`, `enabled()`, `speed()`, `grounded()`, `velocity()` |
| `VehicleComponent` | `set_input(forward, steer, brake, handbrake)`, `speed()` (m/s, negative in reverse), `rpm()`, `gear()`, `teleport(position, yaw)`, `velocity()` |
| `AnimatorComponent` | `set(name, number or bool)`, `get(name)`, `trigger(name)`, `state()`, `play(state, fade)`, `events()` (names fired since the last call) |
| `NavAgentComponent` | `go_to(position)`, `stop()`, `arrived()`, `on_mesh()`, `speed()` |
| `AudioSourceComponent` | `play()`, `stop(fade)`, `playing()` |
| `ThirdPersonCameraComponent` | `set_target(object)`, `target()`, `snap()` (behind the target now) |
| `PhysicsComponent` | `velocity()`, `set_velocity(v)`, `impulse(v)` |
| `SpriteAnimatorComponent` | `play(first, count, fps, loop)` (the same clip again keeps its place, so call it every frame), `stop()`, `finished()` |
| `TilemapComponent` | `tile(column, row)` (-1 when empty), `set_tile(column, row, tile)`, `solid(column, row)`, `cell_at(position)` → column, row, `cell_center(column, row)` → vec3 |
| `ScriptComponent` | `instance()`: the other script's instance table |

`TimeOfDayComponent` ([0018](design/0018-time-of-day.md)) has no methods; its properties
are the clock: `scene.find("Sun"):get("TimeOfDayComponent").time = 21.5` jumps to 21:30,
`dayLengthMinutes = 0` stops it.

## Libraries

**scene**: `find(name)`, `get(id)`, `create(name, parent)`, `spawn(path, position, yaw)`
(a cooked model such as `"models/car.gltf"`, or a `.gprefab`), `main_camera()`,
`set_main_camera(o)`, `to_world(v)`, `to_scene(v)`, `origin()`.

**input** ([0009](design/0009-input-actions.md)): `down(action)`, `pressed(action)`,
`released(action)`, `value(action)`, `vector(x_action, y_action)` (two numbers, length at
most 1), `device()` (`"keyboard"` or `"gamepad"`), `set_group(name, enabled)`,
`capture_cursor(b)`, `override(action, value)` (forces an action; `override(action)`
releases it, `override()` releases all).

**physics**: `raycast(origin, direction, distance, layers, ignore)` and
`spherecast(origin, radius, direction, distance, layers, ignore)` return `nil` or
`{position, normal, distance, object}`. `layers` is `"static"`, `"moving"` or `"all"`;
`ignore` is an object whose body is skipped.

**audio** ([0012](design/0012-audio.md)): `play(path, {volume, pitch, loop, position,
min_distance, max_distance})` returns a sound id (a `position` makes it 3D); `stop(id,
fade)`, `playing(id)`, `move(id, position)`, `set_pitch(id, p)`, `set_sound_volume(id, v)`;
`music(path, fade, volume)`, `stop_music(fade)`; `set_volume(bus, v)` and `volume(bus)`
for `"master"`, `"music"` and `"effects"`.

**nav** ([0014](design/0014-navigation.md)): `ready()`, `random_point(center, radius)`,
`nearest(position, radius)`, `path(from, to)` (corners and whether it reaches).

**ui** ([0015](design/0015-game-ui.md)): the canvas is 1080 units high. Anchors are
`"top_left"`, `"top"`, `"top_right"`, `"left"`, `"center"`, `"right"`, `"bottom_left"`,
`"bottom"`, `"bottom_right"`; offsets move inwards from them.

| Function | |
|---|---|
| `text(text, anchor, x, y, size, color)` | |
| `panel(anchor, x, y, w, h, color, rounding)` | |
| `bar(anchor, x, y, w, h, fraction, color, background)` | |
| `fade(alpha, color)` | the whole screen |
| `measure(text, size)`, `canvas()` | width; canvas width and height |
| `begin_menu(id, title, width)` ... `end_menu()` | a centred menu navigated with `ui_*` actions and the mouse |
| `button(label)` | true when activated |
| `slider(label, value, min, max, step)` | new value, changed |
| `toggle(label, value)` | new value, changed |
| `label(text)` | a line that cannot be focused |
| `focus(menu)`, `set_focus(menu, index)`, `accent()` | |

**time**: `now()` (seconds since start), `frame()`.
**game**: `quit()`, `screenshot(path)`, `set_paused(b)` (stops physics, fixed updates and
the crowd).
**log**: `info(...)`, `warn(...)`, `error(...)`; `print` is `log.info`.
