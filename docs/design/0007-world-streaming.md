# 0007: World streaming

Milestone: M5 · Status: Done · Last updated: 2026-11-11

## Problem

Everything the engine draws today is one scene that is loaded once. `samples/city` is a
360 m block of 10,000 objects; a 4 km world built the same way would be about 1.2 million
objects, hundreds of megabytes of scene text, and far more than the renderer, the physics
world or the memory of a phone can hold at once. There is no level of detail (every
building draws its full mesh at any distance), positions are `float` (a millimetre at 16 km,
visible jitter well before that), the editor can only open a whole scene, and nothing
watches memory.

**Done** (ROADMAP M5) means flying across a 4 km × 4 km world at vehicle speed shows no
visible hitches and stays within a fixed memory budget. Concretely: `samples/city --world`
flies a scripted route across `worlds/metro` with every frame inside the frame budget, the
cells around the camera always loaded before the camera reaches them, and resident streamed
memory never above the platform budget. `benchmark_world` checks this in CI.

## Approach

**Partition.** A world is a `.gworld` file (`{"format": "geni.world"}`): a cell size, a grid
origin and size, and a persistent scene (sun, camera, anything global). Cells are ordinary
scene files at `cells/cell_<x>_<z>.gscene` next to it; a missing file is an empty cell. A
cell's node positions are relative to its corner (`origin + (x, z) * cellSize`), so cell
files stay small numbers and moving a cell's content is moving its root. The reference
world `samples/city/assets/worlds/metro` is 16 × 16 cells of 256 m; its cells place a
handful of district prefabs each, so the checked-in world is a few hundred kilobytes.

**Cooking.** The cooker lists cells and prefabs as it already does (they are scenes), then
cooks each `.gworld` into a `world` asset: the cell table with each cell's scene ID, node
count (prefabs expanded), height range and HLOD scene ID. HLOD is built here: the cell's
static meshes are expanded through its prefabs, transformed into cell space, merged per
material and simplified with meshoptimizer (`meshopt_simplify`, falling back to
`meshopt_simplifySloppy` when topology blocks the target; the best non-empty result is
kept, so pruning never deletes a whole material). Each object goes in at its coarsest LOD;
objects under 1.5 m are dropped, and detailed objects under 12 m (trees) go in as their
bounding box, which at HLOD distance is a few pixels either way. Each cell gets one HLOD
`.gmesh` per material and a small `.gscene` that draws them; for the metro world that is
160 KB of HLOD per cell on average.

**Mesh LOD.** The cooker optimises every triangle mesh with meshoptimizer (vertex cache and
fetch order) and appends up to three LODs to the same index buffer (`.gmesh` version 2: a
list of index ranges with their simplification error in model units). LODs share the
vertex buffer, so a LOD is an index range, not another mesh. The renderer picks per object
the coarsest LOD whose error projects to under `RendererConfig::lodErrorPixels` (1 px) and
sorts by LOD, so instancing still merges equal (mesh, LOD, material) runs; shadow passes use
the same LOD. meshoptimizer is linked by the cooker only.

**Streaming (`world/WorldStreamer`).** `Engine::OpenWorld` loads the cooked world and its
persistent scene. Each frame, after assets update, the streamer takes the focus (the main
camera, or an explicit point) and sorts cells by distance to their bounds:

| Distance | State |
|---|---|
| < `loadRadius` | full cell: scene instantiated, collision on |
| < `hlodRadius` | HLOD scene only |
| beyond | nothing |

Unloading waits for `hysteresis` metres more, so a camera on a boundary does not thrash.
Loading a full cell is: parse the scene and preload its prefabs on workers, instantiate it
under an inactive cell root a few hundred nodes per frame (one shared node budget, nearest
cell first), wait until its meshes and materials are ready, then activate it and hide its
HLOD. So a cell appears in one frame, complete, and the HLOD never leaves a hole. Unloading
deactivates the cell root at once and destroys its objects a slice per frame; asset garbage
is collected after unloads. Cell roots are scene roots named `Cell x,z`; game code sees
ordinary objects.

**Uploads.** Streaming creates GPU buffers every few frames. A GPU-only buffer with
initial data used to be a staging copy plus a blocking submit, which waited for the frame
in flight: 10–30 ms per cell of HLOD meshes. Buffers created between frames are now copied
by commands recorded at the start of the next frame (one barrier for all of them), with the
staging memory freed when that frame completes. A buffer created inside a frame still
uploads at once, and `UpdateBuffer` flushes pending copies first. Textures still upload
synchronously; the metro world streams none.

