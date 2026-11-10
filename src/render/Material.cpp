#include "render/Material.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "graphics/Texture.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "rhi/Device.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace Geni
{

Material::Material(const Material &other)
    : m_shader(other.m_shader), m_floatParams(other.m_floatParams), m_float2Params(other.m_float2Params),
      m_float4Params(other.m_float4Params), m_textures(other.m_textures)
{
}

Material &Material::operator=(const Material &other)
{
    if (this != &other)
    {
        ReleaseGpuState();
        m_shader = other.m_shader;
        m_floatParams = other.m_floatParams;
        m_float2Params = other.m_float2Params;
        m_float4Params = other.m_float4Params;
        m_textures = other.m_textures;
    }
    return *this;
}

Material::~Material()
{
    ReleaseGpuState();
}

void Material::ReleaseGpuState()
{
    if (rhi::Device *device = Engine::GetInstance().GetRenderer().GetDevice())
    {
        device->DestroyBindGroup(m_bindGroup);
        device->DestroyBuffer(m_uniformBuffer);
    }
    m_bindGroup = {};
    m_uniformBuffer = {};
    m_gpuShaderId = 0;
    m_uniformsDirty = true;
    m_bindingsDirty = true;
}

void Material::SetShader(const std::shared_ptr<Shader> &shader)
{
    m_shader = shader;
}

void Material::SetParam(const std::string &name, float value)
{
    m_floatParams[name] = value;
    m_uniformsDirty = true;
}

void Material::SetParam(const std::string &name, float v0, float v1)
{
    m_float2Params[name] = {v0, v1};
    m_uniformsDirty = true;
}

void Material::SetParam(const std::string &name, const glm::vec4 &value)
{
    m_float4Params[name] = value;
    m_uniformsDirty = true;
}

void Material::SetParam(const std::string &name, const std::shared_ptr<Texture> &texture)
{
    m_textures[name] = texture;
    m_bindingsDirty = true;
}

rhi::BindGroupHandle Material::PrepareBindGroup(Renderer &renderer)
{
    rhi::Device *device = renderer.GetDevice();
    if (!m_shader || !device)
    {
        return {};
    }

    if (m_gpuShaderId != m_shader->GetId())
    {
        ReleaseGpuState();
        m_gpuShaderId = m_shader->GetId();
        if (m_shader->GetMaterialBlockSize() > 0)
        {
            m_uniformBuffer = device->CreateBuffer({m_shader->GetMaterialBlockSize(), rhi::BufferUsage::Uniform,
                                                    rhi::MemoryUsage::GpuOnly, "material uniforms"});
        }
    }

    if (m_uniformsDirty && m_uniformBuffer.IsValid())
    {
        std::vector<uint8_t> block(m_shader->GetMaterialBlockSize(), 0);
        auto write = [&](const std::string &name, const void *value, uint32_t size) {
            if (const Shader::Uniform *uniform = m_shader->FindMaterialUniform(name))
            {
                std::memcpy(block.data() + uniform->offset, value, std::min(size, uniform->size));
            }
        };
        for (const auto &param : m_floatParams)
        {
            write(param.first, &param.second, sizeof(float));
        }
        for (const auto &param : m_float2Params)
        {
            const float values[2] = {param.second.first, param.second.second};
            write(param.first, values, sizeof(values));
        }
        for (const auto &param : m_float4Params)
        {
            write(param.first, &param.second, sizeof(glm::vec4));
        }
        // Waits for frames in flight; parameters change rarely (materials are set up once).
        device->UpdateBuffer(m_uniformBuffer, 0, block.data(), block.size());
        m_uniformsDirty = false;
    }

    if (m_bindingsDirty || !m_bindGroup.IsValid())
    {
        rhi::BindGroupDesc desc;
        desc.layout = m_shader->GetMaterialLayout();
        if (m_uniformBuffer.IsValid())
        {
            desc.entries.push_back(
                {m_shader->GetMaterialBlockBinding(), m_uniformBuffer, 0, m_shader->GetMaterialBlockSize()});
        }
        for (const auto &slot : m_shader->GetMaterialTextures())
        {
            auto it = m_textures.find(slot.name);
            std::shared_ptr<Texture> texture = it != m_textures.end() ? it->second : nullptr;
            if (!texture || !texture->GetHandle().IsValid())
            {
                texture = Engine::GetInstance().GetTextureManager().GetWhiteTexture();
            }
            rhi::BindGroupEntry entry;
            entry.binding = slot.binding;
            entry.texture = texture->GetHandle();
            entry.sampler = renderer.GetDefaultSampler();
            desc.entries.push_back(entry);
        }
        device->DestroyBindGroup(m_bindGroup);
        m_bindGroup = device->CreateBindGroup(desc);
        m_bindingsDirty = false;
    }
    return m_bindGroup;
}

std::shared_ptr<Material> Material::Load(const std::string &path)
{
    auto contents = Engine::GetInstance().GetFileSystem().LoadAssetFileText(path);
    if (contents.empty())
    {
        return nullptr;
    }

    nlohmann::json json = nlohmann::json::parse(contents);
    std::shared_ptr<Material> result;

    if (json.contains("shader"))
    {
        auto shaderObj = json["shader"];
        std::string vertexPath = shaderObj.value("vertex", "");
        std::string fragmentPath = shaderObj.value("fragment", "");

        auto shader = Shader::Load(vertexPath, fragmentPath);
        if (!shader)
        {
            return nullptr;
        }

        result = std::make_shared<Material>();
        result->SetShader(shader);
    }

    if (!result)
    {
        GENI_LOG_ERROR("Material '%s' has no \"shader\" object", path.c_str());
        return nullptr;
    }

    if (json.contains("params"))
    {
        auto paramsObj = json["params"];

        // floats
        if (paramsObj.contains("float"))
        {
            for (auto &p : paramsObj["float"])
            {
                std::string name = p.value("name", "");
                float value = p.value("value", 0.0f);
                result->SetParam(name, value);
            }
        }

        // floats 2
        if (paramsObj.contains("float2"))
        {
            for (auto &p : paramsObj["float2"])
            {
                std::string name = p.value("name", "");
                float v0 = p.value("value0", 0.0f);
                float v1 = p.value("value1", 0.0f);
                result->SetParam(name, v0, v1);
            }
        }

        // textures
        if (paramsObj.contains("textures"))
        {
            for (auto &p : paramsObj["textures"])
            {
                std::string name = p.value("name", "");
                std::string texPath = p.value("path", "");
                auto texture = Texture::Load(texPath);

                result->SetParam(name, texture);
            }
        }
    }

    return result;
}

} // namespace Geni