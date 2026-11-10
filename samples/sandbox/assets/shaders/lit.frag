#version 450
// Bindings follow the engine's shader contract (include/render/ShaderInterface.h).

layout(set = 0, binding = 0) uniform Frame
{
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 lightColor;
    vec4 lightPosition;
} uFrame;

// Material parameters: member and sampler names are the names given to Material::SetParam.
layout(set = 1, binding = 0) uniform Material
{
    vec4 uBaseColorFactor;
} uMaterial;
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vFragPos;

layout(location = 0) out vec4 FragColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uFrame.lightPosition.xyz - vFragPos);
    vec3 viewDir = normalize(uFrame.cameraPos.xyz - vFragPos);

    float diffuse = max(dot(normal, lightDir), 0.0);
    float specular = 0.3 * pow(max(dot(viewDir, reflect(-lightDir, normal)), 0.0), 32.0);

    vec3 lighting = vec3(0.25) + (diffuse + specular) * uFrame.lightColor.rgb;
    vec4 base = texture(baseColorTexture, vUV) * uMaterial.uBaseColorFactor;
    FragColor = vec4(base.rgb * lighting, base.a);
}
