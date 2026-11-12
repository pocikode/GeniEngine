// The sample's lit shader, loaded through materials/checker.mat to show a game supplying
// its own shader. It uses the engine's standard surface (geni/standard.slang) unchanged;
// a game would customise the look here, between the module calls.

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
