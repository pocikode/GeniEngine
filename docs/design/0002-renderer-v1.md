# 0002: Renderer v1

Milestone: M2 · Status: Accepted (implemented) · Last updated: 2026-11-10

## Problem

M1 renders the sandbox correctly but it is a first-step renderer:

- Every `RenderCommand` becomes one draw call, in submission order. There is no culling
  and no sorting, so the city block M2 must hold at 60 fps would issue 10,000 unordered
  draws and hit a pipeline/descriptor change per object.
- One point light. `RenderQueue::Draw` reads `lights[0]` and the shaders compute a single
  Blinn-Phong lobe.
- The backbuffer is UNORM and shaders write display values. There is no HDR buffer, no
  tonemapping, no sky and no fog.
- There are no off-screen render targets, so shadows are impossible: `RenderPassDesc` only
  targets the backbuffer.
- Materials are a base colour factor plus one texture, not the glTF metallic-roughness
  model.
- No anti-aliasing.

**Done** means a static city block with 10,000 objects and 100 lights holds 60 fps on the
development Mac (Apple M4) and on a mid-range Windows GPU, and the sandbox shows shadows,
sky, PBR materials and a frame that stays inside the CI performance budget.

## Approach

### Frame structure

One frame, one command buffer, four kinds of pass:

1. **Shadow cascades.** Four depth-only passes, one per cascade, 2048×2048, drawn from the
   light's point of view with a depth-only pipeline and slope-scaled depth bias. Each
   cascade's light view is anchored at the world origin and its centre is snapped to whole
   texels, so the map does not swim as the camera moves. Its depth range reaches
   `RendererConfig::shadowCasterDistance` (200 m) beyond the slice towards the sun, so a
   tall building still casts into a narrow first cascade; the shader's depth bias is given
   in texels and converted per cascade, so the longer range does not loosen it.
2. **HDR scene pass.** MSAA 4× colour (`RGBA16Float`) and depth, cleared, one full-screen
   sky triangle with depth writes off, then the opaque draws sorted and instanced. The
   pass resolves the multisampled colour into a single-sample HDR texture.
3. **Tonemap pass.** One full-screen triangle reads the HDR texture, applies exposure and
   ACES, encodes sRGB, and writes to the UNORM backbuffer. The pass stays open so Dear
   ImGui draws in the same pass.
4. ImGui, as before.

No pass reads a texture another pass in the same frame still writes; each pass ends with
the attachment in the layout the next one expects (render pass final layouts).

### Anti-aliasing: MSAA 4×, not TAA

MSAA is chosen. The scene is forward-rendered, MSAA needs no motion vectors, no history
buffer and no jitter, and it has no failure mode on camera cuts. It also resolves inside
the render pass, so it costs one extra attachment, not another pass.

TAA is rejected for v1: it needs per-object motion vectors (not available before the
instanced/streamed transforms settle in M5), a jittered projection the cascades must
follow, history rejection for the thin geometry in a city block, and an extra resolve
pass with its own budget. It becomes worth revisiting with M5's camera-relative rendering.
MSAA is configurable (`RendererConfig::msaaSamples`, 1 turns it off) because the mobile
tier in M7 needs the option.

### RHI additions

Everything here exists in WebGPU too, so the M8 backend is not blocked:

- **Off-screen targets.** `TextureDesc` gains `usage` (sampled, render target, depth
  attachment) and `sampleCount`. `RenderPassDesc` names its colour, resolve and depth
  textures; an invalid handle still means the backbuffer. Pipelines declare the target
  signature (formats, sample count, depth-only) because Vulkan pipelines are created for a
  render-pass shape.
- **Render targets as sampled textures.** Shadow cascades and the resolved HDR image are
  sampled next frame, so the backend tracks each texture's aspect and final layout.
- **Instanced vertex streams.** `VertexLayoutDesc` gets multiple vertex bindings with a
  per-vertex or per-instance step, and `DrawIndexedInstanced`. Instance transforms are
  vertex attributes (locations 6-9), not a second uniform buffer, because a uniform buffer
  cannot be indexed per instance without a storage buffer and dynamic indexing.
- **Read-only storage buffers** for clustered lighting (WebGPU has them). They are bound
  in the frame set.
- **Depth bias** in `PipelineDesc`, for shadow acne.

The backend caches render passes and framebuffers by their attachment signature. Each
frame slot owns its HDR targets so two frames in flight never write the same image. The
shadow maps are shared by all frame slots and created once (their size does not depend on
the window): everything runs on one queue, and the shadow pass's external dependency
orders its depth writes after the previous frame's shader reads. They use
`Device::GetSampledDepthFormat()`, a depth-only format that can be sampled (D32, or D16,
which every device supports), and are read with nearest filtering.

### Culling, sorting and instancing

`Mesh` stores a local-space bounding sphere and AABB (computed once from the position
attribute). `Frustum` extracts six planes from a view-projection matrix.

`Renderer::DrawFrame`:

1. Compute a world bounding sphere for every command. A skinned mesh is bounded by the
   union of its bind-pose sphere transformed by each bone the mesh uses
   (`RenderCommand::boneCount`); a skinned vertex is a weighted average of those transforms,
   so the union contains it. Frustum-cull against the camera.
2. Sort survivors by shader id, material pointer, mesh pointer, skinned flag. Sorting by
   pipeline/material first is what makes instancing useful: a city block reuses a handful
   of meshes and materials.
