# 0022: Terrain

Milestone: After v0.1 (rendering) · Status: Accepted · Last updated: 2026-11-13

## Problem

Open worlds are built from boxes and imported meshes on a flat ground plane. There is no
heightmap terrain: no hills to drive over, no slopes for the physics, nothing to scatter
trees on. Modelling terrain in a DCC tool and importing it as glTF works for a small
patch, but gives no LODs that join without cracks, no cheap height queries and a
triangle-mesh collider far more expensive than a height field.

**Done** means a `.gterrain` file (a heightmap image plus size, height and colours) cooks
into chunked meshes with crack-free LODs, a terrain material and a height field;
instantiating its scene gives drawable, shadow-casting chunks and a `TerrainComponent`
with a Jolt height-field collider and `GetHeight`/`GetNormal` queries; Geni City has a
terrain view (`city --terrain`) with a screenshot test.

## Approach

- **Source** `<name>.gterrain`: `{"format": "geni.terrain", "version": 1, "heightmap":
  "hills.png", "size": [512, 512], "height": 60, "chunks": 8, "chunkQuads": 32,
  "colors": {"grass": [...], "rock": [...], "snow": [...]}, "rockSlope": 35,
  "snowHeight": 0.75, "uvScale": 0.25}`. The heightmap is an 8- or 16-bit grey PNG
  (16-bit read with full precision), resampled bilinearly to a grid of `chunks *
  chunkQuads + 1` samples per side; heights are `value * height` metres. The terrain's
  origin is its corner at height 0; it spans +X and +Z.
- **Meshes**: one `.gmesh` per chunk (grid of `chunkQuads` quads, shared border samples)
  with position, normal (central differences over the whole grid, so borders match), UV
  (world XZ times `uvScale`) and a colour attribute holding the grass/rock/snow weights
  (rock by slope above `rockSlope`, snow above `snowHeight` of the range, soft blends). Two
  coarser LODs are simplified with meshoptimizer with the chunk border locked, so
  neighbouring chunks at different LODs still share every border vertex: no cracks, no
  skirts. The renderer's existing LOD selection picks them.
- **Material**: a built-in `builtin:terrain` shader (`terrain.slang`): the three colours
  (material params `uGrass`, `uRock`, `uSnow`) blended by the vertex weights, broken up by a
  little world-space noise so large slopes are not flat colour, lit by `geni.lighting` and
  fogged like the standard shader.
- **Height field** asset (type `heightfield`): sample count, cell size and the float
  heights. `TerrainComponent` (on the scene's root, property `heights`) loads it, answers
  `GetHeight(world x, z)` and `GetNormal` (bilinear), and owns one static `RigidBody` with
  a new `HeightFieldCollider` (Jolt `HeightFieldShape`; the grid is padded by repeating
  its last row and column to the block size Jolt needs).
- **Scene** asset under the `.gterrain`'s path: a root with the `TerrainComponent` and one
  child per chunk with a `MeshComponent`. It is placed like any scene (prefab-style); a
  streamed world can put one terrain per cell.

Order: physics height field; cooker; shader and component; city view; docs.

## Rejected

- **GPU tessellation or clipmaps.** Fewer vertices and seamless LOD, but tessellation is
  not in WebGPU and clipmaps need vertex texture fetch in a custom pipeline; chunked meshes
  reuse the mesh, culling, LOD and shadow paths that exist.
- **Skirts to hide cracks.** Border-locked simplification has no cracks to hide.
- **Texture splatting with a splat map.** Needs four more textures and samplers per draw;
  vertex weights and colours first, textures later.
- **Navigation on terrain.** Recast reads static colliders (0014); terrain triangles are
  an extension of `NavGeometry` and the world cooker, left for when a game needs it.

## To verify

- Jolt's height-field precision for a 512 m, 60 m terrain (it quantises heights per
  block). Default: accept the default 8-bit block quantisation.
