#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

// The contract between the renderer and every shader. Shaders are GLSL 450, compiled
// offline to SPIR-V (cmake/GeniShaders.cmake), and use three descriptor sets:
//
//   set 0  per frame     binding 0: uniform Frame  (FrameUniforms)
//                        binding 1: readonly buffer Lights       (GpuLight[])
//                        binding 2: readonly buffer ClusterGrid  (ClusterRange[])
//                        binding 3: readonly buffer ClusterLights (uint[])
//                        bindings 4-7: the four cascade depth maps
//   set 1  per material  binding N: one uniform block of material parameters, any name,
//                                   whose member names match Material::SetParam names
//                        binding M: sampler2D textures, matched by name to
//                                   Material::SetParam(name, texture)
//   set 2  per draw      binding 0: uniform Bones (BoneUniforms, skinned meshes only)
//
// The per-instance model matrix is not a uniform: it arrives as vertex attributes 6-9
// (four vec4 columns), read once per instance. Every draw is instanced; a single object
// is an instance count of one.
//
// Vertex inputs use the locations in VertexElement (graphics/VertexLayout.h), which only
// covers mesh attributes 0-5; 6-9 are reserved for the instance transform. Clip space has
// Y up and depth 0..1; CameraComponent's OpenGL-style projection is converted by the
// renderer, so shaders just compute projection * view * model * position.
//
//   layout(set = 0, binding = 0) uniform Frame { ... } uFrame;
//   layout(set = 2, binding = 0) uniform Bones { mat4 bones[128]; } uBones;
//   layout(location = 6) in vec4 aInstance0; // .. 9
namespace Geni::ShaderInterface
{

constexpr uint32_t FrameSet = 0;
constexpr uint32_t MaterialSet = 1;
constexpr uint32_t DrawSet = 2;

constexpr uint32_t FrameBinding = 0;
constexpr uint32_t LightsBinding = 1;
constexpr uint32_t ClusterGridBinding = 2;
constexpr uint32_t ClusterLightsBinding = 3;
// One sampled depth map per cascade: bindings 4, 5, 6, 7.
constexpr uint32_t ShadowBinding0 = 4;
constexpr uint32_t BonesBinding = 0;

constexpr uint32_t MaxBones = 128;
constexpr uint32_t Cascades = 4;
constexpr uint32_t MaxLightsPerCluster = 256;

// Vertex attribute locations for the per-instance model matrix columns.
constexpr uint32_t InstanceMatrixLocation = 6;

// std140 layout; every member is 16-byte aligned.
struct FrameUniforms
{
    glm::mat4 view;                  // world -> view
    glm::mat4 projection;            // view -> clip (depth 0..1)
    glm::mat4 viewProjection;        // world -> clip
    glm::mat4 inverseViewProjection; // clip -> world, for the sky
    glm::vec4 cameraPosition;        // xyz; w = exposure
    glm::vec4 sunDirection;          // xyz = towards the sun; w = intensity
    glm::vec4 sunColor;              // rgb
    glm::vec4 skyZenith;             // rgb; w = ambient intensity
    glm::vec4 skyHorizon;            // rgb; w = fog density (1/m)
    glm::vec4 fogColor;              // rgb; w = shadow texel size
    glm::mat4 shadowMatrices[Cascades];
    glm::vec4 cascadeSplits; // x..w = far view-space depth of cascades 0..3
    glm::vec4 shadowParams;  // x = depth bias (texels), y = normal bias (m), z = cascade count, w = 1/size
    glm::vec4 clusterGrid;   // x,y,z = cluster counts, w = max lights per cluster
    glm::vec4 clusterDepth;  // x = near, y = far, z = log(far/near), w = inverse of z
    glm::vec4 screenParams;  // w, h, 1/w, 1/h
};

// std430; matches the structure declared in the shaders.
struct GpuLight
{
    glm::vec4 positionRange;  // xyz = world position; w = range
    glm::vec4 colorIntensity; // rgb = colour; w = intensity (already folded in)
    glm::vec4 directionOuter; // xyz = direction; w = cos(outer cone)
    glm::vec4 coneParams;     // x = cos(inner cone), y = 1/(cos(inner)-cos(outer))
};

struct ClusterRange
{
    uint32_t offset = 0;
    uint32_t count = 0;
};

struct BoneUniforms
{
    glm::mat4 bones[MaxBones];
};

static_assert(sizeof(FrameUniforms) == 688, "FrameUniforms must match the std140 Frame block");
static_assert(sizeof(GpuLight) == 64, "GpuLight must match the std430 light structure");
static_assert(sizeof(ClusterRange) == 8, "ClusterRange must match the std430 cluster grid");
static_assert(sizeof(BoneUniforms) == MaxBones * 64, "BoneUniforms must be a mat4 array");

} // namespace Geni::ShaderInterface
