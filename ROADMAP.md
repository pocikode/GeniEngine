# GeniEngine Roadmap

Last updated: 2026-11-11

## Goal

GeniEngine becomes a general-purpose engine by first being very good at one thing: **streamed open-world 3D** (driving and on-foot in a large world). Breadth comes after that works and other people can use it.

The long-term ambition is an engine that could carry large productions. This roadmap covers the part one person with AI agents controls: the architecture, the tools and a shipped reference game.

## Requirements this plan is built on

| Requirement | Decision |
|---|---|
| Time budget | 5–10 hours/week (planned at about 8 h/week, roughly 100 hours per quarter) |
| First focus | Open-world 3D |
| Graphics API | Vulkan only, with MoltenVK on Apple platforms |
| Platforms | macOS, Windows, Linux/Steam Deck first; Android and iOS next; web last |
| How work gets done | Solo, with AI agents writing most of the code |

Two consequences of these choices:

- **Web cannot run Vulkan.** Browsers only expose WebGPU and WebGL. Web support therefore needs a second backend (WebGPU) behind the rendering interface. It is placed last (M8), and the rendering interface in M1 is kept thin enough to allow it.
- **At 8 h/week the reference game lands in late 2030.** The same plan at 20 h/week lands in about 2028. Hours per week is the biggest lever on every date below.

## Rules that apply to every milestone

1. **Every milestone ends in a runnable demo** in `samples/`, built by CI on all desktop platforms.
2. **The reference game lives in the repo** (`samples/city/`) and grows with each milestone. A feature the reference game does not use is postponed.
3. **A written design comes before each major system**: one page in `docs/design/` stating the problem, the chosen approach and what was rejected. Agents implement against it; you review against it.
4. **Nothing merges without automated checks**: unit tests, a render screenshot comparison and a performance benchmark with a budget.
5. **Mobile limits are respected from M1 on**, even though mobile ships later:
   - clustered forward lighting, not a heavy deferred pipeline
   - no dependence on desktop-only Vulkan features; baseline is the Vulkan version that MoltenVK and mainstream Android devices both support (verify at the start of M1)
   - texture pipeline produces both BC (desktop) and ASTC (mobile) formats
   - memory budgets are set per platform and enforced by the streaming system
6. **Use existing libraries for solved problems**: physics, navmesh, audio, texture compression, mesh simplification.

## Milestones

Dates assume about 8 h/week starting November 2026.

### M0: Foundations (Nov 2026 – Jan 2027, about 80 h)

**Status (2026-11-10):** implemented. Verified locally on macOS and on Linux (Arch, GCC 16, Wayland): `cmake --workflow --preset dev` builds and passes every test. On Linux, GNU ld needs the Bullet libraries listed dependents-first, which `CMakeLists.txt` now does. Open until CI has run green on macOS, Windows and Linux.

Make the project safe to change quickly.

- Logging, assertions and crash reporting with stack traces
- Tracy profiler integration and a Dear ImGui debug overlay
- Unit test framework; first tests for transforms, the scene tree and GLTF loading
- CI building and testing on macOS, Windows and Linux
- `samples/` directory with a demo of everything that works today (`samples/sandbox`)
- Fixed-timestep game loop separated from rendering
- `docs/design/` created, with the first design page written for M1

**Done when:** a fresh clone builds and passes tests on all three desktop platforms with one command, and CI is green.

### M1: Vulkan backend (Feb – Jul 2027, about 200 h)

**Status (2026-11-10):** implemented; all tests pass locally on macOS (MoltenVK) with validation layers on, and the sandbox matches the screenshots captured from the OpenGL renderer. On Linux, all tests pass in debug and release with zero validation errors on an NVIDIA RTX 4060 Laptop GPU (driver 615.78) and an AMD Radeon 780M (Mesa RADV), and the screenshot tests match the references on both. Known driver issue: on NVIDIA under Wayland, the driver crashes in `vkDestroySwapchainKHR` when a second Vulkan instance was created and destroyed while the engine's was alive. It does not happen under X11 or on RADV. The `gpu rhi` test, which creates its own instance, therefore runs in its own process (`gpu_rhi`); not yet reported to NVIDIA. Open until CI has run green on macOS, Windows and Linux.

Replace direct OpenGL calls with a rendering interface (RHI) and a Vulkan backend.

- Thin RHI: device, swapchain, buffers, textures, pipelines, render passes, command lists
- Vulkan backend with validation layers on in debug builds; MoltenVK on macOS
- Shader pipeline: GLSL compiled offline to SPIR-V, with reflection for uniform layout
- Port of everything that works today: meshes, materials, textures, skinned meshes
- GPU memory allocator (Vulkan Memory Allocator)
- Screenshot comparison tests in CI
- OpenGL code deleted

**Done when:** the M0 sample renders identically on Vulkan on macOS, Windows and Linux, with zero validation errors.

### M2: Renderer v1 (Aug 2027 – Jan 2028, about 200 h)

