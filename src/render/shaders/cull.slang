// GPU frustum culling (docs/design/0029). One thread per candidate instance: a sphere
// outside any frustum plane is dropped; a visible one claims the next slot of its draw
// group (an atomic add on the group's indirect instance count) and copies its model
// matrix there. Positions are camera-relative, like everything the renderer uploads.

struct CullParams
{
    float4 planes[6]; // xyz normal pointing inwards, w distance
    uint count;
    uint pad0;
    uint pad1;
    uint pad2;
};

// Matrices as four columns, so no layout rule can reorder them between buffers.
struct CullInstance
{
    float4 column0;
    float4 column1;
    float4 column2;
    float4 column3;
    float4 sphere; // xyz centre, w radius (0: never culled; < 0: not a GPU candidate)
    uint group;
    uint base;
    uint pad0;
    uint pad1;
};

struct VisibleInstance
{
    float4 column0;
    float4 column1;
    float4 column2;
    float4 column3;
};

[[vk::binding(0, 0)]] ConstantBuffer<CullParams> uCull;
[[vk::binding(1, 0)]] StructuredBuffer<CullInstance> uInstances;
[[vk::binding(2, 0)]] RWStructuredBuffer<VisibleInstance> uVisible;
// DrawIndexedIndirectArgs, five uints per group; [group * 5 + 1] is the instance count.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> uArgs;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    const uint index = id.x;
    if (index >= uCull.count)
    {
        return;
    }
    CullInstance instance = uInstances[index];
    if (instance.sphere.w < 0.0)
    {
        return; // drawn directly by the CPU path (skinned, non-indexed)
    }
    if (instance.sphere.w > 0.0)
    {
        for (int p = 0; p < 6; ++p)
        {
            if (dot(uCull.planes[p].xyz, instance.sphere.xyz) + uCull.planes[p].w < -instance.sphere.w)
            {
                return;
            }
        }
    }
    uint slot;
    InterlockedAdd(uArgs[instance.group * 5 + 1], 1, slot);
    VisibleInstance visible;
    visible.column0 = instance.column0;
    visible.column1 = instance.column1;
    visible.column2 = instance.column2;
    visible.column3 = instance.column3;
    uVisible[instance.base + slot] = visible;
}
