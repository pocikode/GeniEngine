// Per-frame data (set 0, binding 0) and the per-instance model matrix (vertex attributes
// 6-9): the parts of the shader contract in include/render/ShaderInterface.h that every
// shader can use. Matrices are column-major in memory, like glm; mul(M, v) is M * v.

// Must match ShaderInterface::FrameUniforms (std140, 688 bytes).
struct Frame
{
    float4x4 view;                  // world -> view
    float4x4 projection;            // view -> clip (depth 0..1)
    float4x4 viewProjection;        // world -> clip
    float4x4 inverseViewProjection; // clip -> world, for the sky
    float4 cameraPosition;          // xyz; w = exposure
    float4 sunDirection;            // xyz = towards the sun; w = intensity
    float4 sunColor;                // rgb
    float4 skyZenith;               // rgb; w = ambient intensity
    float4 skyHorizon;              // rgb; w = fog density (1/m)
    float4 fogColor;                // rgb; w = tonemapper (0 ACES, 1 none)
    float4x4 shadowMatrices[4];
    float4 cascadeSplits;           // x..w = far view-space depth of cascades 0..3
    float4 shadowParams;            // x = depth bias (texels), y = normal bias (m), z = cascade count, w = 1/size
    float4 clusterGrid;             // x,y,z = cluster counts, w = max lights per cluster
    float4 clusterDepth;            // x = near, y = far, z = log(far/near), w = 1/z
    float4 screenParams;            // w, h, 1/w, 1/h
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> uFrame;

// The model matrix from its four columns, which arrive as per-instance vertex attributes
// 6-9. Slang offsets explicit locations in nested structs and in later entry point
// parameters, so every vertex shader declares the columns in its own flat input struct:
//
//     [[vk::location(6)]] float4 instance0 : INSTANCE0;  // .. 9
float4x4 ModelMatrix(float4 column0, float4 column1, float4 column2, float4 column3)
{
    // The float4x4 constructor takes rows.
    return transpose(float4x4(column0, column1, column2, column3));
}

// Transforms normals: the inverse transpose of the model's upper 3x3, i.e. its cofactor
// matrix over its determinant. Equal to the upper 3x3 of transpose(inverse(model)) for
// the affine matrices instances use.
float3x3 NormalMatrix(float4x4 model)
{
    float3 r0 = model[0].xyz;
    float3 r1 = model[1].xyz;
    float3 r2 = model[2].xyz;
    float3 c0 = cross(r1, r2);
    float invDet = 1.0 / dot(r0, c0);
    return float3x3(c0, cross(r2, r0), cross(r0, r1)) * invDet;
}

// Clip-space corner of a triangle that covers the screen, for a draw of three vertices
// without a vertex buffer.
float2 FullScreenTrianglePosition(uint vertexIndex)
{
    const float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    return positions[vertexIndex];
}
