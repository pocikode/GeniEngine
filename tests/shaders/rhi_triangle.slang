// Coloured triangle for the RHI tests: two vertex attributes and one uniform block.

struct Tint
{
    float4 tint;
};

[[vk::binding(0, 0)]] ConstantBuffer<Tint> uTint;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;
};

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
};

[shader("vertex")]
Varyings vertexMain(VertexInput input)
{
    Varyings output;
    output.color = input.color * uTint.tint.rgb;
    output.position = float4(input.position, 0.5, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varyings input) : SV_Target
{
    return float4(input.color, 1.0);
}
