#pragma once
#include <GL/glew.h>
#include <cstdint>
#include <vector>

namespace Orbis
{

struct VertexElement
{
    GLuint index;    // attribute location
    GLuint size;     // number of components
    GLuint type;     // data type (e.g. GL_FLOAT)
    uint32_t offset; // bytes offset
};

struct VertexLayout
{
    std::vector<VertexElement> elements;
    uint32_t stride = 0; // total size of a single vertex
};

} // namespace Orbis