#pragma once

#include <GL/glew.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace Geni
{

class Texture
{
  public:
    Texture(int width, int height, int numChannels, unsigned char *data);
    ~Texture();
    GLuint GetID() const;
    void Init(int width, int height, int numChannels, unsigned char *data);

    static std::shared_ptr<Texture> Load(const std::string &path);

  private:
    GLuint m_ID = 0;
    int m_width = 0;
    int m_height = 0;
    int m_numChannels = 0;
};

class TextureManager
{
  public:
    std::shared_ptr<Texture> GetOrLoadTexture(const std::string &path);

  private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
};

} // namespace Geni