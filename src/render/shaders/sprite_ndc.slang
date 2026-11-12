// TEMPORARY (debug/paravirtual-sprites): no matrices; camera-relative xy scaled into clip
// space at depth 0.5, constant green output.
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
    output.position = float4(input.position.xy * 0.05, 0.5, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
