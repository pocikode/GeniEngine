#include "render/ShaderReflection.h"
#include "render/ShaderInterface.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <cstdio>

namespace Geni
{

const ShaderBinding *ShaderReflection::Find(uint32_t set, uint32_t binding) const
{
    for (const auto &b : bindings)
    {
        if (b.set == set && b.binding == binding)
        {
            return &b;
        }
    }
    return nullptr;
}

bool ReflectSpirv(
    const uint32_t *code, size_t sizeBytes, rhi::ShaderStage stage, ShaderReflection &out, std::string &error
)
{
    out = ShaderReflection();
    SpvReflectShaderModule module;
    if (spvReflectCreateShaderModule(sizeBytes, code, &module) != SPV_REFLECT_RESULT_SUCCESS)
    {
        error = "not valid SPIR-V";
        return false;
    }

    uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding *> bindings(count);
    spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

    bool ok = true;
    for (const SpvReflectDescriptorBinding *b : bindings)
    {
        ShaderBinding binding;
        binding.set = b->set;
        binding.binding = b->binding;
        binding.stages = stage;
        binding.name = b->name ? b->name : "";

        if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        {
            binding.kind = ShaderBindingKind::UniformBlock;
            if (b->type_description && b->type_description->type_name)
            {
                binding.blockName = b->type_description->type_name;
            }
            binding.size = b->block.size;
            for (uint32_t m = 0; m < b->block.member_count; ++m)
            {
                const SpvReflectBlockVariable &member = b->block.members[m];
                binding.members.push_back({member.name ? member.name : "", member.offset, member.size});
            }
        }
        else if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            binding.kind = ShaderBindingKind::StorageBuffer;
            if (b->type_description && b->type_description->type_name)
            {
                binding.blockName = b->type_description->type_name;
            }
            binding.size = b->block.size;
        }
        else if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        {
            binding.kind = ShaderBindingKind::SampledTexture;
        }
        else
        {
            char text[160];
            std::snprintf(
                text, sizeof(text), "'%s' (set %u, binding %u) is neither a uniform block nor a sampler2D",
                binding.name.c_str(), binding.set, binding.binding
            );
            error = text;
            ok = false;
            break;
        }
        out.bindings.push_back(std::move(binding));
    }

    if (ok && stage == rhi::ShaderStage::Vertex)
    {
        spvReflectEnumerateInputVariables(&module, &count, nullptr);
        std::vector<SpvReflectInterfaceVariable *> inputs(count);
        spvReflectEnumerateInputVariables(&module, &count, inputs.data());
        for (const SpvReflectInterfaceVariable *input : inputs)
        {
            if (input->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
            {
                continue;
            }
            out.inputLocations.push_back(input->location);
        }
        std::sort(out.inputLocations.begin(), out.inputLocations.end());
    }

    spvReflectDestroyShaderModule(&module);
    return ok;
}

bool MergeReflection(ShaderReflection &into, const ShaderReflection &from, std::string &error)
{
    for (const auto &binding : from.bindings)
    {
        auto it = std::find_if(into.bindings.begin(), into.bindings.end(), [&](const ShaderBinding &b) {
            return b.set == binding.set && b.binding == binding.binding;
        });
        if (it == into.bindings.end())
        {
            into.bindings.push_back(binding);
            continue;
        }
        if (it->kind != binding.kind || it->size != binding.size)
        {
            char text[160];
            std::snprintf(
                text, sizeof(text), "set %u, binding %u is declared differently in the two stages", binding.set,
                binding.binding
            );
            error = text;
            return false;
        }
        it->stages = it->stages | binding.stages;
    }
    if (!from.inputLocations.empty())
    {
        into.inputLocations = from.inputLocations;
    }
    return true;
}

bool ValidateShaderInterface(const ShaderReflection &reflection, std::string &error)
{
    using namespace ShaderInterface;
    char text[200];
    int materialBlocks = 0;

    for (const auto &b : reflection.bindings)
    {
        if (b.set == FrameSet)
        {
            bool valid = false;
            if (b.binding == FrameBinding)
            {
                valid = b.kind == ShaderBindingKind::UniformBlock && b.size <= sizeof(FrameUniforms);
            }
            else if (b.binding == LightsBinding || b.binding == ClusterGridBinding || b.binding == ClusterLightsBinding)
            {
                valid = b.kind == ShaderBindingKind::StorageBuffer;
            }
            else if (b.binding >= ShadowBinding0 && b.binding < ShadowBinding0 + Cascades)
            {
                valid = b.kind == ShaderBindingKind::SampledTexture;
            }
            if (!valid)
            {
                std::snprintf(
                    text, sizeof(text),
                    "'%s' (set %u, binding %u) does not match what the renderer provides; see "
                    "render/ShaderInterface.h",
                    b.name.c_str(), b.set, b.binding
                );
                error = text;
                return false;
            }
        }
        else if (b.set == DrawSet)
        {
            if (b.binding != BonesBinding || b.kind != ShaderBindingKind::UniformBlock || b.size > sizeof(BoneUniforms))
            {
                std::snprintf(
                    text, sizeof(text),
                    "'%s' (set 2, binding %u) is not something the renderer provides; the draw set only has "
                    "the Bones block at binding %u",
                    b.name.c_str(), b.binding, BonesBinding
                );
                error = text;
                return false;
            }
        }
        else if (b.set == MaterialSet)
        {
            if (b.kind != ShaderBindingKind::UniformBlock && b.kind != ShaderBindingKind::SampledTexture)
            {
                std::snprintf(
                    text, sizeof(text), "'%s' (set 1, binding %u) must be a uniform block or a sampler2D",
                    b.name.c_str(), b.binding
                );
                error = text;
                return false;
            }
            if (b.kind == ShaderBindingKind::UniformBlock && ++materialBlocks > 1)
            {
                error = "set 1 (material) may hold only one uniform block";
                return false;
            }
        }
        else
        {
            std::snprintf(text, sizeof(text), "'%s' uses set %u; only sets 0, 1 and 2 exist", b.name.c_str(), b.set);
            error = text;
            return false;
        }
    }
    return true;
}

} // namespace Geni
