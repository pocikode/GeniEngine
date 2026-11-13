# 0023: Vegetation

Milestone: After v0.1 (rendering) · Status: Accepted · Last updated: 2026-11-13

## Problem

Terrain (0022) is bare. Covering 512 m of hills with trees as `GameObject`s means tens of
thousands of objects, each with a transform walk, a component update and a scene-file
entry; the metro world already spends its object budget on buildings. Placing them by hand
is out of the question, and a scatter script in Lua would redo the work on every load.

**Done** means a `.gterrain` lists scatter rules (a prefab, a density, slope and height
limits, a scale range, a seed); the cooker places the instances on the terrain; a
`FoliageComponent` draws every instance of the prefab's meshes without objects, culled by
cell and draw distance, casting shadows; the city's terrain view has a forest, the
screenshot test covers it and a benchmark keeps it inside the frame budget.

## Approach

- **The plant is a prefab** (`.gprefab`): its `MeshComponent`s, with their transforms
  relative to the prefab root, are the parts drawn for each instance. Trees are authored
  and previewed in the editor like any prefab.
- **Scatter rules** in `.gterrain`: `"scatter": [{"prefab": "...", "density": 0.004
  (per m²), "maxSlope": 28, "minHeight": 0.05, "maxHeight": 0.7 (fractions of the
  terrain height), "scale": [0.8, 1.3], "seed": 1, "drawDistance": 350}]`. The cooker
  samples a jittered grid (one candidate per `1 / density` m²), keeps candidates that pass
  the slope and height tests, gives each a random yaw and scale and the terrain height at
  its position, and writes them as a `foliage` asset (count, then position, yaw, scale per
  instance, sorted into 32 m cells). Each rule becomes a child of the terrain root with a
  `FoliageComponent` (`prefab`, `instances`, `drawDistance`).
- **`FoliageComponent`** loads the instances and the prefab (synchronously, both small),
  collects the parts (mesh, material, local transform) and loads their meshes and
  materials asynchronously. Every `Update` and `EditUpdate` it finds the view position
  (the editor's view override, else the main camera), skips cells beyond `drawDistance`,
  and submits one `RenderCommand` per instance per part. The renderer's culling, sorting,
  LOD selection, instancing (identical mesh + material merge into one draw) and shadow
  passes work unchanged. `SetInstances` lets a game place foliage at run time.
- City: a `pine.gprefab` (kit trunk and an elongated crown) scattered over the hills;
  `screenshot_terrain` shows the forest; `benchmark_terrain` measures the view.

## Rejected

- **One object per tree.** See Problem.
- **GPU instancing with a per-instance buffer owned by the component.** Faster to submit,
  but it bypasses the renderer's culling, LOD and shadow paths; the existing instancing
  already merges identical commands. Revisit with GPU-driven culling.
- **Billboard impostors for distant trees.** Better far LOD; the mesh LODs and the draw
  distance are enough for the reference view. Later.
- **Wind animation.** Needs a vertex shader with a per-instance phase; later.

## To verify

- CPU cost of submitting ~20,000 commands per frame. Default: draw distance 350 m and
  cell culling; measured by `benchmark_terrain`.
