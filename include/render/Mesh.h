#pragma once
#include "graphics/VertexLayout.h"
#include <GL/glew.h>
#include <stdint.h>
#include <vector>

namespace Orbis
{

class Mesh
{
  public:
    Mesh(const VertexLayout &layout, std::vector<float> &vertices, const std::vector<uint32_t> &indices);
    Mesh(const VertexLayout &layout, std::vector<float> &vertices);
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    void Bind();
    void Draw();

  private:
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    GLuint m_EBO = 0;

    size_t m_vertexCount = 0;
    size_t m_indexCount = 0;

    VertexLayout m_vertexLayout;
};

} // namespace Orbis