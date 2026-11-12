# Changelog

## v0.1 (M6: gameplay systems)

The first version meant for other people: the reference game, **Geni City**, is
playable (`samples/city/city --play`). Walk around a streamed 4 km city, get into a car,
drive it, among pedestrians and traffic.

### Added

- **Input actions** (`input/InputActions.h`, docs/design/0009): named buttons and axes
  bound to keys, mouse buttons, mouse motion and gamepad buttons and sticks (GLFW's
  standard mapping), loaded from JSON, with edges, dead zones, groups, overrides and the
  last device used. `InputManager` tracks gamepads and previous-frame state.
- **Characters** (`CharacterControllerComponent` on Jolt's `CharacterVirtual`): walk,
  run, jump, stairs and slopes, pushes bodies; interpolated between physics steps.
- **Vehicles** (`VehicleComponent` on Jolt's `VehicleConstraint`): engine, automatic
  gearbox, differential, anti-roll bars, cylinder-cast suspension; wheel objects placed
  every frame.
- **Third-person camera** (`ThirdPersonCameraComponent`): orbits a target, follows
  vehicles, sphere-casts against walls.
- **Physics queries and motion**: `PhysicsManager::CastRay`, `CastSphere`, kinematic
  targets, velocities and impulses; `Component::FixedUpdate` before each physics step and
  `Component::LateUpdate` after every update.
- **Animation graphs** (`AnimatorComponent`, docs/design/0011): states, 1D blend spaces
  with phase sync, cross-fades, transitions on parameters, triggers and exit times, clip
  events; sampled on the job system.
- **Audio** (`audio/AudioSystem.h` on miniaudio, docs/design/0012): 2D and 3D sounds,
  streamed music with fades, master/music/effects volumes, `AudioSourceComponent`; runs
  without a device for tests and hidden runs.
- **Lua 5.4 scripting** (`ScriptComponent`, docs/design/0013): script classes with
  start/update/fixed_update/late_update/destroy, bindings for objects, every reflected
  component property, characters, vehicles, animators, agents, audio, physics queries,
  input, navigation and UI; hot reload that keeps state. Reference: docs/scripting.md.
- **Navigation** (`nav/NavigationSystem.h` on Recast/Detour, docs/design/0014): one
  navigation tile per streamed cell, built on the job system from static colliders;
  paths, nearest and random points; a DetourCrowd with `NavAgentComponent` (go to, wander).
- **Game UI** (`ui/GameUI.h`, docs/design/0015): anchored text, panels, bars and menus on
  a 1080-unit canvas, navigated with the gamepad, keyboard or mouse, drawn through Dear
  ImGui's draw lists.
- **Geni City** (`city --play`): title and pause menus, settings, HUD, a player
  mannequin with idle/walk/run/jump, a drivable car with engine sound, 24 wandering
  pedestrians and 10 traffic cars on the street grid, music and ambience, all in Lua
  (`samples/city/assets/scripts/`). `--autopilot` plays it as a test (`play_city`).
- Generated content: `tools/gen_game_assets.py` (the mannequin, four cars, sounds).
- Docs: getting-started guide, Lua API reference, contribution guidelines.

### Changed

- `InputManager` tracked only the first 256 GLFW key codes, so Escape, Shift and the
  function keys were never seen; it now tracks all of them, and the mouse delta adds up
  every motion event of a frame.
- `SkinnedMeshComponent` looked joints up by name across the whole scene, so every copy
  of a character was skinned to the first copy's skeleton; it now looks in its own model.
- Shader hot reload and the editor's shader compile treated a file time of 0 as "missing",
  but libstdc++'s file clock gives negative times (its epoch is in 2174): on Linux shaders
  never reloaded and the editor never compiled a new shader. Missing is now `INT64_MIN`.
- The editor brings the Assets tab to the front of its default layout explicitly; which
  tab won depended on the platform.
- `WorldStreamer` reports cells loading and unloading to listeners.
- `geni_add_sample` copies the UI font next to each sample.

### Known limits

- Pedestrians are not physical: cars drive through them.
- Traffic follows the metro world's street grid; it is scripted for that world.
- Navigation tiles are built at load time from colliders, not cooked offline.
- Audio files are read as they are (WAV/FLAC/MP3), not compressed per platform.
- The game UI needs Dear ImGui (`GENI_ENABLE_IMGUI`, on by default).