**Status (2026-11-10):** implemented; design page `docs/design/0002-renderer-v1.md`. All tests pass locally on macOS (MoltenVK) with validation layers on: the sandbox and the 10,000-object city block render with cascaded shadows, clustered lighting, PBR, HDR and MSAA, and the release benchmark runs at 8.5 ms/frame (118 fps) on an Apple M4. On Linux at 1280×720 the same benchmark runs at 2.7 ms/frame (373 fps) on an NVIDIA RTX 4060 Laptop GPU and 4.4 ms/frame (225 fps) on an AMD Radeon 780M integrated GPU. Open until CI has run green on macOS, Windows and Linux and the budget has been measured on a Windows GPU.

Make it look modern and scale to many objects.

- Frustum culling, draw sorting by pipeline and material, instancing
- Physically based materials, following the glTF material model
- Directional light with cascaded shadow maps
- Clustered forward lighting for many point and spot lights
- HDR rendering, tonemapping, sky and basic fog
- Anti-aliasing (MSAA or TAA; decided in the design page)
- Performance benchmark scene with a frame-time budget in CI

**Done when:** a static city block with 10,000 objects and 100 lights holds 60 fps on your development Mac and on a mid-range Windows GPU.

### M3: Assets, jobs and serialization (Feb – Jul 2028, about 200 h)

**Status (2026-11-10):** implemented. Design pages `docs/design/0004-jobs-and-assets.md`
and `docs/design/0005-scene-storage-and-serialization.md`. The city block loads from a
cooked `.gscene` (10001 objects, 101 lights), instantiated 1,000 nodes per frame while
its meshes and materials load on workers; `samples/sandbox` loads its skinned model
from cooked assets, and all screenshot tests still match the M2 references. On Linux
with an NVIDIA RTX 4060 Laptop GPU (release, 1280×720) the block loads in about 100
frames with the slowest at 2.5 ms, and then runs at 3.0 ms/frame. `benchmark_city` fails
on a loading hitch over `GENI_LOAD_FRAME_BUDGET_MS`. Unit, GPU and cooker tests pass
locally on Linux with validation layers on: job system, asset IDs/manifest/handles and
garbage collection, async loading with material texture dependencies, KTX2 layout and
per-usage texture formats (sRGB colour, BC5 normal maps), scene save/load and
step-by-step instantiation, malformed scenes, and component reflection. The runtime no
longer links cgltf or an image decoder. Open until CI has run green on macOS, Windows
and Linux, including the new `ktx validate` step.

Everything streaming and the editor depend on.

- Job system; asset loading and animation moved off the main thread
- Asset cooker: an offline tool that converts glTF and images to engine binary formats
- Stable asset IDs and handle-based resource management with reference counting
- Compressed textures with mipmaps (KTX2, BC and ASTC outputs)
- Reflection for components, used for serialization and later by the editor and scripting
- Scene file format (text, diff-friendly) with save and load
- Scene storage reworked for cache-friendly iteration; decide here whether to adopt an ECS

**Done when:** the city block loads from a scene file, assets load asynchronously without frame hitches, and no glTF parsing happens at runtime.

### M4: Editor v1 (Aug 2028 – Jan 2029, about 200 h)

**Status (2026-11-11):** implemented; design page `docs/design/0006-editor.md`.
`geni_editor` (in `editor/`) has a docked viewport, hierarchy, inspector, asset browser and
console; translate, rotate and scale gizmos with snapping; picking; undo and redo for every
edit; play-in-editor; hot reload of shaders (recompiled with the pinned `slangc`) and
assets (re-cooked by `geni_asset_cooker`); and prefabs with nesting, unpacking and
live update of instances. The city block is now `samples/city/assets/scenes/city.gscene`:
the M3 layout was imported into the editor once and saved, `tools/gen_assets.py` only
generates the nine-mesh building kit, and `samples/city` loads the scene file with no
scene setup in C++. It still matches the M2 reference screenshot and passes the benchmark.
Tested on macOS (MoltenVK, validation layers on): unit tests for the engine additions and
the editor core, a scripted editor session on the city block (`editor_smoke`) and a
screenshot of the editor window (`screenshot_editor`). Limits of v1, all in the design
page: picking is by bounding box, a prefab instance can only override its root's name and
transform, and overlays are not depth-tested against the scene. Nobody other than the
author has used the editor yet, and nothing has been laid out in it by hand beyond the
scripted session. Open until CI has run green on macOS, Windows and Linux.

The point where other people can start using the engine.

- Editor application built with Dear ImGui: viewport, hierarchy, inspector, asset browser
- Translate, rotate and scale gizmos; object picking; undo and redo
- Play-in-editor mode
- Hot reload for shaders and assets
- Prefabs (reusable object templates)

**Done when:** the city block scene is built and edited entirely in the editor, with no scene setup in C++.

### M5: World streaming (Feb – Sep 2029, about 270 h)

