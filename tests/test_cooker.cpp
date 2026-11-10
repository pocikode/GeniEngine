// Tests for the offline cooker (tools/asset_cooker), built as their own executable
// because the engine does not link the cooker or its encoders.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Cooker.h"
#include "Ktx2Writer.h"
#include "TextureCooker.h"
#include "assets/CookedFormats.h"
#include "assets/Ktx2.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

uint32_t U32(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 4);
    return value;
}

uint64_t U64(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint64_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 8);
    return value;
}

struct Layout
{
    uint32_t vkFormat = 0;
    uint32_t levelCount = 0;
    uint32_t dfdOffset = 0;
    uint32_t dfdLength = 0;
    uint32_t kvdOffset = 0;
    uint32_t kvdLength = 0;
    std::vector<uint64_t> offsets;
    std::vector<uint64_t> lengths;
};

Layout ReadLayout(const std::vector<uint8_t> &file)
{
    Layout layout;
    layout.vkFormat = U32(file, 12);
    layout.levelCount = U32(file, 40);
    layout.dfdOffset = U32(file, 48);
    layout.dfdLength = U32(file, 52);
    layout.kvdOffset = U32(file, 56);
    layout.kvdLength = U32(file, 60);
    for (uint32_t level = 0; level < layout.levelCount; ++level)
    {
        layout.offsets.push_back(U64(file, 80 + level * 24));
        layout.lengths.push_back(U64(file, 80 + level * 24 + 8));
    }
    return layout;
}

// Number of samples in the DFD's basic descriptor block.
uint32_t DfdSamples(const std::vector<uint8_t> &file, const Layout &layout)
{
    const uint16_t blockSize = static_cast<uint16_t>(U32(file, layout.dfdOffset + 8) >> 16);
    return (blockSize - 24u) / 16u;
}

uint8_t DfdTransfer(const std::vector<uint8_t> &file, const Layout &layout)
{
    return file[layout.dfdOffset + 12 + 2];
}

std::vector<std::vector<uint8_t>> Levels(uint32_t blockBytes, uint32_t width, uint32_t height, bool compressed)
{
    std::vector<std::vector<uint8_t>> levels;
    for (;;)
    {
        const uint32_t units = compressed ? ((width + 3) / 4) * ((height + 3) / 4) : width * height;
        levels.emplace_back(size_t(units) * blockBytes, static_cast<uint8_t>(levels.size() + 1));
        if (width == 1 && height == 1)
        {
            break;
        }
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    return levels;
}

} // namespace

