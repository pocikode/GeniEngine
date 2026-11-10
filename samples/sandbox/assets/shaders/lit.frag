#version 330 core

struct Light
{
    vec3 color;
    vec3 position;
};

in vec2 vUV;
in vec3 vNormal;
in vec3 vFragPos;

uniform sampler2D baseColorTexture;
uniform vec4 uBaseColorFactor;
uniform Light uLight;
uniform vec3 uCameraPos;

out vec4 FragColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLight.position - vFragPos);
    vec3 viewDir = normalize(uCameraPos - vFragPos);

    float diffuse = max(dot(normal, lightDir), 0.0);
    float specular = 0.3 * pow(max(dot(viewDir, reflect(-lightDir, normal)), 0.0), 32.0);

    vec3 lighting = vec3(0.25) + (diffuse + specular) * uLight.color;
    vec4 base = texture(baseColorTexture, vUV) * uBaseColorFactor;
    FragColor = vec4(base.rgb * lighting, base.a);
}
