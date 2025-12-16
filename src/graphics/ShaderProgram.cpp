#include "graphics/ShaderProgram.h"
#include "graphics/GraphicsAPI.h"
#include "graphics/Texture.h"
#include <glm/gtc/type_ptr.hpp>

namespace Geni
{

ShaderProgram::ShaderProgram(GLuint programID) : m_ID(programID)
{
}

ShaderProgram::~ShaderProgram()
{
    glDeleteProgram(m_ID);
}

void ShaderProgram::Bind()
{
    glUseProgram(m_ID);
    m_currentTextureUnit = 0;
}

GLint ShaderProgram::GetUniformLocation(const std::string &name)
{
    auto it = m_uniformLocationCache.find(name);
    if (it != m_uniformLocationCache.end())
    {
        return it->second;
    }

    GLint loc = glGetUniformLocation(m_ID, name.c_str());
    m_uniformLocationCache[name] = loc;

    return loc;
}

void ShaderProgram::SetUniform(const std::string &name, float value)
{
    auto loc = GetUniformLocation(name);
    glUniform1f(loc, value);
}

void ShaderProgram::SetUniform(const std::string &name, float v0, float v1)
{
    auto loc = GetUniformLocation(name);
    glUniform2f(loc, v0, v1);
}

void ShaderProgram::SetUniform(const std::string &name, const glm::mat4 &mat)
{
    auto loc = GetUniformLocation(name);
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mat));
}

void ShaderProgram::SetUniform(const std::string &name, const glm::vec3 &value)
{
    auto loc = GetUniformLocation(name);
    glUniform3fv(loc, 1, glm::value_ptr(value));
}

void ShaderProgram::SetTexture(const std::string &name, Texture *texture)
{
    auto loc = GetUniformLocation(name);

    glActiveTexture(GL_TEXTURE0 + m_currentTextureUnit);
    glBindTexture(GL_TEXTURE_2D, texture->GetID());
    glUniform1i(loc, m_currentTextureUnit);
    ++m_currentTextureUnit;
}

} // namespace Geni