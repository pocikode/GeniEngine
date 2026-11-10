// HDR -> display: exposure, ACES filmic curve, sRGB encode into the UNORM backbuffer.

import geni.frame;

[[vk::binding(0, 1)]] Sampler2D hdrColor;

[shader("vertex")]
float4 vertexMain(uint vertexIndex : SV_VulkanVertexID) : SV_Position
{
    return float4(FullScreenTrianglePosition(vertexIndex), 0.0, 1.0);
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
float4 fragmentMain(float4 fragCoord : SV_Position) : SV_Target
{
    float2 uv = fragCoord.xy * uFrame.screenParams.zw;
    float3 color = hdrColor.Sample(uv).rgb * uFrame.cameraPosition.w; // w = exposure
    color = AcesFilmic(color);
    color = pow(color, float3(1.0 / 2.2));
    return float4(color, 1.0);
}
