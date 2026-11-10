#version 450
// HDR -> display: exposure, ACES filmic curve, sRGB encode into the UNORM backbuffer.

layout(set = 0, binding = 0) uniform Frame
{
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraPosition; // w = exposure
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

layout(set = 1, binding = 0) uniform sampler2D hdrColor;

layout(location = 0) out vec4 FragColor;

vec3 AcesFilmic(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec2 uv = gl_FragCoord.xy * uFrame.screenParams.zw;
    vec3 color = texture(hdrColor, uv).rgb * uFrame.cameraPosition.w;
    color = AcesFilmic(color);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}