3. Merge runs of identical (mesh, material, pipeline) that are not skinned into one
   `DrawIndexedInstanced` with the model matrices in a per-frame instance stream.
4. Cull **every** command, not only the camera-visible ones, against each cascade's light
   frustum: an object behind the camera can shadow the visible street. Casters are grouped
   by mesh into instanced draws; skinned casters are drawn one at a time.

Skinned draws stay one per object: their bone palettes differ, and the palette is still a
per-draw dynamic uniform range. They are sorted next to each other so pipeline changes
stay rare.

### Physically based materials

The default shader follows glTF metallic-roughness: base colour, metallic, roughness,
normal, occlusion and emissive, with GGX specular and a hemispheric ambient from the sky.
Unset material textures bind the 1×1 white texture; the normal map is skipped when its
scale is zero, so white does not become a wrong normal. `GameObject::LoadGLTF` fills the
glTF factors and textures. Base colour and emissive textures are sRGB-encoded in glTF but
stored as RGBA8 UNORM, so the shader decodes them with `pow(c, 2.2)` (the inverse of the
tonemap's encode); the other textures hold linear data. An sRGB texture format would
filter in linear space, but needs the texture's role at load time, which `TextureManager`
does not know yet.

`Material` is unchanged in shape (named parameters), so `.mat` files keep working.

### Lights and cascades

`LightData` gains a type (directional, point, spot), direction, range and cone angles.
`LightComponent` gains matching setters; the scene now collects all three kinds.
The first directional light is the sun and drives the cascades and sky.

Cascades use a practical split blend (0.9 logarithmic) over
`shadowDistance` (`RendererConfig`), fitted to the camera frustum slice corners and
rounded to texel increments to stop shimmering. Cascade selection in the shader uses the
view-space depth with the same splits.

### Clustered forward lighting

A 16×9×24 view-space cluster grid. The CPU assigns each point/spot light to the clusters
its bounding sphere overlaps (sphere vs cluster AABB in view space) and uploads a per-cluster
offset/count array plus one index buffer. The fragment shader reconstructs the cluster from
`gl_FragCoord` and view depth and loops over its lights. 100 lights in a city block is far
below the per-cluster cap (256).

The grid is cheap to compute on the CPU at this light count; the GPU compute version waits
for the profiling data M3 needs for the ECS decision.

### Sky and fog

The sky is an analytic gradient with a sun disc drawn as a full-screen triangle from the
inverse view-projection. Exponential height fog is applied in the PBR shader toward the
horizon colour, so distant city geometry fades into the sky.

### Performance budget

`samples/city` builds the static city block (10,000 objects, 100 lights) from a few
shared meshes/materials and draws it from a fixed camera. It prints the mean frame time
after a warm-up and exits non-zero when `--budget-ms` is exceeded. CTest runs it as
`benchmark_city` with a budget and the `perf` label; CI includes the test when it has a
Vulkan device. CI draws at 640x360 so the software rasterizer fits the test timeout; the
default budget is deliberately loose enough for software Vulkan, and the target on real
hardware is 16.6 ms at 1280x720.

## Rejected

- **Deferred shading.** A G-buffer would make 100 lights trivial, but it is a poor fit for
  the mobile baseline (bandwidth, no native MSAA) and for skinning. Clustered forward is
  the roadmap's decision and stays.
- **Tile-based light culling on the GPU now.** Compute shaders would put the work on the
  right processor, but they add a storage-image path and synchronization for a grid that
  costs well under a millisecond on the CPU at 100 lights.
- **TAA right now.** See above.
- **Instancing through a storage buffer indexed by `gl_InstanceIndex`.** It would avoid
  the new vertex-binding shape, but it moves transforms out of the vertex fetch path and
  makes every shader that reads the model do an extra buffer load. Instance attributes are
  the WebGPU-portable equivalent of what every forward renderer does.
- **A second PNG/JSON material format for PBR.** glTF's model is the one the roadmap names;
  `.mat` keeps its parameter names and gains only the glTF parameter names.
- **MSAA on the shadow maps.** Depth-only passes need no AA and would multiply the memory.

## Verified

| Question | Answer |
|---|---|
| Does MoltenVK support the formats and sample counts used? | Yes: `RGBA16Float` colour, `D32_SFLOAT` depth and 4× MSAA are core Vulkan 1.1 on the M4; the sandbox screenshot tests run with validation layers on. |
| Android baseline still satisfied? | Still Vulkan 1.1 core, no optional features; MSAA 4× and `RGBA16Float` are required by the baseline. Shadow and HDR target sizes are `RendererConfig` fields for the M7 quality tiers. |
| Draw-call reduction from instancing | The city block's 10,000 buildings come from one mesh and eight materials, so the sorted main pass issues 9 object draws instead of 10,000 in the benchmark view. The debug overlay reports submitted objects, culled objects and issued draws. |
| GPU frame time | `build/release/samples/city/city --hidden --frames 60 --warmup 20` on the Apple M4: **8.5 ms/frame (118 fps)** for 10,000 objects and 100 lights at 1280×720, against the 16.6 ms target. The Debug build is ~90 ms/frame; CI runs the Debug build at 640x360 with a 2000 ms budget and the user can tighten `GENI_BENCHMARK_BUDGET_MS`. |
| Shadow map correctness | The cascade matrices are fitted in OpenGL-style depth and converted to the RHI's 0..1 range before upload; the first implementation missed that and put half the scene in shadow. The screenshot tests (`sandbox_*`, `city_f60`) catch regressions. |
