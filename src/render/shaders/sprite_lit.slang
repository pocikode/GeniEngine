// Lit sprites (docs/design/0021): the sprite's texel lit like a flat, rough, non-metallic
// surface by the sun (with its shadows), the clustered lights and the sky ambient. The
// normal is the quad's own (from screen-space derivatives), facing the camera, so flat
// sprites and billboards need no normal in the vertex.

import geni.frame;
import geni.lighting;

[[vk::binding(0, 1)]] Sampler2D spriteTexture;

struct VertexInput
{
    // xyz: camera-relative position; w: 1 to decode sRGB in the shader (see sprite.slang).
    [[vk::location(0)]] float4 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD2;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float decode : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(uFrame.viewProjection, float4(input.position.xyz, 1.0));
    output.worldPosition = input.position.xyz;
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
    float4 base = texel * input.color;

    float3 normal = normalize(cross(ddx(input.worldPosition), ddy(input.worldPosition)));
    // Two-sided: the side the camera (at the origin) sees.
    if (dot(normal, -input.worldPosition) < 0.0)
    {
        normal = -normal;
    }
    Surface surface;
    surface.position = input.worldPosition;
    surface.normal = normal;
    surface.albedo = base.rgb;
    surface.metallic = 0.0;
    surface.roughness = 1.0;
    surface.occlusion = 1.0;
    return float4(ShadeSurface(surface, input.position.xy), base.a);
}
