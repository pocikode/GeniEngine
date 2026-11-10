# GeniEngine

A C++17 game engine, built as a static library. It is heading towards streamed open-world 3D; see [ROADMAP.md](ROADMAP.md) for the plan and [docs/design/](docs/design/) for the design page of each major system.

Today it renders with OpenGL 3.3 and has a scene graph, glTF loading with skinning and animation, materials, and rigid-body physics.

## Build and test

You need CMake 3.30 or newer, a C++17 compiler, git, and GLFW and GLEW:

| Platform | Install GLFW and GLEW with |
|---|---|
| macOS | `brew install glfw glew` |
| Ubuntu / Debian | `sudo apt install libglfw3-dev libglew-dev libgl1-mesa-dev` |
| Windows | [vcpkg](https://vcpkg.io), with the `VCPKG_ROOT` environment variable set. The packages listed in `vcpkg.json` are installed during configure. |

Then one command configures, builds and runs the tests:

```sh
cmake --workflow --preset dev
```

Everything else the engine depends on is a git submodule under `thirdparty/`, fetched during configure if it is missing. The build goes to `build/dev/`.

| Workflow preset | What it does |
|---|---|
| `dev` | Debug build with tests, samples, the debug overlay and Tracy |
| `dev-no-gpu` | Same, but skips the tests that need a window and an OpenGL context |
| `release`, `release-no-gpu` | Optimized build with debug info, without Tracy, in `build/release/` |

Makefile builds use one core unless told otherwise: set `CMAKE_BUILD_PARALLEL_LEVEL` (for example `export CMAKE_BUILD_PARALLEL_LEVEL=8`) to speed them up.

To run tests again without rebuilding: `ctest --preset dev`. To run one group: `build/dev/tests/geni_tests --test-suite="scene tree"` (add `--list-test-suites` to see them all).

## Run the sample

```sh
build/dev/samples/sandbox/sandbox
```

`samples/sandbox` uses every engine feature in one scene: textured meshes, a light, an orbit camera, physics, and a skinned animated model. Left drag orbits, right drag pans, scroll zooms, Space spawns a crate, Esc quits.

**F1** toggles the debug overlay: frame time, fixed steps per frame, draw calls and the recent log.

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

Installed on the system: GLFW, GLEW.

Git submodules in `thirdparty/`: [GLM](https://github.com/g-truc/glm), [nlohmann/json](https://github.com/nlohmann/json) 3.12.0, [cgltf](https://github.com/jkuhlmann/cgltf) 1.15, [Bullet](https://github.com/bulletphysics/bullet3), [Dear ImGui](https://github.com/ocornut/imgui) 1.93.0 (docking), [Tracy](https://github.com/wolfpld/tracy) 0.15.0, [doctest](https://github.com/doctest/doctest) 2.5.3. Checked in directly: [stb_image](https://github.com/nothings/stb).

## Licence

See [LICENSE](LICENSE).
