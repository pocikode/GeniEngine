// Built-in glTF metallic-roughness shader (Renderer::GetDefaultShader). The model matrix
// arrives as per-instance vertex attributes 6-9; lighting is geni/lighting.slang.

import geni.standard;

[shader("vertex")]
StandardVaryings vertexMain(StandardVertexInput input)
{
    return StandardVertex(input);
}

[shader("fragment")]
float4 fragmentMain(StandardVaryings input) : SV_Target
{
    return StandardFragment(input);
}
