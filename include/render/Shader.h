#pragma once

#include "render/ShaderReflection.h"
#include "rhi/Handle.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Geni
{

// A vertex + fragment shader pair compiled to SPIR-V, with what the renderer needs to know
// about it: the material parameters and textures it reads (set 1) and its vertex inputs.
// Pipelines are made from it on demand by the Renderer, one per vertex layout.
class Shader
{
  public:
    struct Uniform
    {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
    };
    struct TextureSlot
    {
        std::string name;
        uint32_t binding = 0;
    };

    // Creates the shader from SPIR-V words. Returns nullptr and logs on failure (invalid
    // SPIR-V, or bindings that break the contract in render/ShaderInterface.h).
    static std::shared_ptr<Shader> Create(const uint32_t *vertexCode, size_t vertexBytes, const uint32_t *fragmentCode,
                                          size_t fragmentBytes, const std::string &name);
    // Loads "<vertexPath>.spv" and "<fragmentPath>.spv" from the assets folder, as compiled
    // by geni_compile_shaders. The paths are the GLSL sources' asset paths.
    static std::shared_ptr<Shader> Load(const std::string &vertexPath, const std::string &fragmentPath);

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;
    ~Shader();

    const std::string &GetName() const
    {
        return m_name;
    }
    // Unique for the life of the process; the pipeline cache is keyed by it.
    uint64_t GetId() const
    {
        return m_id;
    }

    rhi::ShaderModuleHandle GetVertexModule() const
    {
        return m_vertexModule;
    }
    rhi::ShaderModuleHandle GetFragmentModule() const
    {
        return m_fragmentModule;
    }
    rhi::BindGroupLayoutHandle GetMaterialLayout() const
    {
        return m_materialLayout;
    }

    // Material uniform block (set 1). Size 0 when the shader has none.
    uint32_t GetMaterialBlockSize() const
    {
        return m_materialBlockSize;
    }
    uint32_t GetMaterialBlockBinding() const
    {
        return m_materialBlockBinding;
    }
    const Uniform *FindMaterialUniform(const std::string &name) const;
    const std::vector<TextureSlot> &GetMaterialTextures() const
    {
        return m_textures;
    }
    const std::vector<uint32_t> &GetVertexInputLocations() const
    {
        return m_inputLocations;
    }

  private:
    Shader() = default;

    std::string m_name;
    uint64_t m_id = 0;
    rhi::ShaderModuleHandle m_vertexModule;
    rhi::ShaderModuleHandle m_fragmentModule;
    rhi::BindGroupLayoutHandle m_materialLayout;
    uint32_t m_materialBlockSize = 0;
    uint32_t m_materialBlockBinding = 0;
    std::vector<Uniform> m_uniforms;
    std::vector<TextureSlot> m_textures;
    std::vector<uint32_t> m_inputLocations;
};

} // namespace Geni