TEST_SUITE("cooker")
{
    TEST_CASE("KTX2 files follow the specification's layout")
    {
        struct Case
        {
            Geni::cook::Ktx2Format format;
            uint32_t vkFormat;
            uint32_t blockBytes;
            bool compressed;
            uint32_t samples;
            uint8_t transfer; // 1 linear, 2 sRGB
        };
        const Case cases[] = {
            {Geni::cook::Ktx2Format::RGBA8, 37, 4, false, 4, 1},
            {Geni::cook::Ktx2Format::RGBA8Srgb, 43, 4, false, 4, 2},
            {Geni::cook::Ktx2Format::BC5, 141, 16, true, 2, 1},
            {Geni::cook::Ktx2Format::BC7, 145, 16, true, 1, 1},
            {Geni::cook::Ktx2Format::BC7Srgb, 146, 16, true, 1, 2},
            {Geni::cook::Ktx2Format::ASTC4x4, 157, 16, true, 1, 1},
            {Geni::cook::Ktx2Format::ASTC4x4Srgb, 158, 16, true, 1, 2},
        };
        for (const Case &c : cases)
        {
            CAPTURE(c.vkFormat);
            const auto levels = Levels(c.blockBytes, 16, 8, c.compressed);
            std::vector<uint8_t> file;
            Geni::cook::WriteKtx2(c.format, 16, 8, levels, file);
            const Layout layout = ReadLayout(file);

            CHECK(layout.vkFormat == c.vkFormat);
            REQUIRE(layout.levelCount == levels.size());
            CHECK(DfdSamples(file, layout) == c.samples);
            CHECK(DfdTransfer(file, layout) == c.transfer);
            CHECK(U32(file, layout.dfdOffset) == layout.dfdLength); // dfdTotalSize
            CHECK(layout.kvdLength > 0);
            CHECK(std::string(reinterpret_cast<const char *>(file.data() + layout.kvdOffset + 4)) == "KTXwriter");

            // Smallest level first, each aligned to lcm(block size, 4), base level last.
            const uint32_t alignment = c.blockBytes;
            for (uint32_t level = 0; level < layout.levelCount; ++level)
            {
                CHECK(layout.offsets[level] % alignment == 0);
                CHECK(layout.lengths[level] == levels[level].size());
                CHECK(file[layout.offsets[level]] == levels[level][0]);
                if (level + 1 < layout.levelCount)
                {
                    CHECK(layout.offsets[level + 1] < layout.offsets[level]);
                }
            }
            CHECK(layout.offsets.back() >= layout.kvdOffset + layout.kvdLength);
            CHECK(layout.offsets[0] + layout.lengths[0] == file.size());

            // The engine's reader accepts it and finds every level.
            Geni::Ktx2Image image;
            std::string error;
            CHECK_MESSAGE(Geni::ReadKtx2(file, image, &error), error);
            CHECK(image.levels.size() == levels.size());
            CHECK(image.levels.back() == levels.back());
        }
    }

    TEST_CASE("colour mips are averaged in linear light")
    {
        // A black and a white texel average to 50% linear grey, which is about 188 in
        // sRGB. Averaging the stored bytes would give 128, visibly too dark.
        const std::vector<uint8_t> rgba = {0, 0, 0, 255, 255, 255, 255, 255};
        std::vector<std::pair<int, int>> sizes;
        const auto color = Geni::cook::BuildMipChain(rgba, 2, 1, Geni::cook::TextureUsage::Color, sizes);
        REQUIRE(color.size() == 2);
        CHECK(color[1][0] >= 186);
        CHECK(color[1][0] <= 190);
        CHECK(color[1][3] == 255);

        const auto linear = Geni::cook::BuildMipChain(rgba, 2, 1, Geni::cook::TextureUsage::Linear, sizes);
        CHECK(linear[1][0] == 128);
    }

    TEST_CASE("normal-map mips hold unit vectors")
    {
        // Two normals 90 degrees apart: their plain average is shorter than one.
        const std::vector<uint8_t> rgba = {255, 128, 128, 255, 128, 128, 255, 255};
        std::vector<std::pair<int, int>> sizes;
        const auto levels = Geni::cook::BuildMipChain(rgba, 2, 1, Geni::cook::TextureUsage::Normal, sizes);
        REQUIRE(levels.size() == 2);
        const float x = levels[1][0] / 255.0f * 2.0f - 1.0f;
        const float y = levels[1][1] / 255.0f * 2.0f - 1.0f;
        const float z = levels[1][2] / 255.0f * 2.0f - 1.0f;
        CHECK(std::sqrt(x * x + y * y + z * z) == doctest::Approx(1.0f).epsilon(0.02));
    }

    TEST_CASE("a normal map is cooked as BC5 and ASTC, a colour texture as sRGB")
    {
        // Smallest valid PNG source is awkward to hand-write; cook the checker fixture.
        const auto source = std::filesystem::path(GENI_COOKER_TEST_SOURCE) / "textures" / "checker.png";
        std::ifstream file(source, std::ios::binary);
        REQUIRE(file);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto output = std::filesystem::path(GENI_COOKER_TEST_OUTPUT);

        auto cook = [&](Geni::cook::TextureUsage usage, const std::string &stem) {
            Geni::cook::TextureCookSettings settings;
            settings.usage = usage;
            settings.quality = Geni::cook::TextureQuality::Fast;
            Geni::cook::CookedTextureFiles files;
            std::string error;
            REQUIRE_MESSAGE(Geni::cook::CookTexture(bytes, stem, output, settings, files, &error), error);
            return files;
        };
        auto formatOf = [&](const std::string &relative) {
            std::ifstream in(output / relative, std::ios::binary);
            const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            REQUIRE(data.size() > 80);
            return U32(data, 12);
        };

        const auto normal = cook(Geni::cook::TextureUsage::Normal, "normal");
        CHECK(normal.bc.find("_bc5.ktx2") != std::string::npos);
        CHECK(formatOf(normal.bc) == 141u);   // BC5_UNORM
        CHECK(formatOf(normal.astc) == 157u); // ASTC_4x4_UNORM
        CHECK(formatOf(normal.rgba8) == 37u); // R8G8B8A8_UNORM

        const auto color = cook(Geni::cook::TextureUsage::Color, "color");
        CHECK(formatOf(color.bc) == 146u);   // BC7_SRGB
        CHECK(formatOf(color.astc) == 158u); // ASTC_4x4_SRGB
        CHECK(formatOf(color.rgba8) == 43u); // R8G8B8A8_SRGB
    }

    TEST_CASE("a full cook removes outputs it no longer writes")
    {
        const auto output = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "prune";
        std::filesystem::remove_all(output);
        const auto stale = output / "cooked" / "textures" / "checker_bc1.ktx2";
        const auto staleFolder = output / "cooked" / "gone";
        std::filesystem::create_directories(stale.parent_path());
        std::filesystem::create_directories(staleFolder);
        std::ofstream(stale) << "old";
        std::ofstream(staleFolder / "old.gmesh") << "old";
        const auto userFile = output / "notes.txt"; // outside cooked/: never touched
        std::ofstream(userFile) << "keep";

        Geni::cook::CookOptions options;
        options.sourceRoot = GENI_COOKER_TEST_SOURCE;
        options.outputRoot = output;
        options.cookAll = true;
        options.textureQuality = Geni::cook::TextureQuality::Fast;
        std::string error;
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(options, &error), error);

        CHECK_FALSE(std::filesystem::exists(stale));
        CHECK_FALSE(std::filesystem::exists(staleFolder));
        CHECK(std::filesystem::exists(userFile));
        CHECK(std::filesystem::exists(output / "cooked" / "textures" / "checker_bc7.ktx2"));
        CHECK(std::filesystem::exists(output / "cooked" / "models" / "hierarchy.gscene"));

        // A partial cook must not delete the other assets' files.
        Geni::cook::CookOptions partial = options;
        partial.cookAll = false;
        partial.inputs = {"textures/checker.png"};
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(partial, &error), error);
        CHECK(std::filesystem::exists(output / "cooked" / "models" / "hierarchy.gscene"));
    }
    TEST_CASE("each glTF's materials keep their own textures when many are cooked together")
    {
        // Regression: images were cached by their cgltf_image address across files. Once a
        // glTF was freed, the next one's images could reuse those addresses and its
        // materials then pointed at the previous file's textures. Several identical glTFs,
        // each with its own image, are the easiest way to get the addresses reused.
        const auto root = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "image_cache";
        const auto source = root / "source";
        const auto output = root / "output";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(source / "models");
        std::filesystem::create_directories(source / "textures");

        std::ifstream quadFile(std::filesystem::path(GENI_COOKER_TEST_SOURCE) / "models" / "textured_quad.gltf");
        REQUIRE(quadFile);
        const std::string quad((std::istreambuf_iterator<char>(quadFile)), std::istreambuf_iterator<char>());
        const std::string imageUri = "\"../textures/checker.png\"";
        REQUIRE(quad.find(imageUri) != std::string::npos);

        const std::vector<std::string> names = {"a", "b", "c", "d", "e", "f", "g", "h"};
        for (const auto &name : names)
        {
            std::filesystem::copy_file(
                std::filesystem::path(GENI_COOKER_TEST_SOURCE) / "textures" / "checker.png",
                source / "textures" / (name + ".png")
            );
            std::string gltf = quad;
            gltf.replace(gltf.find(imageUri), imageUri.size(), "\"../textures/" + name + ".png\"");
            std::ofstream(source / "models" / (name + ".gltf")) << gltf;
        }

        Geni::cook::CookOptions options;
        options.sourceRoot = source;
        options.outputRoot = output;
        options.cookAll = true;
        options.textureQuality = Geni::cook::TextureQuality::Fast;
        std::string error;
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(options, &error), error);

        std::ifstream manifestFile(output / "cooked_manifest.json");
        REQUIRE(manifestFile);
        const auto manifest = nlohmann::json::parse(manifestFile);
        std::map<std::string, std::string> sourceByID; // texture ID -> source path
        std::map<std::string, std::string> materialPathBySource;
        for (const auto &entry : manifest.at("assets"))
        {
            const std::string type = entry.at("type").get<std::string>();
            if (type == "texture")
            {
                sourceByID[entry.at("id").get<std::string>()] = entry.at("source").get<std::string>();
            }
            else if (type == "material")
            {
                materialPathBySource[entry.at("source").get<std::string>()] = entry.at("path").get<std::string>();
            }
        }

        for (const auto &name : names)
        {
            const auto material = materialPathBySource.find("models/" + name + ".gltf#material0");
            REQUIRE_MESSAGE(material != materialPathBySource.end(), name);
            std::vector<uint8_t> bytes;
            REQUIRE(Geni::cooked::ReadFile(output / material->second, bytes));
            Geni::cooked::CookedMaterialData data;
            REQUIRE_MESSAGE(Geni::cooked::ReadMaterial(bytes, data, &error), error);
            for (const auto &[slot, expected] : std::map<std::string, std::string>{
                     {"baseColorTexture", "textures/" + name + ".png"}, {"normalTexture", "textures/" + name + ".png#normal"}})
            {
                const auto texture = data.textures.find(slot);
                REQUIRE_MESSAGE(texture != data.textures.end(), name << " " << slot);
                CHECK_MESSAGE(
                    sourceByID[texture->second.ToHex()] == expected,
                    name << ".gltf " << slot << " uses " << sourceByID[texture->second.ToHex()]
                );
            }
        }
    }
}
