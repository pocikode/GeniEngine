#pragma once

#include "core/Common.h"
#include <cstddef>
#include <glm/mat4x4.hpp>
#include <vector>

namespace Geni
{

class Mesh;
class Material;
class Renderer;

namespace rhi
{
class CommandList;
}

struct RenderCommand
{
    Mesh *mesh = nullptr;
    Material *material = nullptr;
    glm::mat4 modelMatrix;
    const std::vector<glm::mat4> *bonePalette = nullptr;
};

class RenderQueue
{
  public:
    void Submit(const RenderCommand &command);
    // Draws and clears the queued commands, inside an open render pass.
    void Draw(Renderer &renderer, rhi::CommandList &cmd, const CameraData &cameraData,
              const std::vector<LightData> &lights);
    // Drops queued commands without drawing them.
    void Clear();
    // Commands waiting for the next Draw.
    size_t GetPendingCount() const;
    // Draw calls issued by the most recent Draw.
    int GetLastDrawCallCount() const;

  private:
    std::vector<RenderCommand> m_commands;
    int m_lastDrawCallCount = 0;
};

} // namespace Geni