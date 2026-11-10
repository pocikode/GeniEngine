#include "render/ShaderInterface.h"
#include "render/ShaderReflection.h"

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>

#include "bad_set_frag.spv.h"
#include "default_frag.spv.h"
#include "default_vert.spv.h"

using Geni::ShaderBindingKind;
using Geni::rhi::ShaderStage;

TEST_SUITE("shader reflection")
{
    TEST_CASE("the built-in shader's material block and texture are found by name")
    {
        Geni::ShaderReflection fragment;
        std::string error;
        REQUIRE(
            Geni::ReflectSpirv(
                geni_spirv_default_frag, sizeof(geni_spirv_default_frag), ShaderStage::Fragment, fragment, error
            )
        );

        const Geni::ShaderBinding *block = fragment.Find(Geni::ShaderInterface::MaterialSet, 0);
        REQUIRE(block != nullptr);
        CHECK(block->kind == ShaderBindingKind::UniformBlock);
        CHECK(block->blockName == "Material");
        CHECK(block->size == 64);
        REQUIRE(block->members.size() == 4);
        CHECK(block->members[0].name == "uBaseColorFactor");
        CHECK(block->members[0].offset == 0);
        CHECK(block->members[0].size == 16);
        CHECK(block->members[1].name == "uMetallicRoughness");
        CHECK(block->members[1].offset == 16);

        const Geni::ShaderBinding *texture = fragment.Find(Geni::ShaderInterface::MaterialSet, 1);
        REQUIRE(texture != nullptr);
        CHECK(texture->kind == ShaderBindingKind::SampledTexture);
        CHECK(texture->name == "baseColorTexture");
    }

    TEST_CASE("merging stages combines shared bindings and keeps the vertex inputs")
    {
        Geni::ShaderReflection merged;
        Geni::ShaderReflection fragment;
        std::string error;
        REQUIRE(
            Geni::ReflectSpirv(
                geni_spirv_default_vert, sizeof(geni_spirv_default_vert), ShaderStage::Vertex, merged, error
            )
        );
        REQUIRE(
            Geni::ReflectSpirv(
                geni_spirv_default_frag, sizeof(geni_spirv_default_frag), ShaderStage::Fragment, fragment, error
            )
        );
        REQUIRE(Geni::MergeReflection(merged, fragment, error));

        // position, UV, normal and the four instance-transform attributes; the color
        // attribute (1) is not read.
        CHECK(merged.inputLocations == std::vector<uint32_t>{0, 2, 3, 6, 7, 8, 9});

        const Geni::ShaderBinding *frame = merged.Find(Geni::ShaderInterface::FrameSet, 0);
        REQUIRE(frame != nullptr);
        CHECK(frame->size == sizeof(Geni::ShaderInterface::FrameUniforms));
        // Slang lays the block out from geni/frame.slang; it must match the C++ struct.
        using Frame = Geni::ShaderInterface::FrameUniforms;
        auto offsetOf = [&](const char *name) -> int64_t {
            for (const auto &member : frame->members)
            {
                if (member.name == name)
                {
                    return member.offset;
                }
            }
            return -1;
        };
        CHECK(offsetOf("view") == offsetof(Frame, view));
        CHECK(offsetOf("cameraPosition") == offsetof(Frame, cameraPosition));
        CHECK(offsetOf("shadowMatrices") == offsetof(Frame, shadowMatrices));
        CHECK(offsetOf("cascadeSplits") == offsetof(Frame, cascadeSplits));
        CHECK(offsetOf("screenParams") == offsetof(Frame, screenParams));
        CHECK(Geni::rhi::HasFlag(frame->stages, ShaderStage::Vertex));
        CHECK(Geni::rhi::HasFlag(frame->stages, ShaderStage::Fragment));

        CHECK(Geni::ValidateShaderInterface(merged, error));
    }

    TEST_CASE("a shader that uses a set the renderer does not have is rejected")
    {
        Geni::ShaderReflection reflection;
        std::string error;
        REQUIRE(
            Geni::ReflectSpirv(
                geni_spirv_bad_set_frag, sizeof(geni_spirv_bad_set_frag), ShaderStage::Fragment, reflection, error
            )
        );
        CHECK_FALSE(Geni::ValidateShaderInterface(reflection, error));
        CHECK(error.find("set 3") != std::string::npos);
    }

    TEST_CASE("bytes that are not SPIR-V are rejected")
    {
        const uint32_t garbage[4] = {1, 2, 3, 4};
        Geni::ShaderReflection reflection;
        std::string error;
        CHECK_FALSE(Geni::ReflectSpirv(garbage, sizeof(garbage), ShaderStage::Vertex, reflection, error));
    }
}
