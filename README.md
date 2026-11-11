# GeniEngine

A C++17 game engine, built as a static library. It is heading towards streamed open-world 3D; see [ROADMAP.md](ROADMAP.md) for the plan and [docs/design/](docs/design/) for the design page of each major system.

Today it renders with Vulkan (MoltenVK on macOS) behind a thin rendering interface: physically based materials, cascaded shadow maps, clustered forward lighting for many point and spot lights, a sky, fog, HDR with tonemapping and 4x MSAA, plus frustum culling and instancing. It has a scene graph with dense component indexes, a job system, a cooked asset pipeline (glTF and images converted offline), KTX2 textures with BC and ASTC variants, stable asset IDs with reference-counted handles, reflected components, text scene files, prefabs, skinning, animation and rigid-body physics (Jolt). Large worlds stream in grid cells around the camera, with mesh LODs and simplified distant cells (HLOD) made by the cooker, camera-relative rendering, origin rebasing, collision streamed per cell and per-platform memory budgets. It comes with a scene editor (`geni_editor`): viewport, hierarchy, inspector and asset browser, gizmos with undo and redo, play-in-editor, and hot reload for shaders and assets.

## Build and test

You need CMake 3.30 or newer, a C++17 compiler, git, GLFW, and the Vulkan loader, headers and validation layers:

