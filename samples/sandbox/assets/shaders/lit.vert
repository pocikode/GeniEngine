#version 450
// Bindings follow the engine's shader contract (include/render/ShaderInterface.h).

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aNormal;

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

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vFragPos;

void main()
{
    vUV = aUV;
    vNormal = mat3(transpose(inverse(uDraw.model))) * aNormal;
    vFragPos = vec3(uDraw.model * vec4(aPos, 1.0));
    gl_Position = uFrame.projection * uFrame.view * vec4(vFragPos, 1.0);
}
