#pragma once

#include "rhi/Types.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Geni
{

enum class ShaderBindingKind : uint8_t
{
    UniformBlock,
    SampledTexture,
};

struct ShaderBlockMember
{
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

// One descriptor a shader uses, as found in its SPIR-V.
struct ShaderBinding
{
    uint32_t set = 0;
    uint32_t binding = 0;
    ShaderBindingKind kind = ShaderBindingKind::UniformBlock;
    // Variable name: the instance name of a block (`uMaterial`), the sampler's name.
    std::string name;
    // Block type name (`Material`); empty for textures.
    std::string blockName;
    uint32_t size = 0; // block size in bytes
    std::vector<ShaderBlockMember> members;
    rhi::ShaderStage stages = rhi::ShaderStage::Vertex;
};

struct ShaderReflection
{
    std::vector<ShaderBinding> bindings;
    // Vertex input locations (vertex stage only).
    std::vector<uint32_t> inputLocations;

    const ShaderBinding *Find(uint32_t set, uint32_t binding) const;
};

// Reads the descriptor bindings and vertex inputs of one SPIR-V module.
bool ReflectSpirv(const uint32_t *code, size_t sizeBytes, rhi::ShaderStage stage, ShaderReflection &out,
                  std::string &error);

// Adds `from` (another stage) to `into`. A binding both stages use must agree on its kind
// and, for blocks, on its size.
bool MergeReflection(ShaderReflection &into, const ShaderReflection &from, std::string &error);

// Checks the bindings against ShaderInterface: sets 0 and 2 may only use what the renderer
// provides, set 1 may hold at most one uniform block, and no other set is allowed.
bool ValidateShaderInterface(const ShaderReflection &reflection, std::string &error);

} // namespace Geni
