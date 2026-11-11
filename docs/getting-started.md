# Getting started with GeniEngine

This guide takes you from a fresh clone to a small game of your own: a character that
walks around a scene, a script that reacts to input, a sound and a line of HUD text. It
assumes you can build C++ with CMake. Everything here works the same on macOS, Windows
and Linux.

## 1. Build the engine and play the reference game

Install the prerequisites for your platform (see the table in the [README](../README.md#build-and-test)),
then:

```sh
git clone --recursive <this repository> GeniEngine
cd GeniEngine
cmake --workflow --preset dev      # configure, build, run the tests (Debug)
cmake --workflow --preset release  # the same, optimized, in build/release/
```

Play **Geni City**, the reference game, from the release build:

```sh
build/release/samples/city/city --play
```

| Action | Keyboard and mouse | Gamepad |
|---|---|---|
| Move, look | W A S D, mouse | Left stick, right stick |
| Run, jump | Shift, Space | B, A |
| Get in or out of a car | E | Y |
| Accelerate, brake and reverse, steer | W, S, A/D | RT, LT, left stick |
| Handbrake, horn | Space, H | X, R3 |
| Menu | Esc | Start |
| Debug overlay | F1 | |

The game's rules are Lua files in `samples/city/assets/scripts/`. Open `game.lua` while
the game runs, change a number (say `ENTER_DISTANCE`, or the HUD text) and save: the
script reloads within half a second and keeps its state.

## 2. Make a project

A game is a CMake project that embeds the engine and has an `assets/` folder:

```
my_game/
  CMakeLists.txt
  main.cpp
  assets/
    models/   scenes/   scripts/   sounds/   input/
  external/GeniEngine/      (this repository, as a git submodule)
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.30)
project(MyGame)
set(CMAKE_CXX_STANDARD 17)
# The asset cooker (and the editor) are built only when asked for in an embedding project.
set(GENI_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(GENI_BUILD_EDITOR ON CACHE BOOL "" FORCE)
add_subdirectory(external/GeniEngine)
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE GeniEngine)
# Cook glTF models and images into engine formats next to the executable.
geni_cook_assets(my_game "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:my_game>/assets")
```

`main.cpp` sets up the scene and leaves the game logic to Lua:

```cpp
#include <geni.h>

class MyGame : public Geni::Application
{
  public:
    bool Init() override
    {
        auto &engine = Geni::Engine::GetInstance();
        m_scene = new Geni::Scene();
        engine.SetScene(m_scene);
        // Optional: your own bindings (the engine has sensible defaults).
        engine.GetInputActions().LoadJson(
            nlohmann::json::parse(engine.GetFileSystem().LoadAssetFileText("input/actions.json"), nullptr, false));

        // A scene made in the editor, then a navigation mesh from its static colliders.
        auto &assets = engine.GetAssetManager();
        auto level = assets.LoadScene(assets.FindBySource(Geni::AssetType::Scene, "scenes/level.gscene"));
        Geni::SerializationContext context{m_scene, &assets};
        Geni::SceneSerializer::Load(*m_scene, level->document, context);
        engine.GetNavigation().BuildForScene(*m_scene);

        // One object runs the game's script.
        auto *game = m_scene->CreateObject("Game");
        auto *script = new Geni::ScriptComponent();
        script->SetScript("scripts/main.lua");
        game->AddComponent(script);
        return true;
    }

    void Update(float deltaTime) override
    {
        m_scene->Update(deltaTime); // runs every component, scripts included, and draws
    }

    void Destroy() override
    {
    }

  private:
    Geni::Scene *m_scene = nullptr; // owned by the engine
};

int main()
{
    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(new MyGame());
    Geni::EngineConfig config;
    config.title = "My Game";
    config.debugOverlayVisible = false; // F1 shows it; the game UI needs it initialised
    if (engine.Init(config))
    {
        engine.Run();
    }
    engine.Destroy();
}
```

Build the level in the editor: `build/dev/editor/geni_editor --project my_game`. Drag
models from the asset browser into the scene, add a `PhysicsComponent` (static box) to
anything the player should stand on or bump into, add a camera, and save as
`scenes/level.gscene`.

## 3. A player, in Lua

`assets/scripts/main.lua`:

```lua
local Main = {}

function Main:start()
  -- A character model (a glTF with a skin and Idle/Walk/Run clips) with an animation
  -- graph and a controller that reads the move/jump/sprint actions.
  self.player = scene.spawn("models/hero.gltf", vec3(0, 0.1, 0), 0)
  local clips = self.player:get("AnimationComponent")
  self.player:add("AnimatorComponent", { animation = clips.animation, graph = "animations/hero.animgraph" })
  self.player:add("CharacterControllerComponent", { playerInput = true, walkSpeed = 1.6, runSpeed = 5.0 })

  -- The main camera follows the player and does not go through walls.
  local camera = scene.main_camera()
  self.camera = camera:add("ThirdPersonCameraComponent", { distance = 4.5 })
  self.camera:set_target(self.player)
  input.capture_cursor(true)

  audio.music("sounds/theme.wav", 2.0)
  self.coins = 0
end

function Main:update(dt)
  if input.pressed("interact") then
    audio.play("sounds/beep.wav", { position = self.player:world_position() })
    self.coins = self.coins + 1
  end
  ui.text("Coins: " .. self.coins, "top_left", 32, 24, 36)
  if input.pressed("pause") then game.quit() end
end

return Main
```

`assets/animations/hero.animgraph` blends idle, walk and run by speed and plays a jump on
the controller's `jump` trigger (the controller sets `speed`, `grounded` and `jump` on an
animator next to it):

```json
{
  "parameters": { "speed": 0.0, "grounded": true, "jump": "trigger" },
  "initial": "move",
  "states": {
    "move": { "blend": { "parameter": "speed", "clips": [["Idle", 0.0], ["Walk", 1.6], ["Run", 5.0]] } },
    "jump": { "clip": "Jump", "loop": false }
  },
  "transitions": [
    { "from": "move", "to": "jump", "when": [["jump"]], "duration": 0.08 },
    { "from": "jump", "to": "move", "when": [["grounded", "==", true]], "exitTime": 0.35, "duration": 0.2 }
  ],
  "events": [{ "clip": "Walk", "time": 0.26, "name": "footstep" }]
}
```

Every function available to scripts is listed in the [Lua API reference](scripting.md).

## 4. What else is there

| You want | Use | Read |
|---|---|---|
| A drivable car | `VehicleComponent` on an object with `Wheel0`..`Wheel3` children | [0010](design/0010-characters-vehicles-cameras.md) |
| People walking around | `NavAgentComponent` (`wanderRadius`, or `go_to` from Lua) after `BuildForScene` or `AttachToWorld` | [0014](design/0014-navigation.md) |
| A big world | `Engine::OpenWorld` with a `.gworld` and `GetNavigation().AttachToWorld(...)` | [0007](design/0007-world-streaming.md) |
| Menus | `ui.begin_menu`, `ui.button`, `ui.slider`, `ui.end_menu` | [0015](design/0015-game-ui.md) |
| Rebindable controls | `assets/input/actions.json` | [0009](design/0009-input-actions.md) |
| Sounds that follow objects | `AudioSourceComponent` | [0012](design/0012-audio.md) |
| Gameplay in C++ | Your own `Component` with `RegisterComponentType<T>` (it appears in the editor and in Lua) | [0005](design/0005-scene-storage-and-serialization.md) |

`samples/city/` is the complete reference: `PlayMode.cpp` is the C++ set-up and
`assets/scripts/` the game.
