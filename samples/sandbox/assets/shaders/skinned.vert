#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec4 aJoints; // joint indices stored as floats
layout(location = 5) in vec4 aWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uBones[128];

out vec2 vUV;
out vec3 vNormal;
out vec3 vFragPos;

void main()
{
    ivec4 joints = ivec4(aJoints);
    float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;

    mat4 skin = mat4(1.0);
    if (totalWeight > 0.0)
    {
        skin = aWeights.x * uBones[joints.x]
             + aWeights.y * uBones[joints.y]
             + aWeights.z * uBones[joints.z]
             + aWeights.w * uBones[joints.w];
    }

    vec4 worldPos = uModel * skin * vec4(aPos, 1.0);

    vUV = aUV;
    vNormal = mat3(uModel) * mat3(skin) * aNormal;
    vFragPos = worldPos.xyz;
    gl_Position = uProjection * uView * worldPos;
}
