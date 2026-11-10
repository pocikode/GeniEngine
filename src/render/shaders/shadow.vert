#version 450
// Depth-only pass for shadow cascades. The cascade index rides on instance attribute 10,
// so the same vertex buffer layout and pipeline work for every cascade.

layout(location = 0) in vec3 aPos;
layout(location = 6) in vec4 aInstance0;
layout(location = 7) in vec4 aInstance1;
layout(location = 8) in vec4 aInstance2;
layout(location = 9) in vec4 aInstance3;
layout(location = 10) in float aCascade;

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

void main()
{
    mat4 model = mat4(aInstance0, aInstance1, aInstance2, aInstance3);
    int cascade = clamp(int(aCascade + 0.5), 0, 3);
    gl_Position = uFrame.shadowMatrices[cascade] * model * vec4(aPos, 1.0);
}
