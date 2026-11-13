// Terrain (docs/design/0022): grass, rock and snow colours blended by the per-vertex
// weights the cooker baked from slope and height, broken up by world-space value noise,
// lit by geni.lighting and fogged like the standard shader.

import geni.frame;
import geni.lighting;

// Material::SetParam names (linear colours).
struct TerrainMaterial
{
    float4 uGrass;
    float4 uRock;
    float4 uSnow;
};

[[vk::binding(0, 1)]] ConstantBuffer<TerrainMaterial> uMaterial;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 weights : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float3 normal : NORMAL;
    [[vk::location(6)]] float4 instance0 : INSTANCE0;
    [[vk::location(7)]] float4 instance1 : INSTANCE1;
    [[vk::location(8)]] float4 instance2 : INSTANCE2;
    [[vk::location(9)]] float4 instance3 : INSTANCE3;
};

struct Varyings
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 worldPosition : POSITION1;
    [[vk::location(3)]] float3 weights : COLOR0;
};

[shader("vertex")]
Varyings vertexMain(VertexInput input)
{
    float4x4 model = ModelMatrix(input.instance0, input.instance1, input.instance2, input.instance3);
    float4 world = mul(model, float4(input.position, 1.0));
    Varyings output;
    output.uv = input.uv;
    output.normal = mul(NormalMatrix(model), input.normal);
    output.worldPosition = world.xyz;
    output.weights = input.weights;
    output.clipPosition = mul(uFrame.projection, mul(uFrame.view, world));
    return output;
}

float Hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

// Smooth value noise in 0..1.
float Noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), u.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), u.x), u.y);
}

[shader("fragment")]
float4 fragmentMain(Varyings input) : SV_Target
{
    float3 weights = input.weights / max(input.weights.x + input.weights.y + input.weights.z, 1e-4);
    // The UVs are world XZ times the terrain's uvScale: two octaves of noise vary the
    // brightness a little so wide slopes are not flat colour.
    float variation = 0.85 + 0.2 * Noise(input.uv * 0.5) + 0.1 * Noise(input.uv * 4.0);
    float3 albedo =
        (uMaterial.uGrass.rgb * weights.x + uMaterial.uRock.rgb * weights.y + uMaterial.uSnow.rgb * weights.z) *
        variation;

    Surface s;
    s.position = input.worldPosition;
    s.normal = normalize(input.normal);
    s.albedo = albedo;
    s.metallic = 0.0;
    s.roughness = lerp(0.95, 0.6, weights.z); // snow is a little shinier
    s.occlusion = 1.0;
    float3 color = ShadeSurface(s, input.clipPosition.xy);
    return float4(ApplyFog(color, input.worldPosition), 1.0);
}
