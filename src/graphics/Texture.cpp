#include "graphics/Texture.h"
#include "core/Engine.h"
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Geni
{

Texture::Texture(int width, int height, int numChannels, unsigned char *data)
    : m_width(width), m_height(height), m_numChannels(numChannels)
{
    Init(width, height, numChannels, data);
}

Texture::~Texture()
{
    if (m_ID > 0)
    {
        glDeleteTextures(1, &m_ID);
    }
}

GLuint Texture::GetID() const
{
    return m_ID;
}

void Texture::Init(int width, int height, int numChannels, unsigned char *data)
{
    glGenTextures(1, &m_ID);
    glBindTexture(GL_TEXTURE_2D, m_ID);

    int internalFormat = GL_RGB;
    int format = GL_RGB;

    if (numChannels == 4)
    {
        internalFormat = GL_RGBA;
        format = GL_RGBA;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, internalFormat, GL_UNSIGNED_BYTE, data);

    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

std::shared_ptr<Texture> Texture::Load(const std::string &path)
{
    int width, height, numChannels;

    auto fs = Geni::Engine::GetInstance().GetFileSystem();
    auto fullPath = fs.GetAssetsFolder() / path;

    if (!std::filesystem::exists(fullPath))
    {
        return nullptr;
    }

    std::shared_ptr<Texture> result;

    unsigned char *data = stbi_load(fullPath.string().c_str(), &width, &height, &numChannels, 0);
    if (data)
    {
        result = std::make_shared<Texture>(width, height, numChannels, data);
        stbi_image_free(data);
    }

    return result;
}

std::shared_ptr<Texture> TextureManager::GetOrLoadTexture(const std::string &path)
{
    auto it = m_textures.find(path);
    if (it != m_textures.end())
    {
        return it->second;
    }

    auto texture = Texture::Load(path);
    m_textures[path] = texture;

    return texture;
}

std::shared_ptr<Texture> TextureManager::GetWhiteTexture()
{
    if (!m_whiteTexture)
    {
        unsigned char pixels[4] = {255, 255, 255, 255};
        m_whiteTexture = std::make_shared<Texture>(1, 1, 4, pixels);
    }
    return m_whiteTexture;
}

} // namespace Geni