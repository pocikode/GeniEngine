// Direct and ambient lighting from set 0: the sun with cascaded shadows, the clustered
// point and spot lights, and a hemispheric sky ambient. The BRDF is the glTF
// metallic-roughness model (GGX, Smith-Schlick, Schlick Fresnel).

import geni.frame;

static const float PI = 3.14159265359;

// Must match ShaderInterface::GpuLight (std430).
struct GpuLight
{
    float4 positionRange;  // xyz = position, w = range
    float4 colorIntensity; // rgb = colour, w = intensity
    float4 directionOuter; // xyz = direction, w = cos(outer cone); w < -1.5 for a point light
    float4 coneParams;     // x = cos(inner cone), y = 1/(cos(inner)-cos(outer))
};

struct ClusterRange
{
    uint offset;
    uint count;
};

[[vk::binding(1, 0)]] StructuredBuffer<GpuLight> uLights;
[[vk::binding(2, 0)]] StructuredBuffer<ClusterRange> uClusters;
[[vk::binding(3, 0)]] StructuredBuffer<uint> uClusterIndices;

[[vk::binding(4, 0)]] Sampler2D shadowMap0;
[[vk::binding(5, 0)]] Sampler2D shadowMap1;
[[vk::binding(6, 0)]] Sampler2D shadowMap2;
[[vk::binding(7, 0)]] Sampler2D shadowMap3;

// What the BRDF needs to know about a surface point.
struct Surface
{
    float3 position; // world
    float3 normal;   // world, normalized
    float3 albedo;   // linear
    float metallic;
    float roughness;
    float occlusion;
};

// Matches the tonemap pass, which encodes with pow(1/2.2).
float3 SrgbToLinear(float3 c)
{
    return pow(c, float3(2.2));
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-6);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float SampleShadowDepth(int cascade, float2 uv)
{
    if (cascade == 0)
    {
        return shadowMap0.Sample(uv).r;
    }
    if (cascade == 1)
    {
        return shadowMap1.Sample(uv).r;
    }
    if (cascade == 2)
    {
        return shadowMap2.Sample(uv).r;
    }
    return shadowMap3.Sample(uv).r;
}

// Fraction of the sun that reaches worldPos in the given cascade, 3x3 PCF.
float ShadowFactor(int cascade, float3 worldPos, float3 normal)
{
    float4x4 m = uFrame.shadowMatrices[cascade];
    float4 clip = mul(m, float4(worldPos + normal * uFrame.shadowParams.y, 1.0));
    float3 ndc = clip.xyz / clip.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        return 1.0;
    }
    // The cascade matrices are orthographic and already map into the RHI's 0..1 depth
    // range: row 0 scales x by 1/halfWidth, row 2 scales depth by 1/depthRange. The bias is
    // given in texels and converted to this cascade's depth units.
    float texel = uFrame.shadowParams.w;
    float texelWorld = 2.0 * texel / length(m[0].xyz);
    float depthPerMetre = length(m[2].xyz);
    float reference = ndc.z - uFrame.shadowParams.x * texelWorld * depthPerMetre;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float depth = SampleShadowDepth(cascade, uv + float2(x, y) * texel);
            lit += reference <= depth ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

// The cascade that covers a view-space depth, or -1 beyond the last one.
int SelectCascade(float viewDepth)
{
    int count = int(uFrame.shadowParams.z + 0.5);
    for (int i = 0; i < 4; ++i)
    {
        if (i < count && viewDepth <= uFrame.cascadeSplits[i])
        {
            return i;
        }
    }
    return -1;
}

float3 SkyAmbient(float3 normal)
{
    float up = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    return lerp(uFrame.skyHorizon.rgb, uFrame.skyZenith.rgb, up) * uFrame.skyZenith.w;
}

