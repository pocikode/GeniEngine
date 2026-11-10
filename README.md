# GeniEngine

A C++17 game engine, built as a static library. It is heading towards streamed open-world 3D; see [ROADMAP.md](ROADMAP.md) for the plan and [docs/design/](docs/design/) for the design page of each major system.

Today it renders with Vulkan (MoltenVK on macOS) behind a thin rendering interface: physically based glTF materials, cascaded shadow maps, clustered forward lighting for many point and spot lights, a sky, fog, HDR with tonemapping and 4x MSAA, plus frustum culling and instancing. It has a scene graph, glTF loading with skinning and animation, and rigid-body physics.

## Build and test

You need CMake 3.30 or newer, a C++17 compiler, git, GLFW, and the Vulkan loader, headers, validation layers and the `glslangValidator` shader compiler:

| Platform | Install with |
|---|---|
| macOS | `brew install glfw molten-vk vulkan-headers vulkan-loader vulkan-validationlayers glslang` |
| Ubuntu / Debian | `sudo apt install libglfw3-dev libvulkan-dev vulkan-validationlayers glslang-tools` plus a Vulkan driver (`mesa-vulkan-drivers` includes the lavapipe software driver) |
| Windows | The [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (sets `VULKAN_SDK`), and [vcpkg](https://vcpkg.io) with the `VCPKG_ROOT` environment variable set for GLFW (installed from `vcpkg.json` during configure). |

Debug builds turn on the Vulkan validation layers; every validation error is logged, and the tests fail on any. The Homebrew layer location is found at configure time, so no environment setup is needed.

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
```

`samples/sandbox` uses every engine feature in one scene: PBR textured meshes, a sun and a ring of point lights, an orbit camera, physics, and a skinned animated model. Left drag orbits, right drag pans, scroll zooms, Space spawns a crate, Esc quits.

`samples/city` is the reference game (and the M2 performance benchmark): 10,000 instanced buildings, 100 point lights and cascaded shadows. Run it with `--hidden --frames N --budget-ms M` to measure: it exits with code 4 when the mean frame time is over budget. CTest runs it as `benchmark_city`.

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

Shaders are GLSL 450, compiled to SPIR-V at build time. List them with `geni_compile_shaders(<target> SOURCE_DIR ... OUTPUT_DIR ... SOURCES ...)` from `cmake/GeniShaders.cmake`; a `.mat` file names the GLSL source and the engine loads the compiled `.spv` next to it. The bindings every shader uses are described in `include/render/ShaderInterface.h`, and `samples/sandbox/assets/shaders/` has examples.

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

Installed on the system: GLFW, the Vulkan loader and headers, the Vulkan validation layers, glslang (and MoltenVK on macOS).

Git submodules in `thirdparty/`: [GLM](https://github.com/g-truc/glm), [nlohmann/json](https://github.com/nlohmann/json) 3.12.0, [cgltf](https://github.com/jkuhlmann/cgltf) 1.15, [Bullet](https://github.com/bulletphysics/bullet3), [Dear ImGui](https://github.com/ocornut/imgui) 1.93.0 (docking), [Tracy](https://github.com/wolfpld/tracy) 0.15.0, [doctest](https://github.com/doctest/doctest) 2.5.3, [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect). Checked in directly: [stb_image and stb_image_write](https://github.com/nothings/stb).

## Licence

See [LICENSE](LICENSE).
