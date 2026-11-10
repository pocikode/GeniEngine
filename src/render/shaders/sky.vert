#version 450
// Full-screen triangle for the sky. No vertex buffer: positions come from gl_VertexIndex.

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
    const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 1.0, 1.0);
}