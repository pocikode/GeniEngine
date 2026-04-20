#pragma once
#include "glm/ext/vector_float3.hpp"
#include "graphics/VertexLayout.h"
#include <GL/glew.h>
#include <memory>
#include <stdint.h>
#include <vector>

namespace Geni
{

class Mesh
{
  public:
    Mesh(const VertexLayout &layout, std::vector<float> &vertices, const std::vector<uint32_t> &indices);
    Mesh(const VertexLayout &layout, std::vector<float> &vertices);
    Mesh(const VertexLayout &layout, std::vector<float> &vertices, GLenum drawMode);
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    void Bind();
    void Draw();

    static std::shared_ptr<Mesh> CreateBox(const glm::vec3 &extents = glm::vec3(1.0f));
    static std::shared_ptr<Mesh> CreateLines(const std::vector<glm::vec3> &positions,
                                             const std::vector<glm::vec3> &colors);

  private:
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    GLuint m_EBO = 0;

    size_t m_vertexCount = 0;
    size_t m_indexCount = 0;
    GLenum m_drawMode = GL_TRIANGLES;

    VertexLayout m_vertexLayout;
};

} // namespace Geni