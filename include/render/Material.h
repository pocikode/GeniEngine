#pragma once
#include "rhi/Handle.h"
#include <cstdint>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Geni
{

class Renderer;
class Shader;
class Texture;

// A shader plus named parameters. Parameter names match the members of the shader's
// material uniform block and the names of its sampler2D textures (set 1, see
// render/ShaderInterface.h); names the shader does not use are ignored, as are unset
// ones (zero for values, the white texture for textures).
//
// Copying a material copies its shader and parameters; GPU state is not shared.
class Material
{
  public:
    Material() = default;
    Material(const Material &other);
    Material &operator=(const Material &other);
    ~Material();

    void SetShader(const std::shared_ptr<Shader> &shader);
    Shader *GetShader() const
    {
        return m_shader.get();
    }
    const std::shared_ptr<Shader> &GetShaderShared() const
    {
        return m_shader;
    }

    void SetParam(const std::string &name, float value);
    void SetParam(const std::string &name, float v0, float v1);
    void SetParam(const std::string &name, const glm::vec4 &value);
    void SetParam(const std::string &name, const std::shared_ptr<Texture> &texture);

    // Uploads changed parameters and returns the bind group for set 1. Used by Renderer.
    rhi::BindGroupHandle PrepareBindGroup(Renderer &renderer);

    // Reads a JSON .mat file: {"shader": {"vertex", "fragment"}, "params": {"float",
    // "float2", "textures"}}. Shader paths name GLSL sources; their compiled ".spv" files
    // are loaded (Shader::Load).
    static std::shared_ptr<Material> Load(const std::string &path);

  private:
    void ReleaseGpuState();

    std::shared_ptr<Shader> m_shader;
    std::unordered_map<std::string, float> m_floatParams;
    std::unordered_map<std::string, std::pair<float, float>> m_float2Params;
    std::unordered_map<std::string, glm::vec4> m_float4Params;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;

    // GPU state, rebuilt on demand by PrepareBindGroup.
    uint64_t m_gpuShaderId = 0;
    rhi::BufferHandle m_uniformBuffer;
    rhi::BindGroupHandle m_bindGroup;
    bool m_uniformsDirty = true;
    bool m_bindingsDirty = true;
};

} // namespace Geni
