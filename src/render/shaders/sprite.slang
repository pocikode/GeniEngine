// 2D sprites (docs/design/0017): textured, tinted, alpha-blended quads drawn in the HDR
// scene pass. Vertices arrive camera-relative, like every other world-space position.

import geni.frame;

[[vk::binding(0, 1)]] Sampler2D spriteTexture;

struct VertexInput
{
    // xyz: camera-relative position; w: 1 when the texture holds sRGB-encoded values the
    // sampler does not decode (RGBA8 Unorm), 0 for sRGB formats and linear data.
    [[vk::location(0)]] float4 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    // Linear colour and alpha.
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float decode : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(uFrame.viewProjection, float4(input.position.xyz, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    output.decode = input.position.w;
    return output;
}

float3 SrgbToLinearExact(float3 c)
{
    float3 low = c / 12.92;
    float3 high = pow((max(c, float3(0.04045)) + 0.055) / 1.055, float3(2.4));
    return select(c <= float3(0.04045), low, high);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 texel = spriteTexture.Sample(input.uv);
    if (input.decode > 0.5)
    {
        texel.rgb = SrgbToLinearExact(texel.rgb);
    }
    // Straight alpha; the pipeline blends src * a + dst * (1 - a). The tonemap pass
    // multiplies by the exposure.
    return texel * input.color;
}
