# 0001: Rendering interface and Vulkan backend

Milestone: M1 · Status: Proposed · Last updated: 2026-11-10

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

**Libraries** (ROADMAP rule 6): a Vulkan function loader, Vulkan Memory Allocator for GPU memory, a SPIR-V reflection library, and GLFW's Vulkan surface support. The ImGui overlay switches to ImGui's Vulkan backend.

**Order of work.** Each step ends with the sandbox sample still running.

1. Screenshot harness: render the sandbox off-screen for a fixed number of fixed steps, save a PNG, compare against a reference with a tolerance. Capture the references from the **current OpenGL renderer**, so "renders identically" is measured, not judged by eye.
2. Introduce `rhi/` and move the existing OpenGL code behind it without changing behaviour. This proves the interface against working code.
3. Offline shader pipeline, and uniform blocks in place of named uniforms.
4. Vulkan backend: device, swapchain, static meshes, textures, then skinned meshes, then the overlay.
5. Vulkan passes the screenshot tests on all three platforms in CI with validation layers on. Delete the OpenGL backend, GLEW, and the GL types in public headers.

**Mobile limits apply from the start** (ROADMAP rule 5): no feature is required that the baseline below lacks, render passes are described with explicit load/store operations so tile-based GPUs are not penalised, and every texture goes through one creation path so that BC and ASTC variants can be added in M3 without touching callers.

## Rejected

- **Keep OpenGL as a second backend.** Doubles the work of every later rendering feature for a backend that no target platform needs.
- **An RHI that mirrors Vulkan one to one.** Simplest to write now, but queue families, explicit layouts and push constants would leak into the renderer and make the WebGPU backend a rewrite.
- **Use an existing abstraction library (bgfx, sokol, or a native WebGPU implementation) instead of our own RHI.** Would also cover web, but the roadmap's decision is Vulkan, and M5 streaming needs direct control over memory budgets, upload scheduling and resource lifetime that these layers hide or own.
- **Compile GLSL at run time.** Keeps hot reload trivial, but ships a compiler in every build and is not workable on mobile. Hot reload returns in M4 by re-running the offline compiler.
- **Switch to Slang now.** The roadmap's default is GLSL because the existing shaders are GLSL. Revisit when a second backend needs a second shading language (M8).

## To verify

Check these in the first week of M1, before step 2, and record the answers here.

| Question | Default if not settled |
|---|---|
| Which Vulkan version, and which optional features, do the current MoltenVK release and mainstream Android devices both support? | Target Vulkan 1.1 core features only, and treat anything newer as optional |
| Dynamic rendering or classic render pass objects inside the backend? | Classic render passes; the RHI's `BeginRenderPass` hides the choice |
| Can CI run Vulkan with validation layers on hosted macOS, Windows and Linux runners (software implementation where there is no GPU)? | Run screenshot tests on Linux in CI; run macOS and Windows locally before each merge |
| How close do OpenGL and Vulkan screenshots get (sRGB handling, clip-space depth, Y direction)? | Per-pixel tolerance with a small allowed fraction of differing pixels; regenerate references from Vulkan once OpenGL is deleted |
| Which GLSL-to-SPIR-V compiler is used, and how is it obtained on each platform and in CI? | Require the Vulkan SDK on developer machines and install it in CI |
