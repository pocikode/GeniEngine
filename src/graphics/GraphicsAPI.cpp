#include "graphics/GraphicsAPI.h"
#include "graphics/ShaderProgram.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include <cstdint>
#include <iostream>
#include <memory>

namespace Orbis
{

std::shared_ptr<ShaderProgram> GraphicsAPI::CreateShaderProgram(
    const std::string &vertexSource, const std::string &fragmentSource
)
{
    GLint success;
    char infolog[512];

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char *vertexShaderCStr = vertexSource.c_str();
    glShaderSource(vertexShader, 1, &vertexShaderCStr, nullptr);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infolog);
        std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED: " << infolog << std::endl;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char *fragmentShaderCStr = fragmentSource.c_str();
    glShaderSource(fragmentShader, 1, &fragmentShaderCStr, nullptr);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infolog);
        std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED: " << infolog << std::endl;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(program, 512, nullptr, infolog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINK_FAILED: " << infolog << std::endl;
        return nullptr;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return std::make_shared<ShaderProgram>(program);
}

GLuint GraphicsAPI::CreateVertexBuffer(const std::vector<float> &vertices)
{
    GLuint VBO = 0;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return VBO;
}

GLuint GraphicsAPI::CreateIndexBuffer(const std::vector<uint32_t> &indices)
{
    GLuint EBO = 0;
    glGenBuffers(1, &EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return EBO;
}

void GraphicsAPI::SetClearColor(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
}

void GraphicsAPI::ClearBuffers()
{
    glClear(GL_COLOR_BUFFER_BIT);
}

void GraphicsAPI::BindShaderProgram(ShaderProgram *shaderProgram)
{
    if (shaderProgram)
    {
        shaderProgram->Bind();
    }
}

void GraphicsAPI::BindMaterial(Material *material)
{
    if (material)
    {
        material->Bind();
    }
}

void GraphicsAPI::BindMesh(Mesh *mesh)
{
    if (mesh)
    {
        mesh->Bind();
    }
}

void GraphicsAPI::DrawMesh(Mesh *mesh)
{
    if (mesh)
    {
        mesh->Draw();
    }
}

} // namespace Orbis