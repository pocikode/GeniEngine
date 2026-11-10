// The engine's standard surface: the glTF metallic-roughness material (set 1) and the
// vertex and fragment functions of the built-in shader. A shader that wants the standard
// look with its own entry points imports this module and calls them.

__exported import geni.frame;
__exported import geni.lighting;
__exported import geni.skinning;

// Material::SetParam names match these members and the texture names below.
struct Material
{
    float4 uBaseColorFactor;
    float4 uMetallicRoughness;    // x = metallic, y = roughness
    float4 uEmissiveFactor;       // rgb
    float4 uNormalScaleOcclusion; // x = normal scale, y = occlusion strength
};

[[vk::binding(0, 1)]] ConstantBuffer<Material> uMaterial;
[[vk::binding(1, 1)]] Sampler2D baseColorTexture;
[[vk::binding(2, 1)]] Sampler2D metallicRoughnessTexture;
[[vk::binding(3, 1)]] Sampler2D normalTexture;
[[vk::binding(4, 1)]] Sampler2D occlusionTexture;
[[vk::binding(5, 1)]] Sampler2D emissiveTexture;

// Mesh attributes at the locations in graphics/VertexLayout.h, then the instance
// transform at 6-9.
struct StandardVertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float3 normal : NORMAL;
    [[vk::location(6)]] float4 instance0 : INSTANCE0;
    [[vk::location(7)]] float4 instance1 : INSTANCE1;
    [[vk::location(8)]] float4 instance2 : INSTANCE2;
    [[vk::location(9)]] float4 instance3 : INSTANCE3;
};

struct SkinnedVertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float3 normal : NORMAL;
    [[vk::location(4)]] float4 joints : JOINTS;
    [[vk::location(5)]] float4 weights : WEIGHTS;
    [[vk::location(6)]] float4 instance0 : INSTANCE0;
    [[vk::location(7)]] float4 instance1 : INSTANCE1;
    [[vk::location(8)]] float4 instance2 : INSTANCE2;
    [[vk::location(9)]] float4 instance3 : INSTANCE3;
};

struct StandardVaryings
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 worldPosition : POSITION1;
};

StandardVaryings StandardVertex(StandardVertexInput input)
{
    float4x4 model = ModelMatrix(input.instance0, input.instance1, input.instance2, input.instance3);
    float4 world = mul(model, float4(input.position, 1.0));
    StandardVaryings output;
    output.uv = input.uv;
    output.normal = mul(NormalMatrix(model), input.normal);
    output.worldPosition = world.xyz;
    output.clipPosition = mul(uFrame.projection, mul(uFrame.view, world));
    return output;
}

StandardVaryings SkinnedVertex(SkinnedVertexInput input)
{
    float4x4 skin = SkinMatrix(input.joints, input.weights);
    float4x4 model = ModelMatrix(input.instance0, input.instance1, input.instance2, input.instance3);
    float4 world = mul(model, mul(skin, float4(input.position, 1.0)));
    StandardVaryings output;
    output.uv = input.uv;
    output.normal = mul(NormalMatrix(model), mul((float3x3)skin, input.normal));
    output.worldPosition = world.xyz;
    output.clipPosition = mul(uFrame.projection, mul(uFrame.view, world));
    return output;
}

// Per-pixel tangent frame from screen-space derivatives, so the engine does not need
// tangent attributes (glTF meshes do not always carry them). Rows are T, B, N.
float3x3 CotangentFrame(float3 N, float3 p, float2 uv)
{
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = rsqrt(max(dot(T, T), dot(B, B)));
    return float3x3(T * invmax, B * invmax, N);
}

float4 StandardFragment(StandardVaryings input)
{
    float2 uv = input.uv;

    // Colour textures are cooked as sRGB formats, so the sampler returns linear values
    // (and filters in linear light). The factors are already linear.
    float4 base = baseColorTexture.Sample(uv) * uMaterial.uBaseColorFactor;
    // Opaque geometry: alpha cut-outs are a later feature.
    if (base.a < 0.01)
    {
        discard;
    }

    float4 metallicRoughness = metallicRoughnessTexture.Sample(uv);
    Surface s;
    s.position = input.worldPosition;
    s.albedo = base.rgb;
    s.metallic = clamp(uMaterial.uMetallicRoughness.x * metallicRoughness.b, 0.0, 1.0);
    s.roughness = clamp(uMaterial.uMetallicRoughness.y * metallicRoughness.g, 0.04, 1.0);
    s.occlusion = lerp(1.0, occlusionTexture.Sample(uv).r, uMaterial.uNormalScaleOcclusion.y);

    s.normal = normalize(input.normal);
    if (uMaterial.uNormalScaleOcclusion.x > 0.0)
    {
        // Normal maps are cooked as BC5 on desktop, which stores only X and Y; rebuild Z.
        // The RGBA8 and ASTC variants hold the same unit vectors, so this works for all.
        float2 xy = normalTexture.Sample(uv).xy * 2.0 - 1.0;
        float3 tangentNormal = float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));
        tangentNormal.xy *= uMaterial.uNormalScaleOcclusion.x;
        s.normal = normalize(mul(tangentNormal, CotangentFrame(s.normal, input.worldPosition, uv)));
    }

    float3 color = ShadeSurface(s, input.clipPosition.xy);
    color += uMaterial.uEmissiveFactor.rgb * emissiveTexture.Sample(uv).rgb;
    color = ApplyFog(color, input.worldPosition);
    return float4(color, 1.0);
}
