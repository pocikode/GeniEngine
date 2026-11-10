#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

// The contract between the renderer and every shader. Shaders are GLSL 450, compiled
// offline to SPIR-V (cmake/GeniShaders.cmake), and use three descriptor sets:
//
//   set 0  per frame     binding 0: uniform Frame    (FrameUniforms)
//   set 1  per material  binding N: one uniform block of material parameters, any name,
//                                   whose member names match Material::SetParam names
//                        binding M: sampler2D textures, matched by name to
//                                   Material::SetParam(name, texture)
//   set 2  per draw      binding 0: uniform Draw     (DrawUniforms)
//                        binding 1: uniform Bones    (BoneUniforms, skinned meshes)
//
// Vertex inputs use the locations in VertexElement (graphics/VertexLayout.h). Clip space
// has Y up and depth 0..1; CameraComponent's OpenGL-style projection is converted by the
// renderer, so shaders just compute projection * view * model * position.
//
//   layout(set = 0, binding = 0) uniform Frame
//   {
//       mat4 view;
//       mat4 projection;
//       vec4 cameraPos;     // xyz
//       vec4 lightColor;    // rgb; zero when the scene has no light
//       vec4 lightPosition; // xyz
//   } uFrame;
//   layout(set = 2, binding = 0) uniform Draw { mat4 model; } uDraw;
//   layout(set = 2, binding = 1) uniform Bones { mat4 bones[128]; } uBones;
namespace Geni::ShaderInterface
{

constexpr uint32_t FrameSet = 0;
constexpr uint32_t MaterialSet = 1;
constexpr uint32_t DrawSet = 2;

constexpr uint32_t FrameBinding = 0;
constexpr uint32_t DrawBinding = 0;
constexpr uint32_t BonesBinding = 1;

constexpr uint32_t MaxBones = 128;

// std140 layouts; every member is 16-byte aligned.
struct FrameUniforms
{
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 cameraPos;
    glm::vec4 lightColor;
    glm::vec4 lightPosition;
};

struct DrawUniforms
{
    glm::mat4 model;
};

struct BoneUniforms
{
    glm::mat4 bones[MaxBones];
};

static_assert(sizeof(FrameUniforms) == 176, "FrameUniforms must match the std140 Frame block");
static_assert(sizeof(DrawUniforms) == 64, "DrawUniforms must match the std140 Draw block");

} // namespace Geni::ShaderInterface
