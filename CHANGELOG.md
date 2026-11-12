# Changelog

## Unreleased (v0.2, desktop beta)

Follows the [API stability policy](docs/api-policy.md) from this release on.

### Added

- **2D** (docs/design/0017): `SpriteComponent` (texture, size, pivot, sRGB tint, atlas
  frames, flips, sort layer, linear or nearest filtering), `SpriteAnimatorComponent`
  (frame clips at a rate, looping or one-shot) and `TilemapComponent` (a tileset grid,
  tiles as CSV text, drawn in 16 x 16 chunks that are culled, static colliders for the
  solid tiles merged into runs). Sprites are drawn in the scene pass after opaque meshes,
  sorted by layer and depth and batched by texture (10,000 sprites in 8 draws at 8.6 ms
  per frame on an Apple M4, release, 1280x720). `RenderQueue::SubmitSprites` takes quads
  directly. Lua: `play`/`stop`/`finished` on sprite animators, `tile`/`set_tile`/`solid`/
  `cell_at`/`cell_center` on tilemaps.
- **Orthographic cameras**: `CameraComponent` properties `projection` and `orthoSize`.
- **Physics in a plane**: `PhysicsComponent` property `plane2D` (Jolt `Plane2D`:
  move in X and Y, rotate about Z), `RigidBody::SetPlane2D`.
- **Colours as painted**: `RendererConfig::tonemapper` (`Tonemapper::Aces` or `None`, which
  encodes with the exact sRGB curve), `RendererConfig::sky` and `clearColor`, and
  `Renderer::SetTonemapper`, `SetExposure`, `SetBackground` to change them between frames.
- **Sprite textures in the cooker**: `"usage": "sprite"` in a `.meta` keeps an image as
  RGBA8 sRGB with one mip level and no BC/ASTC variants.
- **Geni Plains** (`samples/platformer`): a 2D platformer with a tilemap level, crates,
  coins, parallax layers and a HUD; tests `play_platformer`, `screenshot_platformer`,
  `benchmark_sprites` and `editor_smoke_2d`. `tools/gen_2d_assets.py` draws its art.
- **Packaging** (docs/design/0016): `geni_add_package(<game>)` and the `package_<game>`
  targets make a macOS app bundle (GLFW, the Vulkan loader and MoltenVK inside, signed ad
  hoc), a Windows folder with its DLLs, or a Linux folder with `lib/`, each also archived.
  `cmake --workflow --preset package` packages Geni City and Geni Plains; CI uploads the
  archives. `FileSystem::GetResourcesFolder` and `FileSystem::IsPackaged`; tests
  `package_layout` and `package_run`. `GENI_STATIC_RUNTIME` links libstdc++ statically on
  Linux.
- **Versioning**: `core/Version.h` (`GENI_VERSION_STRING`, `GENI_VERSION_NUMBER`,
  `GENI_MAKE_VERSION`, `GENI_DEPRECATED`); CMake reads the version from it.
- `docs/api-policy.md`: semantic versioning, what is public API, deprecation and changelog
  rules.

### Changed

- Scene files saved by this version carry the new reflected properties (`orthoSize` and
  `projection` on cameras, `plane2D` on physics components). Older files load unchanged;
  the checked-in scenes and `tools/gen_assets.py` were updated so an editor save does not
  touch every file.
- `FileSystem::GetCookedFolder` and the default assets folder are `assets/` in the
  resources folder, which is the executable's folder everywhere except inside a macOS
  bundle (`Contents/Resources`). In a packaged build the build-time `ASSETS_ROOT` is ignored.
- macOS: GLFW no longer changes the working directory to `Contents/Resources` when the
  game runs from a bundle, so relative paths (screenshots, logs, crash reports) are
  relative to where the game was started.
- Shader contract: `uFrame.fogColor.w` is the tonemapper (0 ACES, 1 none); it was unused.
- `FrameStats::drawCalls` counts sprite batches as well as mesh draws.
- Vulkan: on the "Apple Paravirtual device" (GitHub's macOS VMs) every index buffer gets a
  dedicated allocation. Index buffers placed deep inside a shared memory block drew nothing
  or garbage there (missing sprites, distorted world meshes), with no validation error.

### Known limits

- Sprites are unlit and do not cast shadows. A lit mesh under an orthographic camera is
  only roughly clustered.
- Tilemap colliders are made where the map is when it starts; moving the tilemap object
  later does not move them.
- Packages are checked on macOS only so far. Windows and Linux packages are built by CI
  but have not been run on a clean machine or a Steam Deck.

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
