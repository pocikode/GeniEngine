# 0029: GPU-driven culling

Milestone: After v0.1 (rendering) · Status: Done · Last updated: 2026-11-13

## Problem

The renderer tests every object's bounding sphere against the camera on the CPU, then
packs the visible ones' matrices and merges runs into instanced draws. The RHI had no
compute shaders and no indirect draws, so no work could move to the GPU: neither culling
nor, later, occlusion culling against a depth pyramid or GPU LOD selection, which are what
large open worlds need.

**Done** means the RHI has compute pipelines, read-write storage buffers, indirect
buffers, `Dispatch`, a compute-to-draw barrier and `DrawIndexedIndirect` (Vulkan backend,
WebGPU-shaped); the renderer can cull non-skinned indexed meshes in a compute pass and
draw them with indirect draws (`RendererConfig::gpuCulling`), with the same picture as the
CPU path; a screenshot test runs the city block that way.

## Approach

- **RHI**: `ShaderStage::Compute`, `BindingType::StorageBufferReadWrite`,
  `BufferUsage::Indirect`, `ComputePipelineDesc` / `Device::CreateComputePipeline`
  (destroyed with `DestroyPipeline`), `CommandList::Dispatch` (outside render passes),
  `ComputeToDrawBarrier`, `DrawIndexedIndirect(buffer, offset)` with
  `DrawIndexedIndirectArgs`. `SetPipeline` binds either kind; `SetBindGroup` follows the
  bound pipeline's bind point. Indirect draws keep `firstInstance` at 0 (a non-zero one is
  an optional Vulkan feature); the instance buffer is bound at the group's offset instead.
  Shader files may name a `computeMain` entry point (suffix `comp`).
- **Renderer** (`gpuCulling` on): items that are not skinned and have an index buffer skip
  the CPU frustum test. After sorting, runs that would merge into one instanced draw become
  groups; each group's `DrawIndexedIndirectArgs` (instance count 0) are written to a
  per-frame buffer, every candidate's matrix, camera-relative sphere, group and group base
  to an input buffer, and `cull.slang` (64 threads per group) tests each sphere against
  the camera-relative frustum planes and, if visible, adds one to its group's instance
  count atomically and copies the matrix into the group's range of the visible buffer. A
  barrier, then the HDR pass draws each group with `DrawIndexedIndirect`. Skinned and
  non-indexed meshes keep the CPU path; shadow cascades still cull on the CPU.
- `RenderStats::gpuCullCandidates`; `city --gpu-culling` and `screenshot_city_gpu_culling`
  (the same reference as `screenshot_city`).

## Rejected

- **Culling every pass on the GPU at once (shadows too).** Cascades cull all casters with
  per-cascade frusta; the same kernel with a cascade index is the next step, kept out to
  keep this change reviewable.
- **Multi-draw indirect with a draw count buffer.** Needs `multiDrawIndirect` and
  `drawIndirectCount` (not core in Vulkan 1.1, absent in WebGPU); one indirect draw per
  group works everywhere.

## To verify

- The win. In the city block (10,001 objects, 9 groups) the frame time is unchanged on an
  Apple M4 (8.35 ms CPU culling, 8.38 ms GPU culling, release, 1280x720): culling 10,000
  spheres on the CPU was cheap. The value is the foundation for occlusion culling (a
  hierarchical depth buffer) and GPU LOD, which is when it should become the default.
  Default: off.
