# 0024: Missions and saved games

Milestone: After v0.1 (open-world layers) · Status: Done · Last updated: 2026-11-13

## Problem

Geni City can be walked and driven, but nothing asks the player to do anything, and
nothing survives quitting. Every open-world game needs the same two pieces: missions made
of objectives that track progress, unlock each other and report what happened, and a
place to save progress. Writing them per game in Lua works, but every game redoes the
bookkeeping (states, unlocks, timers, reach checks against the player in world
coordinates) and invents its own file format and location.

**Done** means missions are data (`.gmission` JSON) loaded by an engine `MissionSystem`
that tracks state, checks "reach" objectives itself, runs time limits, unlocks missions
that depend on others, reports events, and saves and restores its state; a `save` library
writes JSON documents to the platform's user-data folder; Lua drives both; Geni City has a
first mission shown on its HUD and saved on completion, and the autopilot checks it.

## Approach

- **Format**: `{"format": "geni.mission", "version": 1, "missions": [{"id", "title",
  "description", "requires": [mission ids], "timeLimit": seconds (0: none),
  "objectives": [{"id", "text", "kind": "reach" | "count" | "custom", "position": [world
  x, y, z] or "object": name, "radius", "count"}]}]}`. Objectives run in order; the
  mission completes when the last one does.
- **States**: `locked` (a required mission is not completed), `available`, `active`,
  `completed`, `failed`. `Start` makes an available (or failed, to retry) mission active
  at its first objective; `Fail` ends it with a reason; `Abandon` returns it to available.
- **Objectives**: `reach` completes when the player object (`SetPlayer`, by object ID) is
  within `radius` of the position (world coordinates, compared in double precision, so it
  works across origin shifts) or of the named object; `count` completes when progress
  reaches `count` (`AddProgress`); `custom` completes when the game says so
  (`CompleteObjective`). A mission time limit fails it when it runs out.
- **Events**: started, objective completed, completed, failed, unlocked: queued and taken
  by the game each frame (`TakeEvents`; Lua `missions.events()`), so the HUD and sounds
  react without callbacks into Lua from engine code.
- **Saving**: `MissionSystem::SaveState`/`RestoreState` produce and read JSON (states,
  current objective, progress, elapsed time). `SaveGame` writes named JSON documents to
  `<user data>/<EngineConfig::gameName>/saves/<name>.json` (macOS Application Support,
  Windows `%APPDATA%`, Linux `$XDG_DATA_HOME` or `~/.local/share`; `EngineConfig::
  saveFolder` overrides it), through a temporary file and a rename so a crash never leaves
  half a save. Lua: `save.write(name, table)`, `save.read(name)`, `save.exists`,
  `save.delete`.
- `MissionSystem` is owned by `Engine` and updated after `Application::Update`; it knows
  nothing about rendering or UI.

## Rejected

- **Missions as Lua scripts only.** Flexible, but the state machine, unlocks and saving
  would be repeated in every game; data plus Lua hooks covers the common case, and
  `custom` objectives leave the rest to scripts.
- **Parallel and optional objectives, branching.** Useful later; ordered objectives first.
- **A binary save format.** JSON is debuggable and small enough for progress data.
- **Saving the whole scene.** Streamed worlds rebuild from their cells; games save the
  state they own.

## To verify

- Save folder permissions in a sandboxed macOS app bundle. Default: Application Support is
  writable for unsandboxed apps, which is what the packager produces.