// Diffuse + specular response to one light from direction L (normalized, towards the light).
float3 Brdf(Surface s, float3 V, float NdotV, float3 L, float NdotL)
{
    float3 F0 = lerp(float3(0.04), s.albedo, s.metallic);
    float3 diffuseColor = s.albedo * (1.0 - s.metallic);
    float3 H = normalize(V + L);
    float NdotH = max(dot(s.normal, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float3 F = FresnelSchlick(VdotH, F0);
    float3 kd = (float3(1.0) - F) * (1.0 - s.metallic);
    float3 specular =
        (DistributionGGX(NdotH, s.roughness) * GeometrySmith(NdotV, NdotL, s.roughness) * F) /
        max(4.0 * NdotV * NdotL, 1e-4);
    return kd * diffuseColor / PI + specular;
}

// Sun, clustered lights and sky ambient at a surface point. fragCoord is SV_Position.
float3 ShadeSurface(Surface s, float2 fragCoord)
{
    float3 V = normalize(uFrame.cameraPosition.xyz - s.position);
    float NdotV = max(dot(s.normal, V), 1e-4);
    float viewDepth = -mul(uFrame.view, float4(s.position, 1.0)).z;
    float3 color = float3(0.0);

    // Sun.
    float3 L = normalize(uFrame.sunDirection.xyz);
    float NdotL = max(dot(s.normal, L), 0.0);
    if (NdotL > 0.0 && uFrame.sunDirection.w > 0.0)
    {
        float shadow = 1.0;
        int cascade = SelectCascade(viewDepth);
        if (cascade >= 0)
        {
            shadow = ShadowFactor(cascade, s.position, s.normal);
        }
        float3 radiance = uFrame.sunColor.rgb * uFrame.sunDirection.w * shadow;
        color += Brdf(s, V, NdotV, L, NdotL) * radiance * NdotL;
    }

    // Clustered point and spot lights.
    int3 grid = int3(uFrame.clusterGrid.xyz + 0.5);
    float2 screenUV = fragCoord * uFrame.screenParams.zw;
    int2 tile = clamp(int2(screenUV * float2(grid.xy)), int2(0), grid.xy - 1);
    float slice = log(max(viewDepth, uFrame.clusterDepth.x) / uFrame.clusterDepth.x) * uFrame.clusterDepth.w;
    int sliceIndex = clamp(int(slice * float(grid.z)), 0, grid.z - 1);
    int cluster = (sliceIndex * grid.y + tile.y) * grid.x + tile.x;
    ClusterRange range = uClusters[cluster];

    for (uint i = 0u; i < range.count; ++i)
    {
        GpuLight light = uLights[uClusterIndices[range.offset + i]];
        float3 toLight = light.positionRange.xyz - s.position;
        float distance = length(toLight);
        if (distance > light.positionRange.w)
        {
            continue;
        }
        float3 lightDir = toLight / max(distance, 1e-4);
        float NdotLight = max(dot(s.normal, lightDir), 0.0);
        if (NdotLight <= 0.0)
        {
            continue;
        }

        float attenuation = 1.0;
        if (light.directionOuter.w > -1.5)
        {
            // Spot light: smoothstep from the outer to the inner cone.
            float cosAngle = dot(-lightDir, normalize(light.directionOuter.xyz));
            attenuation = clamp((cosAngle - light.directionOuter.w) * light.coneParams.y, 0.0, 1.0);
        }
        float ratio = distance / light.positionRange.w;
        float fade = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
        attenuation *= fade * fade / max(distance * distance, 0.01);

        color += Brdf(s, V, NdotV, lightDir, NdotLight) * light.colorIntensity.rgb * light.colorIntensity.w *
                 attenuation * NdotLight;
    }

    color += s.albedo * (1.0 - s.metallic) * SkyAmbient(s.normal) * s.occlusion;
    return color;
}

// Exponential distance fog toward the horizon colour.
float3 ApplyFog(float3 color, float3 worldPos)
{
    float cameraDistance = length(uFrame.cameraPosition.xyz - worldPos);
    float fog = 1.0 - exp(-cameraDistance * uFrame.skyHorizon.w);
    return lerp(color, uFrame.fogColor.rgb, clamp(fog, 0.0, 1.0));
}
