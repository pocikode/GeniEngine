# 0004: Jobs, assets and the cooker

Milestone: M3 · Status: Done · Last updated: 2026-11-10

## Problem

M3 makes everything after it possible: streaming (M5) and the editor (M4) both need
assets that are loaded by ID, cooked ahead of time and decoded without stalling a frame.
Today the engine has none of that:

- `GameObject::LoadGLTF` parses glTF on the main thread, creates GPU buffers and
  textures inline, and every call re-reads the file. `samples/city` builds its 10,000
  objects in C++ instead of loading a scene.
- `Texture` always decodes a PNG, expands it to RGBA8 and builds its mip chain on the
  main thread. The comment on the class says compressed formats are "for M3".
- There is no job system, no stable asset identity and no cache that outlives a
  component's `shared_ptr`.
- Meshes, materials and animations only exist as objects constructed in code.

**Done** means `samples/city` loads its block from a cooked scene file, no glTF parser is
linked into the engine, textures are KTX2 with BC7 and ASTC variants, asset loads run on
worker threads and are finalized on the main thread, and every resource has a stable ID
and reference-counted handle.

## Approach

**Jobs (`core/JobSystem`).** A fixed worker pool owns a task queue. `Schedule` returns a
`JobHandle`; `ParallelFor` splits a range into one task per grain; `PostToMain` queues a
callback that `Engine` drains once per frame. `JobSystem::Wait` and `ParallelFor` run
queued tasks on the waiting thread, so the main thread helps instead of idling and a
`ParallelFor` inside a job cannot deadlock the pool. `WaitIdle` is only for shutdown and
tests. Worker threads never touch the render device: decoding produces CPU data, and the
main thread creates GPU objects in `AssetManager::Update`.

**Identity (`assets/AssetID`).** An asset ID is the 64-bit FNV-1a hash of a logical name
(`mesh:models/hierarchy.gltf:@Triangle:0`, `texture:textures/checker.png`). The cooker
writes a `cooked_manifest.json` mapping ID, type, cooked path and source, and refuses to
write two assets with the same ID. IDs are stable across machines and runs.

A logical name starts from the source file's *root name*: `guid:<16 hex digits>` when
the file has a `<file>.meta` sidecar (`{"guid": "..."}`), its path otherwise. With a
`.meta`, renaming or moving the file (with its `.meta`) keeps every ID it produces;
`geni_asset_cooker --write-meta` creates missing sidecars. Objects inside a glTF are
keyed by `@name` when the name is unique in the file, so reordering meshes or materials
keeps their IDs, and by index otherwise. Code that starts from a path uses
`AssetManager::FindBySource` rather than hashing a name itself.

**Cooker (`tools/asset_cooker`, library `geni_cook`).** Offline conversion:

| Input | Output |
|---|---|
| glTF/GLB scene | `.gscene` text scene + one `.gmesh` per primitive + `.ganim` per clip |
| glTF material | `.gmat` binary (shader names, parameters, texture IDs) |
| PNG/JPEG image | KTX2 with a full mip chain, one file per target format and usage |

The cooker is the only place `cgltf` and `stb_image` are compiled. The engine links
neither; `GameObject::LoadGLTF` is removed. Scene JSON is flat and diff-friendly: nodes
are an array with a parent index, components are `{ "type": ..., "properties": ... }`.
Meshes and animations are binary because they are large and never hand-edited.

**Formats.** `.gmesh` stores the `VertexLayout`, interleaved vertices, 32-bit indices,
bounds and, for skinned primitives, joint names plus inverse bind matrices. `.ganim`
stores clips and transform tracks. `.gmat` stores the shader pair and typed parameters.
All files start with a four-byte magic and a `uint32` version; readers reject anything
else. KTX2 stores one format per file, so the cooker emits `_bc7.ktx2` and `_astc.ktx2`
variants. `Device::IsTextureFormatSupported` chooses at load time, preferring BC on
desktop and ASTC on mobile. The writer follows the KTX 2.0 specification (format-specific
Data Format Descriptor, `KTXwriter` key, levels stored smallest first), and CI runs
Khronos's `ktx validate` over every cooked file.

**Compression.** BC7 comes from `bc7enc_rdo`, BC5 from its `rgbcx`, ASTC 4x4 from Arm's
`astc-encoder` (release 5.7.1, built for AVX2 on x86-64 and NEON on arm64; its
invariance option keeps the output identical across instruction sets). Both are
cooker-only static libraries; nothing from them ships in the engine. ASTC effort is
`--texture-quality` (default medium). There is no BC1 variant: Vulkan exposes the BC
formats as one feature, so every device with BC1 also has BC7, and BC1 drops alpha.

