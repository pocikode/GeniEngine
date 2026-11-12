// Breaks the shader contract on purpose: set 3 does not exist (render/ShaderInterface.h).

struct Extra
{
    float4 color;
};

[[vk::binding(0, 3)]] ConstantBuffer<Extra> uExtra;

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return uExtra.color;
}
