#include "render/RenderQueue.h"
#include "core/Profiler.h"
#include "graphics/GraphicsAPI.h"
#include "graphics/ShaderProgram.h"
#include "render/Material.h"

namespace Geni
{

void RenderQueue::Submit(const RenderCommand &command)
{
    m_commands.push_back(command);
}

void RenderQueue::Draw(GraphicsAPI &graphicsAPI, const CameraData &cameraData, const std::vector<LightData> &lights)
{
    GENI_PROFILE_SCOPE("RenderQueue::Draw");

    m_lastDrawCallCount = 0;
    for (auto &command : m_commands)
    {
        if (!command.material || !command.mesh || !command.material->GetShaderProgram())
        {
            continue;
        }

        graphicsAPI.BindMaterial(command.material);
        auto shaderProgram = command.material->GetShaderProgram();
        shaderProgram->SetUniform("uModel", command.modelMatrix);
        shaderProgram->SetUniform("uView", cameraData.viewMatrix);
        shaderProgram->SetUniform("uProjection", cameraData.projectionMatrix);
        shaderProgram->SetUniform("uCameraPos", cameraData.position);

        if (command.bonePalette && !command.bonePalette->empty())
        {
            shaderProgram->SetUniform("uBones", command.bonePalette->data(),
                                      static_cast<int>(command.bonePalette->size()));
        }

        if (!lights.empty())
        {
            auto &light = lights[0];
            shaderProgram->SetUniform("uLight.color", light.color);
            shaderProgram->SetUniform("uLight.position", light.position);
        }

        graphicsAPI.BindMesh(command.mesh);
        graphicsAPI.DrawMesh(command.mesh);
        ++m_lastDrawCallCount;
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