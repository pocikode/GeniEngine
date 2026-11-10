#version 450
// Bindings follow the engine's shader contract (include/render/ShaderInterface.h).

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec4 aJoints; // joint indices stored as floats
layout(location = 5) in vec4 aWeights;

layout(set = 0, binding = 0) uniform Frame
{
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 lightColor;
    vec4 lightPosition;
} uFrame;

layout(set = 2, binding = 0) uniform Draw
{
    mat4 model;
} uDraw;

layout(set = 2, binding = 1) uniform Bones
{
    mat4 bones[128];
} uBones;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vFragPos;

void main()
{
    ivec4 joints = ivec4(aJoints);
    float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;

    mat4 skin = mat4(1.0);
    if (totalWeight > 0.0)
    {
        skin = aWeights.x * uBones.bones[joints.x]
             + aWeights.y * uBones.bones[joints.y]
             + aWeights.z * uBones.bones[joints.z]
             + aWeights.w * uBones.bones[joints.w];
    }

    vec4 worldPos = uDraw.model * skin * vec4(aPos, 1.0);

    vUV = aUV;
    vNormal = mat3(uDraw.model) * mat3(skin) * aNormal;
    vFragPos = worldPos.xyz;
    gl_Position = uFrame.projection * uFrame.view * worldPos;
}
