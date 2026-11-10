#include "render/RenderQueue.h"
#include "core/Profiler.h"
#include "render/Material.h"
#include "render/Renderer.h"

namespace Geni
{

void RenderQueue::Submit(const RenderCommand &command)
{
    m_commands.push_back(command);
}

void RenderQueue::Draw(Renderer &renderer, rhi::CommandList &cmd, const CameraData &cameraData,
                       const std::vector<LightData> &lights)
{
    GENI_PROFILE_SCOPE("RenderQueue::Draw");

    renderer.BeginFrame(cameraData, lights);
    m_lastDrawCallCount = 0;
    for (auto &command : m_commands)
    {
        if (!command.material || !command.mesh)
        {
            continue;
        }
        if (renderer.DrawMesh(cmd, *command.mesh, *command.material, command.modelMatrix, command.bonePalette))
        {
            ++m_lastDrawCallCount;
        }
    }

    m_commands.clear();
}

void RenderQueue::Clear()
{
    m_commands.clear();
}

size_t RenderQueue::GetPendingCount() const
{
    return m_commands.size();
}

int RenderQueue::GetLastDrawCallCount() const
{
    return m_lastDrawCallCount;
}

} // namespace Geni