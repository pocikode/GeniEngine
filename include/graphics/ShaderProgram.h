#pragma once
#include <GL/glew.h>
#include <string>
#include <unordered_map>

namespace Orbis
{

class ShaderProgram
{
  public:
    ShaderProgram() = delete;
    ShaderProgram(const ShaderProgram &) = delete;
    ShaderProgram &operator=(const ShaderProgram &) = delete;
    explicit ShaderProgram(GLuint programID);
    ~ShaderProgram();

    void Bind();
    GLint GetUniformLocation(const std::string &name);
    void SetUniform(const std::string &name, float value);
    void SetUniform(const std::string &name, float v0, float v1);

  private:
    GLuint m_ID = 0;
    std::unordered_map<std::string, GLint> m_uniformLocationCache;
};

} // namespace Orbis