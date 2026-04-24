#pragma once
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace Geni
{

class ShaderProgram;
class Texture;

class Material
{
  public:
    void SetShaderProgram(const std::shared_ptr<ShaderProgram> &shaderProgram);
    ShaderProgram *GetShaderProgram();
    std::shared_ptr<ShaderProgram> GetShaderProgramShared() const { return m_shaderProgram; }

    void SetParam(const std::string &name, float value);
    void SetParam(const std::string &name, float v0, float v1);
    void SetParam(const std::string &name, const glm::vec4 &value);
    void SetParam(const std::string &name, const std::shared_ptr<Texture> &texture);
    void Bind();

    static std::shared_ptr<Material> Load(const std::string &path);

  private:
    std::shared_ptr<ShaderProgram> m_shaderProgram;
    std::unordered_map<std::string, float> m_floatParams;
    std::unordered_map<std::string, std::pair<float, float>> m_float2Params;
    std::unordered_map<std::string, glm::vec4> m_float4Params;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
};

} // namespace Geni