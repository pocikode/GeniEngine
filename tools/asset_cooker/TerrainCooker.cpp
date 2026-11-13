#include "TerrainCooker.h"

#include "MeshProcessing.h"

#include <stb_image.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace Geni::cook
{

namespace
{

bool Fail(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

glm::vec3 ReadColor(const nlohmann::json &colors, const char *key, const glm::vec3 &fallback)
{
    if (!colors.contains(key))
    {
        return fallback;
    }
    const auto &value = colors.at(key);
    return glm::vec3(value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>());
}

float Smoothstep(float edge0, float edge1, float x)
{
    const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

bool ParseTerrainSource(const nlohmann::json &document, TerrainSource &out, std::string *error)
{
    try
    {
        if (document.value("format", std::string()) != "geni.terrain" || document.value("version", 0) != 1)
        {
            return Fail(error, "not a version 1 geni.terrain file");
        }
        out.heightmap = document.at("heightmap").get<std::string>();
        out.size = document.value("size", out.size);
        out.height = document.value("height", out.height);
        out.chunks = document.value("chunks", out.chunks);
        out.chunkQuads = document.value("chunkQuads", out.chunkQuads);
        out.rockSlope = document.value("rockSlope", out.rockSlope);
        out.snowHeight = document.value("snowHeight", out.snowHeight);
        out.uvScale = document.value("uvScale", out.uvScale);
        const auto colors = document.value("colors", nlohmann::json::object());
        out.grass = ReadColor(colors, "grass", out.grass);
        out.rock = ReadColor(colors, "rock", out.rock);
        out.snow = ReadColor(colors, "snow", out.snow);
    }
    catch (const std::exception &e)
    {
        return Fail(error, std::string("malformed terrain: ") + e.what());
    }
    if (!(out.size > 0.0f) || !(out.height >= 0.0f) || out.chunks < 1 || out.chunks > 64 || out.chunkQuads < 2 ||
        out.chunkQuads > 128)
    {
        return Fail(error, "size must be positive, chunks 1..64 and chunkQuads 2..128");
    }
    return true;
}

bool BuildTerrainGrid(
    const std::vector<uint8_t> &image, const TerrainSource &source, HeightfieldData &out, std::string *error
)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<float> values;
    const auto *data = image.data();
    const int size = static_cast<int>(image.size());
    if (stbi_is_16_bit_from_memory(data, size))
    {
        stbi_us *pixels = stbi_load_16_from_memory(data, size, &width, &height, &channels, 1);
        if (!pixels)
        {
            return Fail(error, "cannot decode the heightmap");
        }
        values.assign(pixels, pixels + static_cast<size_t>(width) * height);
        for (float &v : values)
        {
            v /= 65535.0f;
        }
        stbi_image_free(pixels);
    }
    else
    {
        stbi_uc *pixels = stbi_load_from_memory(data, size, &width, &height, &channels, 1);
        if (!pixels)
        {
            return Fail(error, "cannot decode the heightmap");
        }
        values.assign(pixels, pixels + static_cast<size_t>(width) * height);
        for (float &v : values)
        {
            v /= 255.0f;
        }
        stbi_image_free(pixels);
    }
    if (width < 2 || height < 2)
    {
        return Fail(error, "the heightmap must be at least 2 x 2 pixels");
    }

    out.samples = static_cast<uint32_t>(source.chunks * source.chunkQuads + 1);
    out.cellSize = source.size / static_cast<float>(out.samples - 1);
    out.heights.resize(static_cast<size_t>(out.samples) * out.samples);
    auto pixel = [&](int x, int y) { return values[static_cast<size_t>(y) * width + x]; };
    for (uint32_t z = 0; z < out.samples; ++z)
    {
        // Image rows go down; the terrain's +Z is the image's down, so row 0 is z = 0.
        const float v = static_cast<float>(z) / static_cast<float>(out.samples - 1) * static_cast<float>(height - 1);
        const int y0 = std::min(static_cast<int>(v), height - 2);
        const float fy = v - static_cast<float>(y0);
        for (uint32_t x = 0; x < out.samples; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(out.samples - 1) * static_cast<float>(width - 1);
            const int x0 = std::min(static_cast<int>(u), width - 2);
            const float fx = u - static_cast<float>(x0);
            const float top = glm::mix(pixel(x0, y0), pixel(x0 + 1, y0), fx);
            const float bottom = glm::mix(pixel(x0, y0 + 1), pixel(x0 + 1, y0 + 1), fx);
            out.heights[static_cast<size_t>(z) * out.samples + x] = glm::mix(top, bottom, fy) * source.height;
        }
    }
    return true;
}

cooked::CookedMeshData BuildTerrainChunk(
    const HeightfieldData &grid, const TerrainSource &source, int chunkX, int chunkZ
)
{
    cooked::CookedMeshData mesh;
    // position (0), weights (1), uv (2), normal (3): 11 floats.
    mesh.layout.elements = {
        {VertexElement::PositionIndex, 3, VertexElementType::Float, 0},
        {VertexElement::ColorIndex, 3, VertexElementType::Float, 12},
        {VertexElement::UVIndex, 2, VertexElementType::Float, 24},
        {VertexElement::NormalIndex, 3, VertexElementType::Float, 32},
    };
    mesh.layout.stride = 44;
    mesh.topology = rhi::PrimitiveTopology::TriangleList;

    const int quads = source.chunkQuads;
    const uint32_t firstX = static_cast<uint32_t>(chunkX * quads);
    const uint32_t firstZ = static_cast<uint32_t>(chunkZ * quads);
    const float originX = static_cast<float>(firstX) * grid.cellSize;
    const float originZ = static_cast<float>(firstZ) * grid.cellSize;
    const float cosRock = std::cos(glm::radians(source.rockSlope));
    glm::vec3 low(1e30f);
    glm::vec3 high(-1e30f);
    for (int z = 0; z <= quads; ++z)
    {
        for (int x = 0; x <= quads; ++x)
        {
            const uint32_t gx = firstX + static_cast<uint32_t>(x);
            const uint32_t gz = firstZ + static_cast<uint32_t>(z);
            const float wx = static_cast<float>(gx) * grid.cellSize;
            const float wz = static_cast<float>(gz) * grid.cellSize;
            const float h = grid.At(gx, gz);
            // Normals over the whole grid, so both chunks of a border agree.
            const glm::vec3 normal = grid.NormalAt(wx, wz);
            const float rock = 1.0f - Smoothstep(cosRock - 0.08f, cosRock + 0.08f, normal.y);
            const float relative = source.height > 0.0f ? h / source.height : 0.0f;
            const float snow =
                Smoothstep(source.snowHeight - 0.04f, source.snowHeight + 0.04f, relative) * (1.0f - 0.7f * rock);
            const float grass = std::max(0.0f, 1.0f - rock - snow);
            const float total = std::max(grass + rock + snow, 1e-4f);
            const glm::vec3 position(wx - originX, h, wz - originZ);
            const float vertex[11] = {
                position.x,          position.y,          position.z, grass / total, rock / total, snow / total,
                wx * source.uvScale, wz * source.uvScale, normal.x,   normal.y,      normal.z
            };
            mesh.vertices.insert(mesh.vertices.end(), vertex, vertex + 11);
            low = glm::min(low, position);
            high = glm::max(high, position);
        }
    }
    const uint32_t row = static_cast<uint32_t>(quads + 1);
    for (uint32_t z = 0; z < static_cast<uint32_t>(quads); ++z)
    {
        for (uint32_t x = 0; x < static_cast<uint32_t>(quads); ++x)
        {
            const uint32_t a = z * row + x;
            const uint32_t b = a + 1;
            const uint32_t c = a + row;
            const uint32_t d = c + 1;
            // Counter-clockwise seen from above (+Y): a, c, b and b, c, d.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    mesh.bounds.min = low;
    mesh.bounds.max = high;
    mesh.bounds.center = (low + high) * 0.5f;
    mesh.bounds.radius = glm::length(high - low) * 0.5f;
    OptimizeAndBuildLods(mesh, true);
    return mesh;
}

} // namespace Geni::cook
