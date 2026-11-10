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
    // Leading palette entries the mesh's joints refer to (the rest is identity padding).
    // Culling bounds a skinned mesh by these bones; 0 means the whole palette.
    uint32_t boneCount = 0;
};

class RenderQueue
{
  public:
    void Submit(const RenderCommand &command);
    // Runs the renderer's frame (shadows, HDR scene, tonemap) with the queued commands
    // and clears the queue. The tonemap render pass stays open for later draws.
    void Draw(
        Renderer &renderer, rhi::CommandList &cmd, const CameraData &cameraData, const std::vector<LightData> &lights
    );
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