#version 450
// Built-in lit shader (Renderer::GetDefaultShader). Embedded into the engine at build time.

layout(set = 0, binding = 0) uniform Frame
{
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 lightColor;
    vec4 lightPosition;
} uFrame;

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
    vec3 norm = normalize(vNormal);

    // diffuse
    vec3 lightDir = normalize(uFrame.lightPosition.xyz - vFragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uFrame.lightColor.rgb;

    // specular
    vec3 viewDir = normalize(uFrame.cameraPos.xyz - vFragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    float specularStrength = 0.5;
    vec3 specular = specularStrength * spec * uFrame.lightColor.rgb;

    // Ambient so unlit materials without a dynamic light still render visible.
    vec3 result = vec3(0.2) + diffuse + specular;

    vec4 texColor = texture(baseColorTexture, vUV) * uMaterial.uBaseColorFactor;
    FragColor = vec4(texColor.rgb * result, texColor.a);
}
