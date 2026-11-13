// Tests for the offline cooker (tools/asset_cooker), built as their own executable
// because the engine does not link the cooker or its encoders.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "AtlasCooker.h"
#include "AudioCooker.h"
#include "Cooker.h"
#include "Ktx2Writer.h"
#include "MeshProcessing.h"
#include "TerrainCooker.h"
#include "TextureCooker.h"
#include "TiledImporter.h"
#include "assets/CookedFormats.h"
#include "assets/Ktx2.h"
#include "world/WorldFormat.h"

#include <chrono>
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
    TEST_CASE("scenes and prefabs saved by the editor are listed in the manifest as they are")
    {
        // A source tree of its own: one scene, one prefab, and a file that is neither.
        const auto source = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "scene_source";
        const auto output = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "scene_output";
        std::filesystem::remove_all(source);
        std::filesystem::remove_all(output);
        std::filesystem::create_directories(source / "scenes");
        std::filesystem::create_directories(source / "prefabs");
        const std::string node = R"({"name":"Root","parent":-1,"components":[]})";
        std::ofstream(source / "scenes" / "level.gscene")
            << R"({"format":"geni.scene","version":1,"mainCamera":-1,"nodes":[)" << node << "]}";
        std::ofstream(source / "prefabs" / "lamp.gprefab")
            << R"({"format":"geni.prefab","version":1,"nodes":[)" << node << "]}";

        Geni::cook::CookOptions options;
        options.sourceRoot = source;
        options.outputRoot = output;
        options.cookAll = true;
        std::string error;
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(options, &error), error);

        // Copied unchanged to the same relative path, which is also the manifest path.
        CHECK(std::filesystem::exists(output / "scenes" / "level.gscene"));
        CHECK(std::filesystem::exists(output / "prefabs" / "lamp.gprefab"));
        std::ifstream manifestFile(output / "cooked_manifest.json");
        const auto manifest = nlohmann::json::parse(manifestFile);
        std::map<std::string, nlohmann::json> bySource;
        for (const auto &asset : manifest["assets"])
        {
            bySource[asset["source"].get<std::string>()] = asset;
        }
        REQUIRE(bySource.count("scenes/level.gscene") == 1);
        REQUIRE(bySource.count("prefabs/lamp.gprefab") == 1);
        CHECK(bySource["scenes/level.gscene"]["type"] == "scene");
        CHECK(bySource["scenes/level.gscene"]["path"] == "scenes/level.gscene");
        CHECK(bySource["scenes/level.gscene"]["id"] == Geni::AssetID::FromName("scene:scenes/level.gscene").ToHex());
        CHECK(bySource["prefabs/lamp.gprefab"]["type"] == "prefab");
        CHECK(bySource["prefabs/lamp.gprefab"]["id"] == Geni::AssetID::FromName("prefab:prefabs/lamp.gprefab").ToHex());

        // A file that is not a scene fails the cook with its name, instead of being listed.
        std::ofstream(source / "scenes" / "broken.gscene") << R"({"format":"something else"})";
        CHECK_FALSE(Geni::cook::Cooker().Run(options, &error));
        CHECK(error.find("broken.gscene") != std::string::npos);
    }

    TEST_CASE("cooking again leaves unchanged outputs untouched")
    {
        // Hot reload compares modification times, so a re-cook must only touch the files
        // whose content changed.
        const auto output = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "stable";
        std::filesystem::remove_all(output);
        Geni::cook::CookOptions options;
        options.sourceRoot = GENI_COOKER_TEST_SOURCE;
        options.outputRoot = output;
        options.cookAll = true;
        options.textureQuality = Geni::cook::TextureQuality::Fast;
        std::string error;
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(options, &error), error);

        std::map<std::string, std::filesystem::file_time_type> times;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(output / "cooked"))
        {
            if (entry.is_regular_file())
            {
                times[entry.path().string()] = entry.last_write_time();
            }
        }
        REQUIRE(times.size() > 5);
        // Make every stored time old, so a rewrite would be visible whatever the clock's resolution.
        for (auto &entry : times)
        {
            entry.second -= std::chrono::hours(1);
            std::filesystem::last_write_time(entry.first, entry.second);
        }
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(options, &error), error);
        for (const auto &entry : times)
        {
            CHECK_MESSAGE(std::filesystem::last_write_time(entry.first) == entry.second, entry.first);
        }
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
                     {"baseColorTexture", "textures/" + name + ".png"},
                     {"normalTexture", "textures/" + name + ".png#normal"}
                 })
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

    TEST_CASE("dense meshes get a LOD chain; each LOD is smaller and no more accurate than the last")
    {
        const auto output = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "lod_output";
        std::filesystem::remove_all(output);
        Geni::cook::CookOptions options;
        options.sourceRoot = GENI_COOKER_TEST_SOURCE;
        options.outputRoot = output;
        options.inputs = {"models/lod_sphere.gltf", "models/hierarchy.gltf"};
        std::string error;
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(options, &error), error);

        std::ifstream manifestFile(output / "cooked_manifest.json");
        const auto manifest = nlohmann::json::parse(manifestFile);
        std::map<std::string, std::string> meshes;
        for (const auto &asset : manifest["assets"])
        {
            if (asset["type"] == "mesh")
            {
                meshes[asset["source"].get<std::string>()] = asset["path"].get<std::string>();
            }
        }
        REQUIRE(meshes.count("models/lod_sphere.gltf#mesh0:0") == 1);
        std::vector<uint8_t> bytes;
        REQUIRE(Geni::cooked::ReadFile(output / meshes["models/lod_sphere.gltf#mesh0:0"], bytes));
        Geni::cooked::CookedMeshData sphere;
        REQUIRE_MESSAGE(Geni::cooked::ReadMesh(bytes, sphere, &error), error);
        REQUIRE(sphere.lods.size() >= 3);
        CHECK(sphere.lods[0].firstIndex == 0);
        CHECK(sphere.lods[0].error == 0.0f);
        for (size_t i = 1; i < sphere.lods.size(); ++i)
        {
            CHECK(sphere.lods[i].indexCount < sphere.lods[i - 1].indexCount);
            CHECK(sphere.lods[i].error >= sphere.lods[i - 1].error);
            CHECK(sphere.lods[i].indexCount % 3 == 0);
        }
        CHECK(sphere.lods.back().error > 0.0f);

        // A small mesh keeps one LOD: there is nothing worth simplifying.
        for (const auto &[source, path] : meshes)
        {
            if (source.rfind("models/hierarchy.gltf", 0) == 0)
            {
                REQUIRE(Geni::cooked::ReadFile(output / path, bytes));
                Geni::cooked::CookedMeshData small;
                REQUIRE(Geni::cooked::ReadMesh(bytes, small, &error));
                CHECK(small.lods.size() == 1);
            }
        }
    }

    TEST_CASE("version 1 meshes still load, as one LOD")
    {
        Geni::cooked::CookedMeshData mesh;
        mesh.layout.stride = 3 * sizeof(float);
        mesh.layout.elements.push_back({Geni::VertexElement::PositionIndex, 3, Geni::VertexElementType::Float, 0});
        mesh.vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        mesh.indices = {0, 1, 2};
        std::vector<uint8_t> bytes;
        Geni::cooked::WriteMesh(mesh, bytes);
        // Rewrite it as version 1: same layout without the LOD table at the end.
        bytes[4] = 1;
        bytes.resize(bytes.size() - 4);
        Geni::cooked::CookedMeshData read;
        std::string error;
        REQUIRE_MESSAGE(Geni::cooked::ReadMesh(bytes, read, &error), error);
        CHECK(read.lods.empty());
        CHECK(read.indices.size() == 3);
    }

    TEST_CASE("a world is cooked into a cell table with HLOD scenes, prefabs expanded")
    {
        const auto output = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "world_output";
        std::filesystem::remove_all(output);
        Geni::cook::CookOptions options;
        options.sourceRoot = GENI_COOKER_TEST_SOURCE;
        options.outputRoot = output;
        options.cookAll = true;
        std::string error;
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(options, &error), error);

        std::ifstream manifestFile(output / "cooked_manifest.json");
        const auto manifest = nlohmann::json::parse(manifestFile);
        std::map<std::string, nlohmann::json> byID;
        nlohmann::json world;
        for (const auto &asset : manifest["assets"])
        {
            byID[asset["id"].get<std::string>()] = asset;
            if (asset["type"] == "world")
            {
                world = asset;
            }
        }
        REQUIRE(world.is_object());
        CHECK(world["source"] == "worlds/tiny/tiny.gworld");
        CHECK(world["id"] == Geni::AssetID::FromName("world:worlds/tiny/tiny.gworld").ToHex());

        std::vector<uint8_t> bytes;
        REQUIRE(Geni::cooked::ReadFile(output / world["path"].get<std::string>(), bytes));
        Geni::WorldDescription description;
        REQUIRE_MESSAGE(
            Geni::ParseCookedWorld(nlohmann::json::parse(bytes.begin(), bytes.end()), description, &error), error
        );
        CHECK(description.cellsX == 4);
        CHECK(description.cellsZ == 4);
        CHECK(description.cellSize == 32.0f);
        CHECK(description.origin.x == -64.0);
        REQUIRE(description.cells.size() == 16);
        CHECK(byID.count(description.persistent.ToHex()) == 1);

        const Geni::WorldCellInfo *cell = description.FindCell(2, 1);
        REQUIRE(cell != nullptr);
        // 4 nodes in the file, plus the prefab's two spheres.
        CHECK(cell->nodeCount == 6);
        REQUIRE(cell->prefabs.size() == 1);
        CHECK(cell->prefabs[0] == Geni::AssetID::FromName("prefab:worlds/tiny/prefabs/pair.gprefab"));
        CHECK(byID[cell->scene.ToHex()]["source"] == "worlds/tiny/cells/cell_2_1.gscene");
        // The pillar is 8 m high on a ground whose top is at 0.
        CHECK(cell->minY == doctest::Approx(-1.0f));
        CHECK(cell->maxY == doctest::Approx(8.0f));

        // The HLOD is a scene of merged meshes in the same material.
        REQUIRE(byID.count(cell->hlod.ToHex()) == 1);
        CHECK(cell->hlodBytes > 0);
        REQUIRE(Geni::cooked::ReadFile(output / byID[cell->hlod.ToHex()]["path"].get<std::string>(), bytes));
        const auto hlod = nlohmann::json::parse(bytes.begin(), bytes.end());
        REQUIRE(hlod["nodes"].size() == 1); // one material
        const auto &properties = hlod["nodes"][0]["components"][0]["properties"];
        CHECK(properties["material"] == Geni::AssetID::FromName("material:models/lod_sphere.gltf:@Stone").ToHex());
        const auto hlodMesh = byID[properties["mesh"].get<std::string>()];
        CHECK(hlodMesh["type"] == "mesh");
        REQUIRE(Geni::cooked::ReadFile(output / hlodMesh["path"].get<std::string>(), bytes));
        Geni::cooked::CookedMeshData merged;
        REQUIRE_MESSAGE(Geni::cooked::ReadMesh(bytes, merged, &error), error);
        // Four spheres (ground, pillar, two from the prefab), each 1,024 triangles at full
        // detail; the HLOD merges their coarsest LODs and simplifies further.
        CHECK(merged.indices.size() / 3 < 4 * 1024 / 4);
        CHECK(merged.bounds.max.y == doctest::Approx(8.0f).epsilon(0.05));
    }

    TEST_CASE("HLOD simplification never drops a mesh to nothing")
    {
        // Two far-apart tiny triangles: pruning would remove both at a large error.
        Geni::cooked::CookedMeshData mesh;
        mesh.layout.stride = 3 * sizeof(float);
        mesh.layout.elements.push_back({Geni::VertexElement::PositionIndex, 3, Geni::VertexElementType::Float, 0});
        mesh.vertices = {0, 0, 0, 0.01f, 0, 0, 0, 0.01f, 0, 100, 0, 0, 100.01f, 0, 0, 100, 0.01f, 0};
        mesh.indices = {0, 1, 2, 3, 4, 5};
        Geni::cook::SimplifyInPlace(mesh, 0.1f, 0.5f);
        CHECK(mesh.indices.size() >= 3);
        CHECK(mesh.vertices.size() / 3 <= 6);
    }

    TEST_CASE("sounds compress to IMA ADPCM at a quarter of the size and decode close to the source")
    {
        for (uint32_t channels : {1u, 2u})
        {
            Geni::cook::PcmSound tone;
            tone.sampleRate = 22050;
            tone.channels = channels;
            const uint32_t frames = 22050 + 333; // not a whole number of blocks
            for (uint32_t i = 0; i < frames; ++i)
            {
                for (uint32_t c = 0; c < channels; ++c)
                {
                    const double hz = c == 0 ? 440.0 : 660.0;
                    tone.samples.push_back(
                        static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265358979 * hz * i / 22050.0))
                    );
                }
            }
            const std::vector<uint8_t> encoded = Geni::cook::EncodeImaAdpcmWav(tone);
            CHECK(encoded.size() < tone.samples.size() * 2 / 3);
            CHECK(encoded.size() > tone.samples.size() / 2 - 1);

            Geni::cook::PcmSound decoded;
            std::string error;
            REQUIRE_MESSAGE(Geni::cook::DecodeImaAdpcmWav(encoded, decoded, &error), error);
            CHECK(decoded.channels == channels);
            CHECK(decoded.sampleRate == 22050);
            REQUIRE(decoded.Frames() == frames);
            double signal = 0.0;
            double noise = 0.0;
            for (size_t i = 0; i < tone.samples.size(); ++i)
            {
                const double a = tone.samples[i];
                const double d = decoded.samples[i] - a;
                signal += a * a;
                noise += d * d;
            }
            const double snr = 10.0 * std::log10(signal / std::max(noise, 1.0));
            CHECK_MESSAGE(snr > 25.0, "SNR " << snr << " dB with " << channels << " channels");
        }
    }

    TEST_CASE("only 16-bit PCM WAV is read; short sounds stay PCM on desktop")
    {
        // A minimal 8-bit WAV: rejected, so the cooker copies it unchanged.
        std::vector<uint8_t> wav = {'R', 'I', 'F', 'F', 40,  0,   0,   0,   'W',  'A',  'V', 'E', 'f',  'm',  't', ' ',
                                    16,  0,   0,   0,   1,   0,   1,   0,   0x22, 0x56, 0,   0,   0x22, 0x56, 0,   0,
                                    1,   0,   8,   0,   'd', 'a', 't', 'a', 4,    0,    0,   0,   128,  128,  128, 128};
        Geni::cook::PcmSound sound;
        std::string error;
        CHECK_FALSE(Geni::cook::ReadPcmWav(wav, sound, &error));
        CHECK(error.find("16-bit") != std::string::npos);

        Geni::cook::PcmSound shortSound;
        shortSound.sampleRate = 22050;
        shortSound.channels = 1;
        shortSound.samples.resize(22050); // 1 s
        Geni::cook::PcmSound longSound = shortSound;
        longSound.samples.resize(22050 * 3);
        using Geni::cook::AudioCompression;
        CHECK_FALSE(Geni::cook::ShouldCompress(shortSound, "desktop", AudioCompression::Auto));
        CHECK(Geni::cook::ShouldCompress(longSound, "desktop", AudioCompression::Auto));
        CHECK(Geni::cook::ShouldCompress(shortSound, "mobile", AudioCompression::Auto));
        CHECK_FALSE(Geni::cook::ShouldCompress(longSound, "mobile", AudioCompression::Pcm));
        CHECK(Geni::cook::ShouldCompress(shortSound, "desktop", AudioCompression::Adpcm));
    }

    TEST_CASE("atlas images are packed without overlap and their borders are extruded")
    {
        std::vector<Geni::cook::AtlasImage> images;
        const int sizes[][2] = {{48, 8}, {128, 16}, {16, 16}, {30, 70}, {5, 5}};
        for (size_t i = 0; i < std::size(sizes); ++i)
        {
            Geni::cook::AtlasImage image;
            image.name = "image" + std::to_string(i);
            image.width = sizes[i][0];
            image.height = sizes[i][1];
            image.rgba.assign(static_cast<size_t>(image.width) * image.height * 4, static_cast<uint8_t>(10 * (i + 1)));
            images.push_back(image);
        }
        int width = 0;
        int height = 0;
        REQUIRE(Geni::cook::PackAtlas(images, 1, 4096, width, height));
        CHECK((width & (width - 1)) == 0);
        CHECK((height & (height - 1)) == 0);
        for (size_t a = 0; a < images.size(); ++a)
        {
            const auto &i = images[a];
            CHECK(i.x >= 1);
            CHECK(i.y >= 1);
            CHECK(i.x + i.width + 1 <= width);
            CHECK(i.y + i.height + 1 <= height);
            for (size_t b = a + 1; b < images.size(); ++b)
            {
                const auto &j = images[b];
                // Padded rectangles do not overlap.
                const bool apart = i.x + i.width + 1 <= j.x - 1 || j.x + j.width + 1 <= i.x - 1 ||
                                   i.y + i.height + 1 <= j.y - 1 || j.y + j.height + 1 <= i.y - 1;
                CHECK(apart);
            }
        }
        const std::vector<uint8_t> pixels = Geni::cook::ComposeAtlas(images, 1, width, height);
        const auto &first = images[0];
        // The pixel left of the image (padding) repeats its edge.
        const size_t edge = (static_cast<size_t>(first.y) * width + first.x - 1) * 4;
        CHECK(pixels[edge] == 10);

        std::vector<Geni::cook::AtlasImage> tooBig(1);
        tooBig[0].width = 5000;
        tooBig[0].height = 10;
        CHECK_FALSE(Geni::cook::PackAtlas(tooBig, 1, 4096, width, height));
    }

    TEST_CASE("Tiled maps that the importer cannot represent are refused with a reason")
    {
        auto none = [](const std::string &, nlohmann::json &) { return false; };
        Geni::cook::TiledMap map;
        std::string error;
        CHECK_FALSE(
            Geni::cook::ImportTiledMap(
                nlohmann::json::parse(
                    R"({"orientation":"isometric","width":1,"height":1,"tilewidth":8,"tileheight":8,"layers":[],"tilesets":[]})"
                ),
                "m.tmj", none, map, &error
            )
        );
        CHECK(error.find("orthogonal") != std::string::npos);
        CHECK_FALSE(
            Geni::cook::ImportTiledMap(
                nlohmann::json::parse(
                    R"({"orientation":"orthogonal","infinite":true,"width":1,"height":1,"tilewidth":8,"tileheight":8,"layers":[],"tilesets":[]})"
                ),
                "m.tmj", none, map, &error
            )
        );
        CHECK(error.find("infinite") != std::string::npos);
        CHECK_FALSE(
            Geni::cook::ImportTiledMap(
                nlohmann::json::parse(
                    R"({"orientation":"orthogonal","width":1,"height":1,"tilewidth":8,"tileheight":8,"layers":[],"tilesets":[{"firstgid":1,"source":"missing.tsj"}]})"
                ),
                "maps/m.tmj", none, map, &error
            )
        );
        CHECK(error.find("maps/missing.tsj") != std::string::npos);
        CHECK(Geni::cook::FormatTileList({5, 1, 2, 3, 9, 2}) == "1-3,5,9");
    }

    TEST_CASE("scatter rules place plants by density, slope and height, the same way every time")
    {
        // 128 m square: flat at 0 m on the left half, a 45-degree slope on the right.
        Geni::HeightfieldData grid;
        grid.samples = 129;
        grid.cellSize = 1.0f;
        grid.heights.resize(129 * 129);
        for (uint32_t z = 0; z < 129; ++z)
        {
            for (uint32_t x = 0; x < 129; ++x)
            {
                grid.heights[z * 129 + x] = x < 64 ? 0.0f : static_cast<float>(x - 64);
            }
        }
        Geni::cook::TerrainSource source;
        source.height = 64.0f;
        Geni::cook::ScatterRule rule;
        rule.density = 0.01f; // one per 100 m^2
        rule.maxSlope = 20.0f;
        rule.scaleMin = 0.5f;
        rule.scaleMax = 2.0f;
        const auto instances = Geni::cook::ScatterFoliage(grid, source, rule);
        // About half the area is flat: 128 * 64 / 100 = 82 candidates.
        CHECK(instances.size() > 60);
        CHECK(instances.size() < 100);
        for (const auto &i : instances)
        {
            CHECK(i.position.x <= 64.5f);
            CHECK(i.position.y == doctest::Approx(0.0f));
            CHECK(i.scale >= 0.5f);
            CHECK(i.scale <= 2.0f);
        }
        const auto again = Geni::cook::ScatterFoliage(grid, source, rule);
        REQUIRE(again.size() == instances.size());
        CHECK(again.front().position == instances.front().position);

        rule.maxSlope = 60.0f;
        rule.minHeight = 0.5f; // above 32 m: only the upper part of the slope
        for (const auto &i : Geni::cook::ScatterFoliage(grid, source, rule))
        {
            CHECK(i.position.y >= 32.0f - 1e-3f);
        }

        std::vector<uint8_t> bytes;
        Geni::WriteFoliage(instances, bytes);
        std::vector<Geni::FoliageInstance> read;
        REQUIRE(Geni::ReadFoliage(bytes, read));
        CHECK(read.size() == instances.size());
        CHECK(read.back().yaw == instances.back().yaw);
    }

    TEST_CASE("a .mat cooks into a material with its parameters and textures")
    {
        const auto source = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "mat_source";
        const auto output = std::filesystem::path(GENI_COOKER_TEST_OUTPUT) / "mat_output";
        std::filesystem::remove_all(source);
        std::filesystem::remove_all(output);
        std::filesystem::create_directories(source / "materials");
        std::filesystem::create_directories(source / "textures");
        std::filesystem::copy_file(
            std::filesystem::path(GENI_COOKER_TEST_SOURCE) / "textures" / "checker.png",
            source / "textures" / "checker.png"
        );
        std::ofstream(source / "materials" / "red.mat") << R"({
            "shader": "builtin:default",
            "params": { "float4": { "uBaseColorFactor": [1, 0, 0, 1] }, "float2": { "uMetallicRoughness": [0, 0.4] },
                        "textures": [ { "name": "baseColorTexture", "path": "textures/checker.png" },
                                      { "name": "normalTexture", "path": "textures/checker.png" } ] } })";
        // Two stages in two files: left to Material::Load, not cooked.
        std::ofstream(source / "materials" / "split.mat")
            << R"({"shader": {"vertex": "a.slang", "fragment": "b.slang"}})";

        Geni::cook::CookOptions options;
        options.sourceRoot = source;
        options.outputRoot = output;
        options.cookAll = true;
        std::string error;
        REQUIRE_MESSAGE(Geni::cook::Cooker().Run(options, &error), error);
        std::ifstream manifestFile(output / "cooked_manifest.json");
        const auto manifest = nlohmann::json::parse(manifestFile);
        nlohmann::json red;
        int normals = 0;
        for (const auto &asset : manifest["assets"])
        {
            if (asset["source"] == "materials/red.mat")
            {
                red = asset;
            }
            normals += asset["source"] == "textures/checker.png#normal" ? 1 : 0;
            CHECK(asset["source"] != "materials/split.mat");
        }
        REQUIRE(red.is_object());
        CHECK(red["type"] == "material");
        CHECK(red["id"] == Geni::AssetID::FromName("material:materials/red.mat").ToHex());
        CHECK(normals == 1); // the normal map slot cooked the image as a normal map
        std::vector<uint8_t> bytes;
        REQUIRE(Geni::cooked::ReadFile(output / red["path"].get<std::string>(), bytes));
        Geni::cooked::CookedMaterialData material;
        REQUIRE(Geni::cooked::ReadMaterial(bytes, material));
        CHECK(material.shader == "builtin:default");
        CHECK(material.float4s["uBaseColorFactor"].x == doctest::Approx(1.0f));
        CHECK(material.float2s["uMetallicRoughness"].y == doctest::Approx(0.4f));
        CHECK(material.textures.size() == 2);
    }
}
