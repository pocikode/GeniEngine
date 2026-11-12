// HDR -> display: exposure, ACES filmic curve, sRGB encode into the UNORM backbuffer.

import geni.frame;

[[vk::binding(0, 1)]] Sampler2D hdrColor;

// The HDR image is addressed by an interpolated coordinate, not by the pixel position:
// the pass may draw into a rectangle of the backbuffer (Renderer::SetSceneViewport).
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VulkanVertexID)
{
    VertexOutput output;
    float2 clip = FullScreenTrianglePosition(vertexIndex);
    output.position = float4(clip, 0.0, 1.0);
    // Clip-space Y points up; the image's first row is the top.
    output.uv = float2(0.5 * clip.x + 0.5, 0.5 - 0.5 * clip.y);
    return output;
}

float3 AcesFilmic(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float3 color = hdrColor.Sample(input.uv).rgb * uFrame.cameraPosition.w; // w = exposure
    color = AcesFilmic(color);
    color = pow(color, float3(1.0 / 2.2));
    return float4(color, 1.0);
}
