// Depth-only pass for skinned meshes; pairs with the fragment stage of shadow.slang. The
// bone palette is the per-draw block from the shader contract (set 2, binding 0).

import geni.frame;
import geni.skinning;

struct SkinnedShadowVertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(4)]] float4 joints : JOINTS;
    [[vk::location(5)]] float4 weights : WEIGHTS;
    [[vk::location(6)]] float4 instance0 : INSTANCE0;
    [[vk::location(7)]] float4 instance1 : INSTANCE1;
    [[vk::location(8)]] float4 instance2 : INSTANCE2;
    [[vk::location(9)]] float4 instance3 : INSTANCE3;
    [[vk::location(10)]] float cascade : CASCADE;
};

[shader("vertex")]
float4 vertexMain(SkinnedShadowVertexInput input) : SV_Position
{
    int cascade = clamp(int(input.cascade + 0.5), 0, 3);
    float4x4 model = ModelMatrix(input.instance0, input.instance1, input.instance2, input.instance3);
    float4 world = mul(model, mul(SkinMatrix(input.joints, input.weights), float4(input.position, 1.0)));
    return mul(uFrame.shadowMatrices[cascade], world);
}