**Status (2026-11-11):** implemented; design pages `docs/design/0007-world-streaming.md`
and `docs/design/0008-physics-on-jolt.md`. `samples/city --world` streams
`worlds/metro`, a 4 km × 4 km world of 16 × 16 cells of 256 m built from district prefabs
(128,064 objects in total, about 1.3 MB of scene files). The cooker builds up to three
mesh LODs per mesh and one HLOD per cell with meshoptimizer; the renderer picks LODs by
projected error and renders camera-relative; the scene rebases its origin to the nearest
cell corner every kilometre; physics is Jolt in double precision with each cell's
collision added and removed in batches; `StreamingBudget` holds per-platform radii and
memory budgets, shown in the debug overlay; the editor opens a `.gworld` and edits a
region of cells at a time. Flying the full 5 km diagonal at 40 m/s (`city --world --route`,
release, 1280×720, Apple M4): 8.3 ms mean, 9.7 ms p99, 18 ms slowest frame, no cell loaded
late, at most 10,383 objects and 32 MiB of resident assets against a desktop budget of
60,000 and 768 MiB; with the mobile budget (15,000 objects, 256 MiB) 6,588 objects and
19 MiB, still with no late cell. Getting there needed one engine change outside the plan:
GPU buffers made between frames are now uploaded inside the next frame instead of a
blocking submit each, which removed 10–30 ms upload stalls. Bullet is gone; the sandbox's
falling-crate screenshot was recaptured for Jolt. Tested on macOS (MoltenVK, validation
layers on) in debug and release: `benchmark_world`, `screenshot_world`, the world cases in
`geni_tests`, the cooker and editor tests, and the editor smoke session, which now edits a
region of the metro world. Open until CI has run green on macOS, Windows and Linux, and
until the flight has been measured on a Windows GPU and the Steam Deck.

The engine's specialty.

- World partitioned into grid cells that load and unload around the camera
- Mesh LOD generation in the cooker (meshoptimizer) and LOD selection at runtime
- Simplified distant meshes for far cells (HLOD)
- Camera-relative rendering and origin rebasing for worlds larger than a few kilometres
- Physics moved from Bullet to Jolt, with collision streamed per cell
- Per-platform memory and streaming budgets, visible in the debug overlay
- Editor support for editing a world one region at a time

**Done when:** you can fly across a 4 km × 4 km world at vehicle speed with no visible hitches, within a fixed memory budget.

### M6: Gameplay systems and v0.1 (Oct 2029 – Sep 2030, about 400 h)

Enough to build the reference game.

- Character controller and third-person camera
- Vehicle physics (Jolt vehicle constraints)
- Animation blending and state machines
- Audio (miniaudio or similar): 3D sounds, streaming music
- Scripting layer (default: Lua; see open decisions)
- Navigation mesh and simple AI agents (Recast/Detour)
- Gamepad input and an action-mapping layer
- Game UI: menus and HUD

**Done when:** `samples/city` is playable: walk around a streamed city block, enter a car, drive, with pedestrians or traffic moving around. This is tagged **v0.1** and announced publicly with documentation, a getting-started guide and contribution guidelines.

### M7: Mobile (2031)

- Android (native Vulkan) and iOS (MoltenVK) builds and packaging
- Touch input, lifecycle handling (pause and resume), device performance tiers
- Reduced-quality renderer settings and smaller streaming budgets

**Done when:** the reference game runs at 30 fps on a mid-range Android phone and a recent iPhone.

### M8: Web (2031–2032)

- WebGPU backend behind the RHI
- Shader translation from SPIR-V to WGSL
- Emscripten build, with asset streaming over HTTP
- Reduced world size, because browser memory and threading are limited

**Done when:** a cut-down reference scene runs in Chrome and Safari.

### After v0.1: widening to general-purpose

Chosen by what users ask for, not planned in detail now:

- Open-world layers: traffic and pedestrian simulation, time of day, weather, mission system
- Rendering: global illumination, GPU-driven culling, terrain, vegetation, particles
- 2D rendering, sprites and tilemaps
- Networking and multiplayer
- Plugin system and package distribution
- Visual tools: material editor, animation graph editor, timeline for cutscenes

## Not in scope

- **Consoles.** They require licensed dev kits and a registered company. The architecture does not block them.
- **Feature parity with Unreal or Unity.** The plan is depth in open worlds first.
- **Content production.** City art, vehicles, animation and audio for the reference game come from free or purchased asset packs.

## Open decisions

Each has a default so work is never blocked. Change them before the milestone that depends on them.

| Decision | Default | Needed by |
|---|---|---|
| Scripting language | Lua | M6 |
| Scene model: keep `GameObject`/`Component` or move to an ECS | Decide from M2 profiling data | M3 |
| Shader language: GLSL or Slang | Decided before M3: Slang, compiled offline to SPIR-V by a pinned `slangc`; the same sources compile to WGSL for M8 (`docs/design/0003-slang-shaders.md`) | M3 |
| Anti-aliasing: MSAA or TAA | Decided in M2: 4x MSAA, configurable (`docs/design/0002-renderer-v1.md`); TAA revisited with M5 camera-relative rendering | M2 |
| Licence and contribution terms | Keep the current licence | M6 |

## Review points

Re-plan dates at the end of each milestone using actual hours spent. If a milestone runs more than 50% over, cut scope from it instead of pushing every later date.
