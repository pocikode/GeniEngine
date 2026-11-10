#pragma once

#include "rhi/Handle.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Geni
{

// An 8-bit color texture with a full mip chain, sampled with the renderer's default
// (trilinear, repeating) sampler. Every texture goes through this one creation path, so
// compressed formats (BC, ASTC) can be added in M3 without touching callers.
class Texture
{
  public:
    // `data` has numChannels (1-4) bytes per pixel, top row first.
    Texture(int width, int height, int numChannels, const unsigned char *data);
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

    static std::shared_ptr<Texture> Load(const std::string &path);

  private:
    rhi::TextureHandle m_handle;
    int m_width = 0;
    int m_height = 0;
    int m_mipLevels = 1;
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
