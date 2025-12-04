#include "graphics/ShaderProgram.h"

namespace Orbis
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

} // namespace Orbis