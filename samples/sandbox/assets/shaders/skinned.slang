// Skinned vertex stage for materials/skinned.mat, paired with the fragment stage of
// lit.slang. The bone palette is the draw-set block (include/render/ShaderInterface.h).

import geni.standard;

[shader("vertex")]
StandardVaryings vertexMain(SkinnedVertexInput input)
{
    return SkinnedVertex(input);
}
