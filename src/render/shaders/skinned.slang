// Built-in skinned glTF shader (Renderer::GetSkinnedShader). Same surface as the
// default shader, with the bone palette applied in the vertex stage.
import geni.standard;

[shader("vertex")]
StandardVaryings vertexMain(SkinnedVertexInput input)
{
    return SkinnedVertex(input);
}

[shader("fragment")]
float4 fragmentMain(StandardVaryings input) : SV_Target
{
    return StandardFragment(input);
}
