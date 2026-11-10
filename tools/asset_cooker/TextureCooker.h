#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Geni::cook
{

// What a texture's texels mean. It decides the mip filter and the cooked formats:
//
// | Usage  | Mips                         | Desktop  | Mobile        | Fallback   |
// |--------|------------------------------|----------|---------------|------------|
// | Color  | averaged in linear light     | BC7 sRGB | ASTC 4x4 sRGB | RGBA8 sRGB |
// | Linear | averaged as stored           | BC7      | ASTC 4x4      | RGBA8      |
// | Normal | averaged, then renormalized  | BC5 (XY) | ASTC 4x4      | RGBA8      |
//
// Colour is glTF base colour and emissive. Linear is data: metallic-roughness,
// occlusion, masks. Normal-map shaders rebuild Z from X and Y, so BC5 needs no blue.
enum class TextureUsage
{
    Color,
    Linear,
    Normal
};

const char *TextureUsageName(TextureUsage usage);
bool TextureUsageFromName(const std::string &name, TextureUsage &out);

// astc-encoder search effort. Fast is for iteration; Medium is the default for builds.
enum class TextureQuality
{
    Fast,
    Medium,
    Thorough
};

bool TextureQualityFromName(const std::string &name, TextureQuality &out);

struct TextureCookSettings
{
    TextureUsage usage = TextureUsage::Color;
    TextureQuality quality = TextureQuality::Medium;
};

// Paths of the KTX2 variants, relative to the output root.
struct CookedTextureFiles
{
    std::string rgba8;
    std::string bc; // BC7, or BC5 for normal maps
    std::string astc;
};

// Decodes an image (PNG/JPEG/... via stb_image), builds the mip chain for its usage and
// writes RGBA8, BC and ASTC 4x4 KTX2 files. `outputStem` is a path relative to
// `outputRoot` without an extension, for example "cooked/textures/checker".
bool CookTexture(
    const std::vector<uint8_t> &fileBytes, const std::string &outputStem, const std::filesystem::path &outputRoot,
    const TextureCookSettings &settings, CookedTextureFiles &out, std::string *error = nullptr
);

// The mip chain CookTexture encodes, base level first, as RGBA8 (sRGB-encoded for
// Color). Exposed for tests.
std::vector<std::vector<uint8_t>> BuildMipChain(
    const std::vector<uint8_t> &rgba, int width, int height, TextureUsage usage,
    std::vector<std::pair<int, int>> &sizes
);

} // namespace Geni::cook
