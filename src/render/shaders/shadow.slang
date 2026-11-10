// Depth-only pass for shadow cascades. The cascade index rides on instance attribute 10,
// so the same vertex buffer layout and pipeline work for every cascade.

import geni.frame;

struct ShadowVertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(6)]] float4 instance0 : INSTANCE0;
    [[vk::location(7)]] float4 instance1 : INSTANCE1;
    [[vk::location(8)]] float4 instance2 : INSTANCE2;
    [[vk::location(9)]] float4 instance3 : INSTANCE3;
    [[vk::location(10)]] float cascade : CASCADE;
};

[shader("vertex")]
float4 vertexMain(ShadowVertexInput input) : SV_Position
{
    int cascade = clamp(int(input.cascade + 0.5), 0, 3);
    float4 world = mul(ModelMatrix(input.instance0, input.instance1, input.instance2, input.instance3), float4(input.position, 1.0));
    return mul(uFrame.shadowMatrices[cascade], world);
}

// Shadow passes are depth-only: there is no colour output.
[shader("fragment")]
void fragmentMain()
{
}
