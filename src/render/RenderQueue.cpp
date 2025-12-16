#include "render/RenderQueue.h"
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
    for (auto &command : m_commands)
    {
        graphicsAPI.BindMaterial(command.material);
        auto shaderProgram = command.material->GetShaderProgram();
        shaderProgram->SetUniform("uModel", command.modelMatrix);
        shaderProgram->SetUniform("uView", cameraData.viewMatrix);
        shaderProgram->SetUniform("uProjection", cameraData.projectionMatrix);

        if (!lights.empty())
        {
            auto &light = lights[0];
            shaderProgram->SetUniform("uLight.color", light.color);
            shaderProgram->SetUniform("uLight.position", light.position);
        }

        graphicsAPI.BindMesh(command.mesh);
        graphicsAPI.DrawMesh(command.mesh);
    }

    m_commands.clear();
}

} // namespace Geni