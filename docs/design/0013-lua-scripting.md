# 0013: Lua scripting

Milestone: M6 · Status: Done · Last updated: 2026-11-11

## Problem

All behaviour is C++: a game built on the engine must be compiled with it, rebuilt for
every tweak, and the editor's play mode can only run components that already exist. The
reference game's rules (getting into a car, traffic, menus) are exactly the code that
changes most and should not need a C++ build.

**Done** means a `ScriptComponent` runs a Lua file attached to an object, with start,
update, fixed-update and destroy hooks; scripts can find and spawn objects, read and write
transforms and any reflected component property, call the gameplay components (character,
vehicle, animator, audio, navigation), read input actions, ray-cast and draw game UI;
script errors are reported with a traceback and do not crash the game; and an edited
script reloads while the game runs, keeping its state.

## Approach

**Lua 5.4** (submodule `thirdparty/lua`, v5.4.9), compiled **as C++** into the static
library `geni_lua`, so a Lua error unwinds with an exception and C++ destructors in
bindings run. Headers are included without `extern "C"`. Lua is the default the roadmap
named; nothing seen since argues for changing it.

**One state per engine** (`script/ScriptSystem`, owned by `Engine`), created at init with
the safe standard libraries only (`base`, `table`, `string`, `math`, `utf8`, `coroutine`,
and `os.clock`/`os.time`; no `io`, no `os.execute`, no `debug`). `require "name"` loads
`scripts/name.lua` from the assets folder through `FileSystem`.

**Script files return a class table:**

```lua
local Door = {}
function Door:start() self.open = false end
function Door:update(dt) if input.pressed("interact") then self.open = not self.open end end
return Door
```

Each `ScriptComponent` gets an instance table whose metatable indexes the class, with
`self.object` set to its object. Hooks are optional. A script that raises an error is
logged with its traceback and stops getting calls until it is reloaded.

**Bindings** are hand-written against the C API (no binding library):

- `GameObject` is a userdata holding the scene and the object's 64-bit ID, looked up on
  every call, so a script holding a destroyed object gets `nil`/an error, never a dangling
  pointer. Methods: name, active, position (scene) and world position (double), rotation
  (yaw helper too), forward, parent, children, find, destroy, `get(type)`,
  `add(type, props)`.
- **Components** are proxies (object ID plus type name). Any reflected property reads and
  writes by name (`car.mass = 1500`), converting through the same JSON the scene files use.
  Types with behaviour add methods: `CharacterControllerComponent:move(x, z, sprint)`,
  `VehicleComponent:set_input(...)`, `AnimatorComponent:set(name, v)`, `:events()`,
  `NavAgentComponent:go_to(...)`, `AudioSourceComponent:play()`.
- Libraries: `log`, `input` (actions, 0009), `scene` (find, spawn a cooked scene or
  prefab, main camera, to/from world), `physics` (ray cast), `audio` (play, music,
  volumes), `nav` (random point, nearest point), `ui` (0015), `time`, `vec3` (a small
  vector type written in Lua).

**Hot reload.** The system remembers each loaded script file's modification time and
checks twice a second (a development option). On change it re-runs the file and swaps
the new class into every instance's metatable; instance tables, and with them the state,
stay.

## Rejected

- **LuaJIT.** Faster, but stuck at Lua 5.1 semantics, unsupported on some mobile targets
  without the JIT, and not maintained upstream at the pace we need.
- **sol2.** Pleasant, but a large template-heavy header that slows every build that
  includes it and hides the stack discipline we want visible in the bindings.
- **C# (Mono/CoreCLR) or WebAssembly.** Far bigger runtimes and toolchains; reconsider
  only if users ask (open decision closed with Lua for v0.1).
- **One Lua state per script.** Isolation is not worth the memory and the cost of
  passing objects between states.

## To verify

- Per-frame cost of ~100 script instances on the Steam Deck (target: under 1 ms).
- Whether scripts need a sandboxed environment per instance once third-party content
  exists.
