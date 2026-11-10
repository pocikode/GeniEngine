#version 450
// Breaks the shader contract on purpose: set 3 does not exist (render/ShaderInterface.h).

layout(set = 3, binding = 0) uniform Extra
{
    vec4 color;
} uExtra;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = uExtra.color;
}
