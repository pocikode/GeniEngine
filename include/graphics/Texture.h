#pragma once

#include "assets/Asset.h"
#include "rhi/Handle.h"
#include "rhi/Types.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Geni
{

struct Ktx2Image;

// A texture with a full mip chain, sampled with the renderer's default (trilinear,
// repeating) sampler. Uncompressed images are widened to RGBA8; cooked textures arrive
// as KTX2 with BC1, BC7 or ASTC blocks and are uploaded as-is.
class Texture : public Asset
{
  public:
    // `data` has numChannels (1-4) bytes per pixel, top row first.
    Texture(int width, int height, int numChannels, const unsigned char *data);
    // Cooked KTX2 image, already mipmapped and compressed.
    explicit Texture(const Ktx2Image &image);
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;
    ~Texture();

    rhi::TextureHandle GetHandle() const
    {
        return m_handle;
    }
    int GetWidth() const
    {
        return m_width;
    }
    int GetHeight() const
    {
        return m_height;
    }
    int GetMipLevels() const
    {
        return m_mipLevels;
    }
    rhi::Format GetFormat() const
    {
        return m_format;
    }

    static std::shared_ptr<Texture> Load(const std::string &path);

  private:
    rhi::TextureHandle m_handle;
    int m_width = 0;
    int m_height = 0;
    int m_mipLevels = 1;
    rhi::Format m_format = rhi::Format::RGBA8Unorm;
};

class TextureManager
{
  public:
    std::shared_ptr<Texture> GetOrLoadTexture(const std::string &path);
    // 1x1 opaque white texture. Useful as a fallback when a material has a base
    // color factor but no base color texture, so the shader can still sample
    // `baseColorTexture * baseColorFactor` without branching.
    std::shared_ptr<Texture> GetWhiteTexture();
    // Releases the cache. Textures still referenced elsewhere stay alive.
    void Clear();

  private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
    std::shared_ptr<Texture> m_whiteTexture;
};

} // namespace Geni