A texture's usage decides its filter and formats. The glTF material slot sets it
(base colour and emissive are colour, metallic-roughness and occlusion are linear,
normal maps are normal); a standalone image is colour unless its `.meta` says otherwise.
An image used two ways is cooked once per usage.

| Usage | Mips | Desktop | Mobile | Fallback |
|---|---|---|---|---|
| Colour | box filter in linear light | BC7 sRGB | ASTC 4x4 sRGB | RGBA8 sRGB |
| Linear | box filter on stored values | BC7 | ASTC 4x4 | RGBA8 |
| Normal | box filter, then renormalized | BC5 (X, Y) | ASTC 4x4 | RGBA8 |

Colour textures use sRGB formats so the sampler decodes before filtering; shaders read
linear values. Normal-map shaders rebuild Z from X and Y, which BC5 requires.

**Handles (`assets/AssetHandle`, `AssetManager`).** `AssetHandle<T>` points at a slot
holding a `shared_ptr<T>`; copying a handle increments the resource's reference count,
and the cache keeps one reference so repeated loads are free. `CollectGarbage` drops
slots that nothing else references (no other handle and no `shared_ptr` to the object);
the device defers the GPU destruction until in-flight frames are done. Engine runs it
every 240 frames, and M5 streaming will run it when a region unloads.

An `async` load returns immediately; the slot becomes ready when the worker has decoded
and the main thread has uploaded. `AssetManager::Update` drains completed work once per
frame. A synchronous load decodes on the calling thread, and if the same asset is
already loading it waits for that load instead of returning a pending handle. All load
types share one code path (`StartLoad`). A material's textures load asynchronously like
any other asset; the material becomes ready once every texture has finished or failed,
so `Update` never reads or decodes a texture itself.

Large scenes are instantiated over several frames with `SceneInstantiation`
(docs/design/0005); `samples/city` steps 1,000 nodes per frame and records the slowest
frame while loading, which `benchmark_city` checks against
`GENI_LOAD_FRAME_BUDGET_MS`.

## Rejected

- **Keep runtime glTF and add caching.** The parser, image decode and GPU uploads would
  still be on the frame that first needs an object, and every platform would ship a
  parser it never needs after cooking.
- **A single-file container per scene (like a pak).** Nice for shipping, but it hides
  diffs and makes per-asset hot reload (M4) harder. A manifest plus loose cooked files
  is enough for the reference game.
- **UASTC/ETC1S KTX2 with runtime transcoding (basis_universal).** One texture file for
  every platform, but it puts a large transcoder in the runtime and adds a decode step
  on the loading path. M3's target platforms have native BC and ASTC; pre-cooking both
  is simpler and faster at runtime.
- **A general task graph with dependencies.** `Schedule`/`ParallelFor`/`PostToMain`
  covers cooking, loading and animation; a graph can come with streaming in M5.
- **Third-party job system (Taskflow, enkiTS).** The engine needs a main-thread
  completion queue and a deterministic wait, which is about 250 lines here. The first
  version had a double-completion bug in `ParallelFor` (it returned after half the
  tasks); the fix added a stress test, and waits now run queued work, which was the
  main thing enkiTS would have added. Revisit if M5 needs priorities or task graphs.
- **libktx for writing KTX2.** Conformant by construction, but a large dependency for
  writing one header and a descriptor. The hand-written writer is checked by
  `ktx validate` in CI instead.
- **A BC1 variant.** See Compression.

## To verify

- **ASTC on mobile (M7).** `astc-encoder` output is standard ASTC; check the texture
  budget and quality tier on a real Android device when the mobile backend lands.
- **BC7 on MoltenVK.** Metal supports BC on macOS but not on iOS; the runtime picks ASTC
  there. Verify `IsTextureFormatSupported` against MoltenVK when M7 starts.
- **Cook determinism.** Encoders are deterministic for a pinned revision. Bump the
  submodule and the cooked files together, then rerun the screenshot tests.
- **BC7 quality.** `bc7enc_rdo` is frozen upstream and its scalar `bc7enc` is the weaker
  of its two BC7 encoders. If quality or cook time becomes a problem, move to
  `bc7e.ispc` (needs ISPC) or Compressonator.
