// Analytic sky: a horizon/zenith gradient with a sun disc, drawn as a full-screen triangle
// at the far plane. Writes linear HDR.

import geni.frame;

[shader("vertex")]
float4 vertexMain(uint vertexIndex : SV_VulkanVertexID) : SV_Position
{
    return float4(FullScreenTrianglePosition(vertexIndex), 1.0, 1.0);
}

[shader("fragment")]
float4 fragmentMain(float4 fragCoord : SV_Position) : SV_Target
{
    // Unproject this pixel to a world-space ray.
    float2 screenUV = fragCoord.xy * uFrame.screenParams.zw;
    float2 ndc = screenUV * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 farPoint = mul(uFrame.inverseViewProjection, float4(ndc, 1.0, 1.0));
    float3 direction = normalize(farPoint.xyz / farPoint.w - uFrame.cameraPosition.xyz);

    float height = clamp(direction.y, -1.0, 1.0);
    float3 color = lerp(uFrame.skyHorizon.rgb, uFrame.skyZenith.rgb, pow(clamp(height, 0.0, 1.0), 0.55));
    // Below the horizon fades into the fog colour, so distant geometry sits on it.
    color = lerp(uFrame.fogColor.rgb, color, smoothstep(-0.08, 0.02, height));

    float sunAmount = max(dot(direction, normalize(uFrame.sunDirection.xyz)), 0.0);
    float disc = pow(sunAmount, 2500.0);
    float glow = pow(sunAmount, 40.0) * 0.35;
    color += uFrame.sunColor.rgb * uFrame.sunDirection.w * (disc * 6.0 + glow);

    // A little dithering hides banding in the gradient before tonemapping.
    float dither = frac(sin(dot(fragCoord.xy, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
    color += dither / 255.0;

    return float4(max(color, float3(0.0)), 1.0);
}
