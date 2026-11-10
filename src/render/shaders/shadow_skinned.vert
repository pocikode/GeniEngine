#version 450
// Depth-only pass for skinned meshes. The bone palette is the per-draw dynamic uniform
// block from the shader contract (set 2, binding 0).

layout(location = 0) in vec3 aPos;
layout(location = 4) in vec4 aJoints;
layout(location = 5) in vec4 aWeights;
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

layout(set = 2, binding = 0) uniform Bones
{
    mat4 bones[128];
} uBones;

void main()
{
    ivec4 joints = ivec4(aJoints);
    float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
    mat4 skin = mat4(1.0);
    if (totalWeight > 0.0)
    {
        skin = aWeights.x * uBones.bones[joints.x] + aWeights.y * uBones.bones[joints.y] +
               aWeights.z * uBones.bones[joints.z] + aWeights.w * uBones.bones[joints.w];
    }
    mat4 model = mat4(aInstance0, aInstance1, aInstance2, aInstance3);
    int cascade = clamp(int(aCascade + 0.5), 0, 3);
    gl_Position = uFrame.shadowMatrices[cascade] * model * skin * vec4(aPos, 1.0);
}