| Platform | Install with |
|---|---|
| macOS | `brew install glfw molten-vk vulkan-headers vulkan-loader vulkan-validationlayers` |
| Ubuntu / Debian | `sudo apt install libglfw3-dev libvulkan-dev vulkan-validationlayers` plus a Vulkan driver (`mesa-vulkan-drivers` includes the lavapipe software driver) |
| Windows | The [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (sets `VULKAN_SDK`), and [vcpkg](https://vcpkg.io) with the `VCPKG_ROOT` environment variable set for GLFW (installed from `vcpkg.json` during configure). |

Debug builds turn on the Vulkan validation layers; every validation error is logged, and the tests fail on any. The Homebrew layer location is found at configure time, so no environment setup is needed.

Shaders are written in Slang and compiled offline by a pinned `slangc`, downloaded once into `.cache/slang/` at configure time. Set `GENI_SLANGC` to use a different one.

Then one command configures, builds and runs the tests:

```sh
cmake --workflow --preset dev
```

Everything else the engine depends on is a git submodule under `thirdparty/`, fetched during configure if it is missing. The build goes to `build/dev/`.

| Workflow preset | What it does |
|---|---|
| `dev` | Debug build with tests, samples, the debug overlay and Tracy |
| `dev-no-gpu` | Same, but skips the tests that need a window and a Vulkan device |
| `release`, `release-no-gpu` | Optimized build with debug info, without Tracy, in `build/release/` |

Makefile builds use one core unless told otherwise: set `CMAKE_BUILD_PARALLEL_LEVEL` (for example `export CMAKE_BUILD_PARALLEL_LEVEL=8`) to speed them up.

To run tests again without rebuilding: `ctest --preset dev`. To run one group: `build/dev/tests/geni_tests --test-suite="scene tree"` (add `--list-test-suites` to see them all).

The `screenshot_sandbox_*` tests render the sample off-screen and compare it with `tests/reference/*.png` (`tools/image_compare.cpp`; a failing run writes a diff image to `build/dev/screenshots/`). After an intended visual change, regenerate the references with `GENI_UPDATE_REFERENCES=1 ctest --preset dev -R screenshot` and review the new images before committing them.

## Run the samples

```sh
build/dev/samples/sandbox/sandbox   # everything the engine can do, in one scene
build/dev/samples/city/city         # the reference game's first city block
build/dev/samples/city/city --world # its streamed 4 km x 4 km world
```

`samples/sandbox` uses every engine feature in one scene: PBR textured meshes, a sun and a ring of point lights, an orbit camera, physics, and a skinned animated model. Left drag orbits, right drag pans, scroll zooms, Space spawns a crate, Esc quits.

`samples/city` is the reference game (and the M2 performance benchmark): 10,000 instanced buildings, 100 point lights and cascaded shadows. The block is a scene file, `samples/city/assets/scenes/city.gscene`, built and edited in the editor; the program only loads it. Run it with `--hidden --frames N --budget-ms M` to measure: it exits with code 4 when the mean frame time is over budget. CTest runs it as `benchmark_city`.

`city --world` streams `samples/city/assets/worlds/metro`, 16 x 16 cells of 256 m, around a free camera (W/A/S/D, Q/E, Shift, right mouse button to look; F1 shows memory and streaming against the budget). With `--route` the camera flies across the world at 40 m/s and the program exits non-zero on a hitch (`--hitch-ms`), on a nearby cell that was not loaded in time, or over the memory budget (`--platform desktop|mobile|web` picks the budget). CTest runs a shorter flight as `benchmark_world`.

## Edit a scene

```sh
cmake --build build/dev --target edit_city      # the editor on samples/city
build/dev/editor/geni_editor --project my_game  # or on any folder with an assets/ folder
```

The editor opens the project's first scene. `File > Open World` opens a `.gworld` instead; pick a region of cells in the World panel and load it to edit those cells (saving writes each cell to its own file). Panels dock around the scene viewport: the hierarchy on the left, the inspector on the right, assets and the console below.

| To | Do |
|---|---|
| Look around | Right-drag to look, with W A S D Q E to fly (Shift is faster); Alt + left-drag orbits, middle-drag pans, the wheel zooms, F frames the selection |
| Select | Click an object in the viewport or the hierarchy; Ctrl or Shift adds to the selection |
| Move, rotate, scale | W, E and R pick the gizmo; drag a handle. Hold Ctrl to snap, Esc cancels a drag |
| Change values | Edit fields in the inspector; every change is one undo step (Ctrl+Z, Ctrl+Shift+Z) |
| Add things | The GameObject menu, or drag a model, mesh or prefab from Assets into the scene |
| Lay out a block | Edit > Duplicate as Grid |
| Reuse a group | Right-click an object > Create Prefab; drag the prefab in again; editing the prefab file changes every instance |
| Try it | Ctrl+P plays the scene in place, Ctrl+P or Esc stops and puts everything back |
| Save | Ctrl+S writes a `.gscene` text file, one object per line |

While the editor runs, a changed `.slang` file is recompiled and a changed model or image is re-cooked, and the scene updates without a restart. On macOS, Ctrl is Cmd.

A scripted session runs editor commands without a window, for CI or a batch edit: `geni_editor --assets DIR --hidden --run "open scenes/city.gscene" --run "select Sun" --run "move 0 5 0" --run save` (the commands are in `EditorApp::RunScriptCommand`).

**F1** toggles the debug overlay: frame time, fixed steps per frame, draw calls, submitted/visible/culled objects, instances, clustered lights and the recent log.

To profile, start the [Tracy](https://github.com/wolfpld/tracy) UI (version 0.15) and connect to the running sample. The `dev` preset compiles the Tracy client in; it records nothing until the UI connects.

## Use the engine in a game

Add this repository as a submodule and pull it in from your game's `CMakeLists.txt`:

```cmake
add_subdirectory(external/GeniEngine)
target_link_libraries(my_game PRIVATE GeniEngine)
```

Subclass `Geni::Application`, then:

```cpp
#include <geni.h>

int main()
{
    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(new MyGame()); // the engine takes ownership

    Geni::EngineConfig config;
    config.title = "My Game";
    if (engine.Init(config))
    {
        engine.Run();
    }
    engine.Destroy();
}
```

Each frame the engine runs zero or more **fixed steps** (physics, then `Application::FixedUpdate`, at 60 Hz by default), then one `Application::Update`, then renders. Call `Scene::Update` from your `Update`: that is what submits draw calls. `samples/sandbox/main.cpp` is a complete example.

Asset paths are relative to an `assets/` folder: the top-level project's `assets/` during development, or the one next to the executable. `FileSystem::SetAssetsFolder` overrides it.

Shaders are Slang, compiled to SPIR-V offline by a pinned `slangc`. List them with `geni_compile_shaders(<target> SOURCE_DIR ... OUTPUT_DIR ... SOURCES ...)` from `cmake/GeniShaders.cmake`; a `.mat` file names the Slang source and the engine loads the compiled `.spv` next to it. The bindings every shader uses are described in `include/render/ShaderInterface.h`, and `samples/sandbox/assets/shaders/` has examples. See `docs/design/0003-slang-shaders.md`.

## Assets

Runtime assets are cooked, not parsed. `tools/asset_cooker` reads glTF/GLB scenes and
PNG/JPEG images and writes engine formats next to the game:

- `.gscene` text scene (flat node array with parent indices and reflected components,
  one node per line)
- `.gmesh` meshes with their vertex layout, bounds and skin data
- `.ganim` animation clips
- `.gmat` materials (shader, parameters, texture IDs)
- KTX2 textures with mip chains, one file per format: `_bc7` (or `_bc5` for normal
  maps) on desktop, `_astc` on mobile, `_rgba8` as a fallback. Colour textures use the
  sRGB formats; normal maps and data textures are linear.

`cooked_manifest.json` maps stable asset IDs to those files. `AssetManager` loads by ID,
decodes on the job system and creates GPU objects on the main thread; a handle can be
held while the load is pending, and `CollectGarbage` releases assets nothing uses.
`AssetManager::FindBySource` finds an ID from a source path. `samples/` and the tests
cook their assets at build time through `geni_cook_assets`, so editing a glTF or image
rebuilds the cooked files. The engine does not link cgltf or an image decoder; those
live only in the cooker.

IDs come from the source path unless the file has a `.meta` sidecar with a GUID; run
`geni_asset_cooker --write-meta` to create them, and commit them, so that renaming or
moving an asset keeps its ID. A `.meta` can also set a standalone image's usage
(`"usage": "normal"`).

A scene saved from code uses the same text format:

```cpp
Geni::SerializationContext context{scene, &engine.GetAssetManager()};
Geni::SceneSerializer::Save(*scene, path);
Geni::SceneSerializer::Load(*scene, document, context);
```

Components register their properties with `RegisterComponentType<T>` from
`scene/Reflection.h`; see `docs/design/0005-scene-storage-and-serialization.md`.

CMake options when embedding:

| Option | Default | Effect |
|---|---|---|
| `GENI_ENABLE_IMGUI` | `ON` | Builds Dear ImGui into the engine (target `geni_imgui`) and enables the debug overlay. Turn it off if your game compiles its own copy of ImGui. |
| `GENI_ENABLE_TRACY` | `OFF` | Builds the Tracy client into the engine and activates the `GENI_PROFILE_*` macros. |
| `GENI_FORCE_ASSERTS` | `OFF` | Keeps `GENI_ASSERT` active in release builds. |
| `GENI_BUILD_TESTS`, `GENI_BUILD_SAMPLES` | `ON` only when GeniEngine is the top-level project | |

## Diagnostics

- **Logging**: `GENI_LOG_INFO("loaded %d meshes", count)` and the `TRACE`, `DEBUG`, `WARN`, `ERROR`, `FATAL` variants (printf-style). Output goes to stderr, to the overlay, and to a file if `EngineConfig::logFile` is set.
- **Assertions**: `GENI_ASSERT(condition)` and `GENI_ASSERT_MSG(condition, "format", ...)` are checked in debug builds. `GENI_FATAL("format", ...)` always stops the program.
- **Crash reports**: on a crash, a failed assertion or an unhandled exception, the engine prints the reason, a stack trace and the last log lines, and writes the same to `geni_crash_<date>_<time>.log` in the working directory (change with `EngineConfig::crashReportDirectory`).

## Dependencies

Installed on the system: GLFW, the Vulkan loader and headers, the Vulkan validation layers (and MoltenVK on macOS). Downloaded at configure time: the [Slang](https://github.com/shader-slang/slang) compiler, at a pinned version.

Git submodules in `thirdparty/`: [GLM](https://github.com/g-truc/glm), [nlohmann/json](https://github.com/nlohmann/json) 3.12.0, [cgltf](https://github.com/jkuhlmann/cgltf) 1.15, [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 5.5.0, [Dear ImGui](https://github.com/ocornut/imgui) 1.93.0 (docking), [Tracy](https://github.com/wolfpld/tracy) 0.15.0, [doctest](https://github.com/doctest/doctest) 2.5.3, [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect), [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo) and [astc-encoder](https://github.com/ARM-software/astc-encoder) and [meshoptimizer](https://github.com/zeux/meshoptimizer) 1.2 (cooker only). Checked in directly: [stb_image and stb_image_write](https://github.com/nothings/stb).

## Licence

See [LICENSE](LICENSE).
