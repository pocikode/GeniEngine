# 0001: Rendering interface and Vulkan backend

Milestone: M1 · Status: Accepted (implemented; waiting for CI on all three platforms) · Last updated: 2026-11-10

## Problem

Rendering is OpenGL 3.3 called directly from many places, and OpenGL types are part of the public API:

- `GLuint` / `GLenum` appear in `graphics/GraphicsAPI.h`, `ShaderProgram.h`, `Texture.h`, `VertexLayout.h` and `render/Mesh.h`. `Mesh` and `Texture` call `gl*` functions themselves.
- Shaders are GLSL 330 source compiled at run time. Every shader must use the same uniform **names** (`uModel`, `uView`, `uLight.color`, `uBones`, …), which `RenderQueue::Draw` and `Material::Bind` set one `glUniform` call at a time.
- The window is created with an OpenGL context in `Engine::Init`, and the debug overlay uses Dear ImGui's OpenGL backend.

OpenGL is capped at 4.1 and deprecated on macOS, does not exist on the mobile and web targets in the form used here, and cannot express what M2 and M5 need. M1 replaces it with a rendering interface (RHI) and one backend, Vulkan.

**Done** means the M0 sandbox sample renders the same image on Vulkan on macOS (MoltenVK), Windows and Linux with zero validation errors, no file outside `src/rhi/vulkan/` includes a Vulkan header, and no OpenGL code is left.

## Approach

**Three layers.** `rhi/` is an interface with no backend types. `rhi/vulkan/` implements it. The renderer (`RenderQueue`, `Material`, `Mesh`, `Texture`) is rewritten to use only `rhi/`. Game code keeps using the renderer and never sees the RHI.

**A thin interface shaped like WebGPU, not like Vulkan.** M8 needs a WebGPU backend behind the same interface, so the RHI only has concepts both APIs share:

| RHI object | Notes |
|---|---|
| `Device` | Creates everything else; owns the frame loop (`BeginFrame` / `EndFrame`) |
| `Swapchain` | Recreated on resize; hides surface formats |
| `Buffer`, `Texture`, `Sampler` | Created from plain description structs |
| `ShaderModule` | Created from SPIR-V bytes |
| `Pipeline` | Shaders + vertex layout + blend/depth/raster state + target formats, fixed at creation |
| `BindGroup` | A set of buffers and textures bound together (a Vulkan descriptor set) |
| `CommandList` | `BeginRenderPass(targets, load/store ops)`, set pipeline, bind groups, draw, `EndRenderPass` |

Resources are referred to by small generational handles (index + generation), not pointers, so a stale handle is caught by an assertion instead of crashing. Destruction is deferred until the GPU has finished the frames that used the resource.

**Uniforms move from names to blocks.** Three bind groups replace per-uniform calls: per frame (camera, lights), per material (factors, textures), per draw (model matrix; bone palette for skinned meshes). Per-draw data lives in one large uniform buffer indexed by dynamic offset. Push constants are not used, because WebGPU has none.

**Shaders are compiled offline.** GLSL 4.50 sources in `assets/shaders/` are compiled to SPIR-V by a CMake build step and loaded as binary. Reflection data from the SPIR-V is used to check, at pipeline creation, that the shader's bindings match what the renderer provides. `.mat` files keep their format and refer to the compiled shaders. The built-in default shader moves out of `GraphicsAPI.cpp` into a file.

