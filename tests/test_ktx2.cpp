#include "assets/CookedFormats.h"
#include "assets/Ktx2.h"
#include <doctest/doctest.h>
#include <filesystem>
#include <string>

TEST_SUITE("ktx2")
{
    TEST_CASE("the cooker writes readable variants for colour and normal-map textures")
    {
        struct Variant
        {
            const char *name;
            Geni::rhi::Format format;
        };
        // checker.png is a colour texture on its own and the normal map of
        // models/textured_quad.gltf, so it is cooked both ways.
        const Variant variants[] = {
            {"checker_bc7.ktx2", Geni::rhi::Format::BC7Srgb},
            {"checker_astc.ktx2", Geni::rhi::Format::ASTC4x4Srgb},
            {"checker_rgba8.ktx2", Geni::rhi::Format::RGBA8Srgb},
            {"checker_normal_bc5.ktx2", Geni::rhi::Format::BC5Unorm},
            {"checker_normal_astc.ktx2", Geni::rhi::Format::ASTC4x4Unorm},
            {"checker_normal_rgba8.ktx2", Geni::rhi::Format::RGBA8Unorm},
        };

        for (const auto &variant : variants)
        {
            CAPTURE(variant.name);
            const auto path = std::filesystem::path(GENI_TEST_ASSETS_DIR) / "cooked" / "textures" / variant.name;
            std::vector<uint8_t> bytes;
            REQUIRE_MESSAGE(Geni::cooked::ReadFile(path, bytes), path.string());

            Geni::Ktx2Image image;
            std::string error;
            REQUIRE_MESSAGE(Geni::ReadKtx2(bytes, image, &error), error);
            CHECK(image.width == 16);
            CHECK(image.height == 16);
            CHECK(image.mipLevels == 5);
            CHECK(image.format == variant.format);
            REQUIRE(image.levels.size() == 5);
            CHECK_FALSE(image.levels.front().empty());
            CHECK_FALSE(image.levels.back().empty());
        }
    }

    TEST_CASE("a malformed KTX2 file is rejected")
    {
        Geni::Ktx2Image image;
        std::string error;
        const std::vector<uint8_t> garbage = {1, 2, 3, 4};
        CHECK_FALSE(Geni::ReadKtx2(garbage, image, &error));
        CHECK_FALSE(error.empty());

        std::vector<uint8_t> almost(80, 0);
        almost[0] = 0xAB;
        almost[1] = 0x4B;
        CHECK_FALSE(Geni::ReadKtx2(almost, image, &error));
    }
}
