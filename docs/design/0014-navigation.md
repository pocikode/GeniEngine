# 0014: Navigation meshes and AI agents

Milestone: M6 · Status: Done · Last updated: 2026-11-13

## Problem

Nothing in the engine can find a path. The reference game needs pedestrians walking the
city's streets and pavements around the player, avoiding buildings and each other, in a
world that is streamed: the walkable area changes as cells load and unload.

**Done** means a navigation mesh is built for each loaded cell of a streamed world (and for
an ordinary scene on request), paths can be queried, crowds of agents steer along them
with local avoidance, and an agent component makes an object walk to a destination or
wander.

## Approach

**Recast and Detour** (submodule `thirdparty/recastnavigation`, v1.6.0): Recast builds,
Detour queries, DetourCrowd steers. Only `src/nav/` includes them.

**Tiles are cells.** One `dtNavMesh` in tiled mode with the tile size equal to the world's
cell size (256 m) and the tile grid aligned to the world grid, in world coordinates as
floats (enough for worlds of tens of kilometres; Detour has no double mode). A plain scene
uses the same tiled mesh with a default tile size.

**Input geometry is collision, not render meshes.** `NavigationSystem` gathers the static
colliders under a cell's root (boxes, spheres and capsules, as triangles in world space),
which are already what the character walks on. Building a tile from a few hundred boxes
takes tens of milliseconds, so it runs on the job system (`NavMeshBuilder::BuildTile`, no
shared state) and the finished tile is added on the main thread. `WorldStreamer` gains cell
listeners: a cell that becomes `Loaded` schedules its tile; one that starts unloading
removes it. Agents on a removed tile are parked until it returns.

Build settings (`NavMeshSettings`): 0.3 m cells (nudged so a tile is a whole number of
cells, or neighbouring tiles would not link), 0.2 m height, agent radius 0.4 m, height
1.8 m, climb 0.4 m (kerbs), slope 45°, monotone partitioning (fast, good for city grids).

**Queries**: `FindPath` (straight path corners), `FindNearestPoint`, `RandomPointNear`.

**Agents**: one `dtCrowd` (up to 256 agents). `NavAgentComponent` adds its object to the
crowd when its tile exists, sets targets (`MoveTo`), and every frame copies the crowd's
position back to the object and turns it to face its velocity. With `wanderRadius` set it
picks a new random reachable point near where it is when it arrives (the pedestrians'
behaviour). It can write its speed to an animator parameter so walk cycles match.
`NavigationSystem::Update` adds finished tiles and runs the crowd once per frame, after
the fixed steps and before `Application::Update`, so components read this frame's
positions; it is skipped while physics is paused.

## Cooked tiles (added 2026-11-13, M7)

Building at load cost every player the same Recast work on every cell. The tile builder
now lives in `nav/NavMeshBuilder.{h,cpp}` with no engine dependency and is compiled into
the cooker too. `WorldCooker` gathers each cell's static colliders from the scene files
(prefabs expanded, inactive objects included, exactly as `NavGeometry::AddColliders`
gathers them at run time), builds the tiles on every core with the default
`NavMeshSettings` on the world's grid, and writes `nav/cell_<x>_<z>.gnav` (raw Detour tile
data) as `navtile` assets. The cooked world lists each cell's tile and `navKey`, the
`NavMeshSettings::BuildKey` hash of the settings used. `AttachToWorld(streamer, scene,
assets)` reads a cell's cooked tile on a worker when the keys match, and builds from the
colliders as before when they do not (a game with other agent settings) or when no asset
manager is given. A world edited in the editor is re-cooked on save, so tiles stay in step
with the cells.

## Rejected

- **Building at load only** (the first version). Simple and always consistent with the
  scene, but every player paid for it; kept as the fallback.
- **Render geometry as input.** Thousands more triangles than the colliders, with
  details (windows, trees' crowns) that do not affect walking.
- **DetourTileCache with obstacles.** Needed for doors and parked cars that move; nothing
  in the reference game needs it yet.

## To verify

- Tile build time for the densest downtown cell on the Steam Deck (budget: under 100 ms
  on one worker).
- Polygons per tile against Detour's 32-bit reference limits (tile and polygon bits);
  checked by a test on the densest cell.
