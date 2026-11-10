#version 450

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;

layout(set = 0, binding = 0) uniform Tint
{
    vec4 tint;
} uTint;

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = aColor * uTint.tint.rgb;
    gl_Position = vec4(aPos, 0.5, 1.0);
}
