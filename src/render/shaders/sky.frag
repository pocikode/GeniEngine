#version 450
// Analytic sky: a horizon/zenith gradient with a sun disc. Writes linear HDR.

layout(set = 0, binding = 0) uniform Frame
{
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 skyZenith;
    vec4 skyHorizon;
    vec4 fogColor;
    mat4 shadowMatrices[4];
    vec4 cascadeSplits;
    vec4 shadowParams;
    vec4 clusterGrid;
    vec4 clusterDepth;
    vec4 screenParams;
} uFrame;

layout(location = 0) out vec4 FragColor;

void main()
{
    // Unproject this pixel to a world-space ray.
    vec2 screenUV = gl_FragCoord.xy * uFrame.screenParams.zw;
    vec2 ndc = screenUV * 2.0 - 1.0;
    ndc.y = -ndc.y;
    vec4 farPoint = uFrame.inverseViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / farPoint.w - uFrame.cameraPosition.xyz);

    float height = clamp(direction.y, -1.0, 1.0);
    vec3 color = mix(uFrame.skyHorizon.rgb, uFrame.skyZenith.rgb, pow(clamp(height, 0.0, 1.0), 0.55));
    // Below the horizon fades into the fog colour, so distant geometry sits on it.
    color = mix(uFrame.fogColor.rgb, color, smoothstep(-0.08, 0.02, height));

    float sunAmount = max(dot(direction, normalize(uFrame.sunDirection.xyz)), 0.0);
    float disc = pow(sunAmount, 2500.0);
    float glow = pow(sunAmount, 40.0) * 0.35;
    color += uFrame.sunColor.rgb * uFrame.sunDirection.w * (disc * 6.0 + glow);

    // A little dithering hides banding in the gradient before tonemapping.
    float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    color += dither / 255.0;

    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}