**Budgets.** `StreamingBudget` (in `EngineConfig`) holds the radii, the node budget per
frame, a resident GPU memory budget for assets and a cap on live scene nodes, with presets
per platform (`ForPlatform`: desktop, mobile, web). A cell is only started when the
estimate (its node count, and the GPU bytes it would add) fits; otherwise the nearest cells
win and the effective radius shrinks, which the overlay shows as "budget limited".
`AssetManager` counts the GPU bytes of every resident mesh and texture; the device reports
the allocator's total. The debug overlay gets a streaming window: cells by state, memory
and nodes against budget, loads in flight, the origin.

**Large coordinates.** Two mechanisms, both always on:

- *Origin rebasing.* `Scene` has a double-precision origin. When the focus is more than
  `rebaseDistance` (1 km) from it, `Scene::ShiftOrigin` moves every root by a whole number
  of cells and components that hold world positions (`Component::OnOriginShift`) adjust.
  Scene coordinates therefore stay within about a kilometre of zero anywhere in the world.
- *Camera-relative rendering.* The renderer subtracts the camera position from instance
  matrices, lights, bone palettes and shadow matrices before upload, and shaders work in
  that space (camera at zero). Vertex positions near the camera keep full float precision.

Physics does not rebase: Jolt runs in double precision (docs/design/0008).

**Editing a region.** The editor opens a `.gworld` (`File > Open World`, `--world`, or the
script command `world`) and loads a rectangle of cells (`region x0 z0 x1 z1`). Each loaded
cell is a root object the editor will not let you move or delete; its children are the
cell's content. Saving writes every loaded cell to its own file and the persistent scene to
its file. An object moved into another loaded cell is saved into that cell. The scene
origin is set to the region's corner so the edited coordinates stay small. Undo, prefabs,
gizmos and play work as in a scene, since a region is just a scene with cell roots.

## Rejected

- **One huge scene with distance culling.** Instantiating and updating 100k+ objects is the
  problem itself; it also gives the editor nothing to load partially.
- **A quadtree or level-based partition (Unreal's World Partition with levels of grids).**
  A uniform grid is enough for a 4 km city and makes cell file names, neighbourhood queries
  and HLOD trivial. A coarser grid for HLOD of HLOD can be added when worlds reach 16 km.
- **Runtime LOD generation.** Needs CPU mesh data at runtime and costs load time; cooking
  is deterministic and keeps meshoptimizer out of the runtime.
- **Separate meshes per LOD.** Four allocations, four assets and four pipeline lookups per
  mesh; index ranges in one buffer are cheaper and instancing stays one buffer bind.
- **Double-precision transforms everywhere.** Doubles in every `GameObject` and in shaders
  (Vulkan 1.1 does not guarantee `shaderFloat64`). Rebasing plus camera-relative rendering
  gives the same visible result with floats.
- **Impostors (billboards) for far cells.** Better for vegetation, but they need a render-
  to-texture bake. Merged simplified meshes reuse the existing materials and pipeline.
- **Unloading a cell in one frame.** Destroying a thousand objects plus their bodies is a
  hitch on its own; slices cost nothing extra.

## Verified

| Question | Answer |
|---|---|
| Hitches across the world | `city --world --route` (release, 1280×720, Apple M4), the 5 km diagonal at 40 m/s: 8.3 ms mean, 9.7 ms p99, 18 ms slowest, no late cell, 100 cell loads, 5 origin shifts. At 80 m/s the slowest frame is 15 ms. |
| Memory budget | Desktop: at most 10,383 objects and 32 MiB of assets (budgets 60,000 and 768 MiB). Mobile budget: 6,588 objects and 19 MiB (15,000 and 256 MiB). |
| Rebasing keeps precision | Scene positions stay within about 1.4 km of zero; bodies stay put across a shift (`tests/test_world.cpp`). |

## To verify

- **Asset sharing and the GPU budget.** The metro world reuses one building kit, so most
  streamed bytes are HLOD meshes and the budget mostly bounds node count. Re-check the
  numbers when worlds use per-district art.
- **Instantiation cost.** A cell is a few hundred objects; profile the node budget per
  frame on the Steam Deck and on a phone (M7) and lower it there.
- **HLOD quality.** Sloppy simplification of merged boxes is crude up close; the HLOD
  radius starts beyond the fog's half distance. Revisit with real art (impostors, merged
  textures).
- **Texture uploads** are still synchronous. A world with per-cell textures needs them on
  the same in-frame path as buffers.
- **Windows GPU and Steam Deck.** The flight above was measured on an Apple M4 only.
- **Editor context.** Cells outside the edited region are not shown in the editor yet;
  draw their HLOD for context when someone edits across a region boundary.
