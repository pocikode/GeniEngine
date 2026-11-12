# 0003: Slang shaders

Milestone: before M3 · Status: Done · Last updated: 2026-11-10

## Problem

M1 chose GLSL 450 compiled by glslangValidator, to be revisited with the WebGPU backend
(M8). Three things made it worth deciding now instead:

- **No code sharing.** GLSL has no modules. The `Frame` block was declared in nine files,
  and `samples/sandbox/assets/shaders/lit.frag` was a 309-line hand-synced copy of
  `src/render/shaders/default.frag`.
- **Web needs a second shader output.** WebGPU takes WGSL. With GLSL, M8 needs a
  SPIR-V → WGSL translator (Tint or Naga) in the toolchain.
- **Cost grows with every milestone.** At the switch there were 15 shader files (~1,000
  lines). The M3 cooker and M4 hot reload build on whichever compiler is chosen.

**Done** means every shader is Slang, the screenshots match the GLSL references within the
existing tolerance, the city benchmark is no slower and there are no validation errors.

## Approach

**Compiler.** `slangc` from a pinned release of shader-slang/slang (`GENI_SLANG_VERSION`
in `cmake/GeniShaders.cmake`), downloaded at configure time with a SHA-256 check into
`.cache/slang/` (gitignored, shared by all build folders). It is not looked up on `PATH`:
other software ships its own `slangc` (kitty.app does), and a pinned version keeps output
identical everywhere. `-DGENI_SLANGC=<path>` overrides it.

**Flags.** `-target spirv -profile spirv_1_3` (the Vulkan 1.1 baseline),
`-matrix-layout-column-major` (memory layout of glm), `-warnings-as-errors all`, and
`-I src/render/shaders` so every shader can import the engine modules.

**Files.**

- `src/render/shaders/geni/` holds modules without entry points: `frame.slang` (the
  `Frame` block, `ModelMatrix`, `NormalMatrix`, full-screen triangle), `skinning.slang`
  (bones, `SkinMatrix`), `lighting.slang` (lights, clusters, shadows, BRDF,
  `ShadeSurface`, `ApplyFog`) and `standard.slang` (the glTF material in set 1 and
  `StandardVertex` / `SkinnedVertex` / `StandardFragment`; it re-exports the others).
- One `.slang` file per pipeline, with entry points named `vertexMain` and
  `fragmentMain`. CMake finds them by name and compiles each to its own SPIR-V module.
- Embedded shaders keep their names: `default.slang` → `default_vert.spv.h` with array
  `geni_spirv_default_vert` (`cmake/GeniEmbedSpirv.cmake` writes the header).
- Asset shaders compile to `<file>.slang.vert.spv` / `.frag.spv`. A `.mat` file names the
  `.slang` source for each stage (both may be the same file) and `Shader::Load` appends
  the stage suffix.

**Conventions** (each one is a mistake that compiles without error):

- Matrices: `mul(M, v)` is GLSL's `M * v`. `M[i]` is **row** i (GLSL: column i). The
  `float4x4(a, b, c, d)` constructor takes rows, so `ModelMatrix` transposes the four
  instance columns.
- Vertex inputs and varyings use `[[vk::location(n)]]` in **one flat struct** per entry
  point. Slang shifts explicit locations in nested structs and in later entry-point
  parameters.
- Resources use `[[vk::binding(binding, set)]]`. Uniform blocks are
  `ConstantBuffer<T>` (std140), storage buffers `StructuredBuffer<T>` (std430), textures
  `Sampler2D` (combined image sampler).
- Use `SV_VulkanVertexID`, not `SV_VertexID`: the latter subtracts the base vertex and
  needs the `DrawParameters` capability, which Vulkan 1.1 does not enable.
- There is no `inverse()`. `NormalMatrix` uses the cofactor matrix, which equals the
  inverse transpose for the affine matrices instances use.

**Reflection** still reads the SPIR-V with SPIRV-Reflect. Member and resource names are
preserved; block type names get a layout suffix (`Material_std140`), which
`ShaderReflection` strips.

## Rejected

- **Stay on GLSL and add `#include` (GL_GOOGLE_include_directive).** It fixes the copied
  code but not the WGSL output, and leaves no generics or interfaces for material
  variants later.
- **HLSL with DXC.** Mature SPIR-V output but no WGSL target, so M8 still needs a
  translator. Slang reads most HLSL anyway.
- **Using `slangc` from the Vulkan SDK or `PATH`.** The version varies by machine and by
  SDK release, and Homebrew has no `slangc` formula (`brew info slang` is S-Lang).
- **Linking the Slang library into the engine now** (runtime compilation). Hot reload is
  M4; offline compilation is enough until then.

## To verify

- **WGSL (M8).** Every engine shader already compiles with `-target wgsl`. WGSL has no
  combined texture-samplers, so each `Sampler2D` becomes a texture and a sampler on
  overlapping bindings (warning E39001). The WebGPU backend needs a binding remap
  (for example the sampler at binding + 16). Decide when M8 starts.
- **Android / iOS (M7).** The SPIR-V uses only the `Shader` capability today. Check the
  capability list again when shaders gain features.
- **Updating Slang.** Bump `GENI_SLANG_VERSION` and the hashes together, then run the
  screenshot tests without `GENI_UPDATE_REFERENCES`.
