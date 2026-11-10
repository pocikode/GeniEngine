#version 450
// Built-in glTF metallic-roughness shader. Direct light: one sun with cascaded shadows
// and the clustered point/spot lights in set 0. Ambient: a hemispheric sky approximation.

layout(set = 0, binding = 0) uniform Frame
{
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    vec4 sunDirection;  // xyz = towards the sun, w = intensity
    vec4 sunColor;
    vec4 skyZenith;
    vec4 skyHorizon;    // w = fog density
    vec4 fogColor;
    mat4 shadowMatrices[4];
    vec4 cascadeSplits;
    vec4 shadowParams;  // x = depth bias (texels), y = normal bias (m), z = cascade count, w = 1/size
    vec4 clusterGrid;
    vec4 clusterDepth;  // x = near, y = far, z = log(far/near), w = 1/z
    vec4 screenParams;  // w, h, 1/w, 1/h
} uFrame;

struct GpuLight
{
    vec4 positionRange;  // xyz = position, w = range
    vec4 colorIntensity; // rgb = colour, w = intensity
    vec4 directionOuter; // xyz = direction, w = cos(outer cone)
    vec4 coneParams;     // x = cos(inner cone), y = 1/(cos(inner)-cos(outer))
};
layout(set = 0, binding = 1) readonly buffer LightBuffer
{
    GpuLight lights[];
} uLights;

struct ClusterRange
{
    uint offset;
    uint count;
};
layout(set = 0, binding = 2) readonly buffer ClusterGrid
{
    ClusterRange clusters[];
} uClusters;
layout(set = 0, binding = 3) readonly buffer ClusterIndices
{
    uint indices[];
} uClusterIndices;

layout(set = 0, binding = 4) uniform sampler2D shadowMap0;
layout(set = 0, binding = 5) uniform sampler2D shadowMap1;
layout(set = 0, binding = 6) uniform sampler2D shadowMap2;
layout(set = 0, binding = 7) uniform sampler2D shadowMap3;

layout(set = 1, binding = 0) uniform Material
{
    vec4 uBaseColorFactor;
    vec4 uMetallicRoughness;    // x = metallic, y = roughness
    vec4 uEmissiveFactor;       // rgb
    vec4 uNormalScaleOcclusion; // x = normal scale, y = occlusion strength
} uMaterial;
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 1, binding = 3) uniform sampler2D normalTexture;
layout(set = 1, binding = 4) uniform sampler2D occlusionTexture;
layout(set = 1, binding = 5) uniform sampler2D emissiveTexture;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 FragColor;

const float PI = 3.14159265359;

