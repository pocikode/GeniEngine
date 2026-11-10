// The bone palette (set 2, binding 0), bound per draw for skinned meshes.

struct Bones
{
    float4x4 bones[128]; // ShaderInterface::MaxBones
};

[[vk::binding(0, 2)]]
ConstantBuffer<Bones> uBones;

// Weighted sum of the four joint matrices (joint indices stored as floats, vertex
// attributes 4 and 5); identity for a vertex with no weights.
float4x4 SkinMatrix(float4 jointIndices, float4 w)
{
    int4 joints = int4(jointIndices);
    if (w.x + w.y + w.z + w.w <= 0.0)
    {
        return float4x4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
    }
    return w.x * uBones.bones[joints.x] + w.y * uBones.bones[joints.y] + w.z * uBones.bones[joints.z] +
           w.w * uBones.bones[joints.w];
}
