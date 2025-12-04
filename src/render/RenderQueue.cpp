#include "render/RenderQueue.h"
#include "graphics/GraphicsAPI.h"

namespace Orbis
{

void RenderQueue::Submit(const RenderCommand &command)
{
    m_commands.push_back(command);
}

void RenderQueue::Draw(GraphicsAPI &graphicsAPI)
{
    for (auto &command : m_commands)
    {
        graphicsAPI.BindMaterial(command.material);
        graphicsAPI.BindMesh(command.mesh);
        graphicsAPI.DrawMesh(command.mesh);
    }

    m_commands.clear();
}

} // namespace Orbis