**Libraries** (ROADMAP rule 6): the system Vulkan loader (linked directly through CMake's `find_package(Vulkan)`), Vulkan Memory Allocator for GPU memory, SPIRV-Reflect for SPIR-V reflection, and GLFW's Vulkan surface support. The ImGui overlay switches to ImGui's Vulkan backend. A function loader such as volk was dropped: it would force ImGui's no-prototypes mode and buys nothing until a platform lacks a system loader.

**Order of work.** Each step ends with the sandbox sample still running.

1. Screenshot harness: render the sandbox off-screen for a fixed number of fixed steps, save a PNG, compare against a reference with a tolerance. Capture the references from the **current OpenGL renderer**, so "renders identically" is measured, not judged by eye.
2. Introduce `rhi/` and the Vulkan backend next to the untouched OpenGL renderer, proven by its own off-screen test.
3. Offline shader pipeline, and uniform blocks in place of named uniforms.
4. Switch the renderer to the RHI: static meshes, textures, skinned meshes, the overlay. The OpenGL screenshots are the target.
5. Delete the OpenGL code, GLEW, and the GL types in public headers. Regenerate the references from Vulkan.

*Changed when work started:* the original step 2 moved the OpenGL code behind `rhi/` first. That backend would have been deleted in step 5, so it was skipped. The interface is checked against the screenshot references instead of against a second backend.

**Mobile limits apply from the start** (ROADMAP rule 5): no feature is required that the baseline below lacks, render passes are described with explicit load/store operations so tile-based GPUs are not penalised, and every texture goes through one creation path so that BC and ASTC variants can be added in M3 without touching callers.

## Rejected

- **Keep OpenGL as a second backend.** Doubles the work of every later rendering feature for a backend that no target platform needs.
- **An RHI that mirrors Vulkan one to one.** Simplest to write now, but queue families, explicit layouts and push constants would leak into the renderer and make the WebGPU backend a rewrite.
- **Use an existing abstraction library (bgfx, sokol, or a native WebGPU implementation) instead of our own RHI.** Would also cover web, but the roadmap's decision is Vulkan, and M5 streaming needs direct control over memory budgets, upload scheduling and resource lifetime that these layers hide or own.
- **Compile GLSL at run time.** Keeps hot reload trivial, but ships a compiler in every build and is not workable on mobile. Hot reload returns in M4 by re-running the offline compiler.
- **Switch to Slang now.** The roadmap's default is GLSL because the existing shaders are GLSL. Revisit when a second backend needs a second shading language (M8).

## Decided during implementation

- **Colour stays UNORM end to end** (UNORM swapchain, UNORM textures), as the OpenGL renderer had it, so the screenshots could be compared. Linear lighting with an sRGB backbuffer arrives with HDR and tonemapping in M2.
- **Mip chains are built on the CPU** when a texture is created, so the backend needs no blit path. The cooker takes this over in M3.
- **Per-draw data** (model matrix, bone palette) goes into one host-visible buffer per frame in flight, addressed by dynamic offsets. Material parameters live in a small GPU buffer per material, updated only when a parameter changes.
- **The shader contract** (sets, bindings, block layouts) is written down in `include/render/ShaderInterface.h` and checked with SPIRV-Reflect when a shader is created. Material parameters keep their names: `Material::SetParam` and `.mat` files did not change.
- **Pipelines include only the vertex attributes a shader reads.** The stride still covers the whole vertex, so one mesh layout works with shaders that read different subsets.

## Verified (2026-11-10)

| Question | Answer |
|---|---|
| Which Vulkan version and optional features do MoltenVK and mainstream Android devices both support? | MoltenVK 1.4.2 on an Apple M4 reports Vulkan 1.4 (conformance 1.4.4). The engine requests Vulkan 1.1 and enables no optional device features. Android was not checked on a device; that check moves to the start of M7. |
| Dynamic rendering or classic render pass objects? | Classic render passes, cached per combination of load/store operations. Framebuffers are made with one compatible pass. `BeginRenderPass` hides this. |
| Can CI run Vulkan with validation layers? | Linux: lavapipe (software Vulkan) under xvfb with the apt validation layers. macOS: MoltenVK from Homebrew; CI probes for a device with `vulkaninfo` and falls back to the tests that need no GPU, with a warning, if the runner has none. Windows: no driver on hosted runners; the build and unit tests run. **Not yet confirmed by a CI run.** |
| How close do OpenGL and Vulkan screenshots get? | On the Apple M4: frame 1 differs by at most 1/255 per channel; frame 120 has one pixel (a triangle edge) of 921,600 over the tolerance (8/255 per channel, at most 0.5% of pixels allowed). The references were then regenerated from Vulkan. Whether lavapipe output stays within this tolerance of the MoltenVK references is open until the first Linux CI run. |
| Which GLSL-to-SPIR-V compiler, and where from? | `glslangValidator`: Homebrew `glslang` on macOS, apt `glslang-tools` on Linux, the LunarG Vulkan SDK on Windows. Configure fails with install instructions if it is missing. |
