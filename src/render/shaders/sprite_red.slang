// TEMPORARY (debug/paravirtual-sprites): normal sprite transform, constant red output.
import geni.frame;

struct VertexInput
{
    [[vk::location(0)]] float4 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(uFrame.viewProjection, float4(input.position.xyz, 1.0));
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
