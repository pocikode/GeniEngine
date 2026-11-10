#version 450
// Sample copy of the engine's built-in PBR vertex shader, kept as an asset so the .mat
// file demonstrates loading a shader. Keep it in sync with src/render/shaders/default.vert.
// The model matrix arrives as per-instance vertex attributes 6-9, one mat4 per instance.

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aNormal;
layout(location = 6) in vec4 aInstance0;
layout(location = 7) in vec4 aInstance1;
layout(location = 8) in vec4 aInstance2;
layout(location = 9) in vec4 aInstance3;

layout(set = 0, binding = 0) uniform Frame
{
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 skyZenith;
    vec4 skyHorizon;
    vec4 fogColor;
    mat4 shadowMatrices[4];
    vec4 cascadeSplits;
    vec4 shadowParams;
    vec4 clusterGrid;
    vec4 clusterDepth;
    vec4 screenParams;
} uFrame;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;

void main()
{
    mat4 model = mat4(aInstance0, aInstance1, aInstance2, aInstance3);
    vec4 world = model * vec4(aPos, 1.0);
    vUV = aUV;
    vNormal = mat3(transpose(inverse(model))) * aNormal;
    vWorldPos = world.xyz;
    gl_Position = uFrame.projection * uFrame.view * world;
}