// Matches the tonemap pass, which encodes with pow(1/2.2).
vec3 SrgbToLinear(vec3 c)
{
    return pow(c, vec3(2.2));
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

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Per-pixel tangent frame from screen-space derivatives, so the engine does not need
// tangent attributes (glTF meshes do not always carry them).
mat3 CotangentFrame(vec3 N, vec3 p, vec2 uv)
{
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

float SampleShadowDepth(int cascade, vec2 uv)
{
    if (cascade == 0)
    {
        return texture(shadowMap0, uv).r;
    }
    if (cascade == 1)
    {
        return texture(shadowMap1, uv).r;
    }
    if (cascade == 2)
    {
        return texture(shadowMap2, uv).r;
    }
    return texture(shadowMap3, uv).r;
}

float ShadowFactor(int cascade, vec3 worldPos, vec3 normal)
{
    mat4 matrix = uFrame.shadowMatrices[cascade];
    vec4 clip = matrix * vec4(worldPos + normal * uFrame.shadowParams.y, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        return 1.0;
    }
    // The cascade matrices are orthographic and already map into the RHI's 0..1 depth
    // range: row 0 scales x by 1/halfWidth, row 2 scales depth by 1/depthRange. The bias is
    // given in texels and converted to this cascade's depth units.
    float texel = uFrame.shadowParams.w;
    float texelWorld = 2.0 * texel / length(vec3(matrix[0][0], matrix[1][0], matrix[2][0]));
    float depthPerMetre = length(vec3(matrix[0][2], matrix[1][2], matrix[2][2]));
    float reference = ndc.z - uFrame.shadowParams.x * texelWorld * depthPerMetre;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float depth = SampleShadowDepth(cascade, uv + vec2(x, y) * texel);
            lit += reference <= depth ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

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

vec3 SkyAmbient(vec3 normal)
{
    float up = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(uFrame.skyHorizon.rgb, uFrame.skyZenith.rgb, up) * uFrame.skyZenith.w;
}

void main()
{
    // Colour textures are sRGB-encoded (glTF) but stored as UNORM; decode to linear here.
    // The factors are already linear.
    vec4 baseSample = texture(baseColorTexture, vUV);
    vec4 base = vec4(SrgbToLinear(baseSample.rgb), baseSample.a) * uMaterial.uBaseColorFactor;
    // Opaque geometry: alpha cut-outs are a later feature.
    if (base.a < 0.01)
    {
        discard;
    }
    vec3 albedo = base.rgb;

    vec4 metallicRoughness = texture(metallicRoughnessTexture, vUV);
    float metallic = clamp(uMaterial.uMetallicRoughness.x * metallicRoughness.b, 0.0, 1.0);
    float roughness = clamp(uMaterial.uMetallicRoughness.y * metallicRoughness.g, 0.04, 1.0);
    float occlusion = mix(1.0, texture(occlusionTexture, vUV).r, uMaterial.uNormalScaleOcclusion.y);

    vec3 N = normalize(vNormal);
    if (uMaterial.uNormalScaleOcclusion.x > 0.0)
    {
        vec3 tangentNormal = texture(normalTexture, vUV).xyz * 2.0 - 1.0;
        tangentNormal.xy *= uMaterial.uNormalScaleOcclusion.x;
        N = normalize(CotangentFrame(N, vWorldPos, vUV) * tangentNormal);
    }

    vec3 V = normalize(uFrame.cameraPosition.xyz - vWorldPos);
    float NdotV = max(dot(N, V), 1e-4);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 diffuseColor = albedo * (1.0 - metallic);

    vec3 color = vec3(0.0);

    // Sun.
    vec3 L = normalize(uFrame.sunDirection.xyz);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL > 0.0 && uFrame.sunDirection.w > 0.0)
    {
        vec3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);
        float NDF = DistributionGGX(NdotH, roughness);
        float G = GeometrySmith(NdotV, NdotL, roughness);
        vec3 F = FresnelSchlick(VdotH, F0);
        vec3 specular = (NDF * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);

        float shadow = 1.0;
        float viewDepth = -(uFrame.view * vec4(vWorldPos, 1.0)).z;
        int cascade = SelectCascade(viewDepth);
        if (cascade >= 0)
        {
            shadow = ShadowFactor(cascade, vWorldPos, N);
        }

        vec3 radiance = uFrame.sunColor.rgb * uFrame.sunDirection.w * shadow;
        color += (kd * diffuseColor / PI + specular) * radiance * NdotL;
    }

    // Clustered point and spot lights.
    ivec3 grid = ivec3(uFrame.clusterGrid.xyz + 0.5);
    vec2 screenUV = gl_FragCoord.xy * uFrame.screenParams.zw;
    ivec2 tile = clamp(ivec2(screenUV * vec2(grid.xy)), ivec2(0), grid.xy - 1);
    float viewDepth = -(uFrame.view * vec4(vWorldPos, 1.0)).z;
    float slice = log(max(viewDepth, uFrame.clusterDepth.x) / uFrame.clusterDepth.x) * uFrame.clusterDepth.w;
    int sliceIndex = clamp(int(slice * float(grid.z)), 0, grid.z - 1);
    int cluster = (sliceIndex * grid.y + tile.y) * grid.x + tile.x;
    ClusterRange range = uClusters.clusters[cluster];

    for (uint i = 0u; i < range.count; ++i)
    {
        GpuLight light = uLights.lights[uClusterIndices.indices[range.offset + i]];
        vec3 toLight = light.positionRange.xyz - vWorldPos;
        float distance = length(toLight);
        if (distance > light.positionRange.w)
        {
            continue;
        }
        vec3 lightDir = toLight / max(distance, 1e-4);
        float NdotLight = max(dot(N, lightDir), 0.0);
        if (NdotLight <= 0.0)
        {
            continue;
        }

        float attenuation = 1.0;
        if (light.directionOuter.w > -1.5)
        {
            // Spot light: smoothstep from the outer to the inner cone.
            float cosAngle = dot(-lightDir, normalize(light.directionOuter.xyz));
            attenuation = clamp(
                (cosAngle - light.directionOuter.w) * light.coneParams.y, 0.0, 1.0
            );
        }
        float ratio = distance / light.positionRange.w;
        float fade = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
        attenuation *= fade * fade / max(distance * distance, 0.01);

        vec3 H = normalize(V + lightDir);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);
        vec3 F = FresnelSchlick(VdotH, F0);
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 specular = (DistributionGGX(NdotH, roughness) * GeometrySmith(NdotV, NdotLight, roughness) * F) /
                        max(4.0 * NdotV * NdotLight, 1e-4);
        color += (kd * diffuseColor / PI + specular) * light.colorIntensity.rgb * light.colorIntensity.w *
                 attenuation * NdotLight;
    }

    color += diffuseColor * SkyAmbient(N) * occlusion;
    color += uMaterial.uEmissiveFactor.rgb * SrgbToLinear(texture(emissiveTexture, vUV).rgb);

    // Exponential distance fog toward the horizon colour.
    float cameraDistance = length(uFrame.cameraPosition.xyz - vWorldPos);
    float fog = 1.0 - exp(-cameraDistance * uFrame.skyHorizon.w);
    color = mix(color, uFrame.fogColor.rgb, clamp(fog, 0.0, 1.0));

    FragColor = vec4(color, 1.0);
}
