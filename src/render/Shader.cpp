#include "render/Shader.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "render/ShaderInterface.h"
#include "rhi/Device.h"

#include <atomic>
#include <cstring>

namespace Geni
{

namespace
{
std::atomic<uint64_t> g_nextShaderId{1};

std::vector<uint32_t> ToWords(const std::vector<char> &bytes)
{
    std::vector<uint32_t> words(bytes.size() / 4);
    if (!words.empty())
    {
        std::memcpy(words.data(), bytes.data(), words.size() * 4);
    }
    return words;
}
} // namespace

std::shared_ptr<Shader> Shader::Create(
    const uint32_t *vertexCode, size_t vertexBytes, const uint32_t *fragmentCode, size_t fragmentBytes,
    const std::string &name
)
{
    rhi::Device *device = Engine::GetInstance().GetRenderer().GetDevice();
    if (!device)
    {
        GENI_LOG_ERROR("Shader '%s': no render device (call after Engine::Init)", name.c_str());
        return nullptr;
    }

    ShaderReflection reflection;
    ShaderReflection fragmentReflection;
    std::string error;
    if (!ReflectSpirv(vertexCode, vertexBytes, rhi::ShaderStage::Vertex, reflection, error) ||
        !ReflectSpirv(fragmentCode, fragmentBytes, rhi::ShaderStage::Fragment, fragmentReflection, error) ||
        !MergeReflection(reflection, fragmentReflection, error) || !ValidateShaderInterface(reflection, error))
    {
        GENI_LOG_ERROR("Shader '%s': %s", name.c_str(), error.c_str());
        return nullptr;
    }

    std::shared_ptr<Shader> shader(new Shader());
    shader->m_name = name;
    shader->m_id = g_nextShaderId++;
    shader->m_inputLocations = reflection.inputLocations;

    rhi::BindGroupLayoutDesc materialLayout;
    for (const auto &b : reflection.bindings)
    {
        if (b.set != ShaderInterface::MaterialSet)
        {
            continue;
        }
        if (b.kind == ShaderBindingKind::UniformBlock)
        {
            shader->m_materialBlockSize = b.size;
            shader->m_materialBlockBinding = b.binding;
            for (const auto &member : b.members)
            {
                shader->m_uniforms.push_back({member.name, member.offset, member.size});
            }
            materialLayout.entries.push_back({b.binding, rhi::BindingType::UniformBuffer, b.stages});
        }
        else
        {
            shader->m_textures.push_back({b.name, b.binding});
            materialLayout.entries.push_back({b.binding, rhi::BindingType::SampledTexture, b.stages});
        }
    }

    const std::string vsName = name + ".vert";
    const std::string fsName = name + ".frag";
    shader->m_vertexModule =
        device->CreateShaderModule({rhi::ShaderStage::Vertex, vertexCode, vertexBytes, vsName.c_str()});
    shader->m_fragmentModule =
        device->CreateShaderModule({rhi::ShaderStage::Fragment, fragmentCode, fragmentBytes, fsName.c_str()});
    shader->m_materialLayout = device->CreateBindGroupLayout(materialLayout);
    if (!shader->m_vertexModule.IsValid() || !shader->m_fragmentModule.IsValid() || !shader->m_materialLayout.IsValid())
    {
        return nullptr;
    }
    return shader;
}

std::shared_ptr<Shader> Shader::Load(const std::string &vertexPath, const std::string &fragmentPath)
{
    auto &fs = Engine::GetInstance().GetFileSystem();
    const auto vertexBytes = fs.LoadAssetFile(vertexPath + ".spv");
    const auto fragmentBytes = fs.LoadAssetFile(fragmentPath + ".spv");
    for (const auto *missing :
         {vertexBytes.empty() ? &vertexPath : nullptr, fragmentBytes.empty() ? &fragmentPath : nullptr})
    {
        if (missing)
        {
            GENI_LOG_ERROR(
                "Compiled shader '%s.spv' not found. Shaders are compiled at build time: list the "
                "source with geni_compile_shaders (cmake/GeniShaders.cmake)",
                missing->c_str()
            );
        }
    }
    if (vertexBytes.empty() || fragmentBytes.empty())
    {
        return nullptr;
    }

    const auto vertexWords = ToWords(vertexBytes);
    const auto fragmentWords = ToWords(fragmentBytes);
    return Create(
        vertexWords.data(), vertexWords.size() * 4, fragmentWords.data(), fragmentWords.size() * 4,
        vertexPath + "+" + fragmentPath
    );
}

Shader::~Shader()
{
    auto &renderer = Engine::GetInstance().GetRenderer();
    renderer.ReleasePipelines(m_id);
    if (rhi::Device *device = renderer.GetDevice())
    {
        device->DestroyShaderModule(m_vertexModule);
        device->DestroyShaderModule(m_fragmentModule);
        device->DestroyBindGroupLayout(m_materialLayout);
    }
}

const Shader::Uniform *Shader::FindMaterialUniform(const std::string &name) const
{
    for (const auto &uniform : m_uniforms)
    {
        if (uniform.name == name)
        {
            return &uniform;
        }
    }
    return nullptr;
}

} // namespace Geni
