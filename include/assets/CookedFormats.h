#pragma once

#include "assets/AssetID.h"
#include "graphics/VertexLayout.h"
#include "render/Mesh.h"
#include "rhi/Types.h"
#include "scene/components/AnimationComponent.h"

#include <cstdint>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Binary formats produced by tools/asset_cooker and read by the runtime. Every file is
// little-endian, starts with a four-byte magic and a uint32 version, and is rejected as a
// whole when either is wrong. Strings are uint32 length followed by UTF-8 bytes.
namespace Geni::cooked
{

constexpr uint32_t MeshMagic = 0x48534d47;      // "GMSH"
constexpr uint32_t AnimationMagic = 0x4e4e4147; // "GANN"
constexpr uint32_t MaterialMagic = 0x54414d47;  // "GMAT"
constexpr uint32_t FormatVersion = 1;

struct CookedMeshData
{
    VertexLayout layout;
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::TriangleList;
    MeshBounds bounds;
    // Present for a skinned primitive. Joint names index GameObjects by name; the
    // inverse bind matrices are in the same order.
    std::vector<std::string> jointNames;
    std::vector<glm::mat4> inverseBindMatrices;

    bool IsSkinned() const
    {
        return !jointNames.empty();
    }
};

struct CookedAnimationData
{
    std::string name;
    float duration = 0.0f;
    bool looping = true;
    std::vector<TransformTrack> tracks;
};

struct CookedMaterialData
{
    // "builtin:default" or "builtin:skinned" for engine shaders, otherwise an asset path
    // for both stages (Shader::Load).
    std::string shader = "builtin:default";
    std::unordered_map<std::string, float> floats;
    std::unordered_map<std::string, glm::vec2> float2s;
    std::unordered_map<std::string, glm::vec4> float4s;
    std::unordered_map<std::string, AssetID> textures;
};

bool ReadMesh(const std::vector<uint8_t> &bytes, CookedMeshData &out, std::string *error = nullptr);
void WriteMesh(const CookedMeshData &data, std::vector<uint8_t> &out);

bool ReadAnimations(
    const std::vector<uint8_t> &bytes, std::vector<CookedAnimationData> &out, std::string *error = nullptr
);
void WriteAnimations(const std::vector<CookedAnimationData> &clips, std::vector<uint8_t> &out);

bool ReadMaterial(const std::vector<uint8_t> &bytes, CookedMaterialData &out, std::string *error = nullptr);
void WriteMaterial(const CookedMaterialData &data, std::vector<uint8_t> &out);

// Scene text shared by the cooker and SceneSerializer: top-level fields one per line,
// then the "nodes" array with one compact node per line, so diffs stay line-per-object.
std::string FormatSceneText(const nlohmann::ordered_json &document);

bool ReadFile(const std::filesystem::path &path, std::vector<uint8_t> &out);
bool WriteFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes);

} // namespace Geni::cooked
