#include "render/Shader.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "render/ShaderInterface.h"
#include "rhi/Device.h"

#include <algorithm>
#include <limits>
#include <atomic>
#include <filesystem>
#include <system_error>
#include <cstring>

namespace Geni
{

namespace
{
std::atomic<uint64_t> g_nextShaderId{1};

// Shaders made by Shader::Load, for ReloadChanged. Main thread only.
std::vector<Shader *> g_loadedShaders;

// The later modification time of the two SPIR-V files; INT64_MIN when one is missing.
// File-clock times can be negative (libstdc++'s epoch is in 2174), so 0 is not "none".
int64_t ShaderFileTime(const std::string &vertexPath, const std::string &fragmentPath)
{
    auto &fs = Engine::GetInstance().GetFileSystem();
    int64_t latest = std::numeric_limits<int64_t>::min();
    for (const std::string &file : {vertexPath + ".vert.spv", fragmentPath + ".frag.spv"})
    {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(fs.ResolveAssetPath(file), ec);
        if (ec)
        {
            return std::numeric_limits<int64_t>::min();
        }
        latest = std::max(latest, static_cast<int64_t>(time.time_since_epoch().count()));
    }
    return latest;
}

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
    const std::string vertexFile = vertexPath + ".vert.spv";
    const std::string fragmentFile = fragmentPath + ".frag.spv";
    const auto vertexBytes = fs.LoadAssetFile(vertexFile);
    const auto fragmentBytes = fs.LoadAssetFile(fragmentFile);
    for (const auto *missing :
         {vertexBytes.empty() ? &vertexFile : nullptr, fragmentBytes.empty() ? &fragmentFile : nullptr})
    {
        if (missing)
        {
            GENI_LOG_ERROR(
                "Compiled shader '%s' not found. Shaders are compiled at build time: list the "
                "source with geni_compile_shaders (cmake/GeniShaders.cmake), and give it a "
                "vertexMain or fragmentMain entry point",
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
    auto shader = Create(
        vertexWords.data(), vertexWords.size() * 4, fragmentWords.data(), fragmentWords.size() * 4,
        vertexPath + "+" + fragmentPath
    );
    if (shader)
    {
        shader->m_vertexPath = vertexPath;
        shader->m_fragmentPath = fragmentPath;
        shader->m_fileTime = ShaderFileTime(vertexPath, fragmentPath);
        g_loadedShaders.push_back(shader.get());
    }
    return shader;
}

bool Shader::Reload()
{
    if (m_vertexPath.empty())
    {
        return false;
    }
    // Remember the files as seen now, also when they do not load: a broken shader is
    // reported once, not on every poll.
    m_fileTime = ShaderFileTime(m_vertexPath, m_fragmentPath);
    const std::shared_ptr<Shader> fresh = Load(m_vertexPath, m_fragmentPath);
    if (!fresh)
    {
        GENI_LOG_ERROR("Shader '%s' did not reload; keeping the loaded version", m_name.c_str());
        return false;
    }
    // Take the new modules and ID; `fresh` leaves with the old ones and its destructor
    // releases them and the pipelines built from them.
    std::swap(m_id, fresh->m_id);
    std::swap(m_vertexModule, fresh->m_vertexModule);
    std::swap(m_fragmentModule, fresh->m_fragmentModule);
    std::swap(m_materialLayout, fresh->m_materialLayout);
    std::swap(m_materialBlockSize, fresh->m_materialBlockSize);
    std::swap(m_materialBlockBinding, fresh->m_materialBlockBinding);
    std::swap(m_uniforms, fresh->m_uniforms);
    std::swap(m_textures, fresh->m_textures);
    std::swap(m_inputLocations, fresh->m_inputLocations);
    return true;
}

int Shader::ReloadChanged()
{
    // Reload registers and unregisters a temporary shader, so walk a copy.
    const std::vector<Shader *> loaded = g_loadedShaders;
    int reloaded = 0;
    for (Shader *shader : loaded)
    {
        if (ShaderFileTime(shader->m_vertexPath, shader->m_fragmentPath) != shader->m_fileTime && shader->Reload())
        {
            ++reloaded;
        }
    }
    return reloaded;
}

Shader::~Shader()
{
    g_loadedShaders.erase(std::remove(g_loadedShaders.begin(), g_loadedShaders.end(), this), g_loadedShaders.end());
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
