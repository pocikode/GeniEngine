#include "assets/CookedFormats.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <system_error>
#include <type_traits>

namespace Geni::cooked
{

namespace
{

class Writer
{
  public:
    std::vector<uint8_t> bytes;

    void U32(uint32_t value)
    {
        const size_t offset = bytes.size();
        bytes.resize(offset + 4);
        std::memcpy(bytes.data() + offset, &value, 4);
    }

    void U64(uint64_t value)
    {
        const size_t offset = bytes.size();
        bytes.resize(offset + 8);
        std::memcpy(bytes.data() + offset, &value, 8);
    }

    void F32(float value)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, 4);
        U32(bits);
    }

    void Raw(const void *data, size_t size)
    {
        const size_t offset = bytes.size();
        bytes.resize(offset + size);
        if (size > 0)
        {
            std::memcpy(bytes.data() + offset, data, size);
        }
    }

    void String(const std::string &value)
    {
        U32(static_cast<uint32_t>(value.size()));
        Raw(value.data(), value.size());
    }
};

template <typename T> std::vector<std::pair<std::string, T>> Sorted(const std::unordered_map<std::string, T> &map)
{
    std::vector<std::pair<std::string, T>> entries(map.begin(), map.end());
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    return entries;
}

class Reader
{
  public:
    Reader(const std::vector<uint8_t> &bytes, std::string *error) : m_bytes(bytes), m_error(error)
    {
    }

    bool U32(uint32_t &value)
    {
        if (!Need(4))
            return false;
        std::memcpy(&value, m_bytes.data() + m_offset, 4);
        m_offset += 4;
        return true;
    }

    bool U64(uint64_t &value)
    {
        if (!Need(8))
            return false;
        std::memcpy(&value, m_bytes.data() + m_offset, 8);
        m_offset += 8;
        return true;
    }

    bool F32(float &value)
    {
        uint32_t bits = 0;
        if (!U32(bits))
            return false;
        std::memcpy(&value, &bits, 4);
        return true;
    }

    bool Raw(void *data, size_t size)
    {
        if (!Need(size))
            return false;
        if (size > 0)
        {
            std::memcpy(data, m_bytes.data() + m_offset, size);
        }
        m_offset += size;
        return true;
    }

    bool String(std::string &value)
    {
        uint32_t size = 0;
        if (!U32(size))
            return false;
        if (size > m_bytes.size() - m_offset)
        {
            Fail("string length exceeds file size");
            return false;
        }
        value.assign(reinterpret_cast<const char *>(m_bytes.data() + m_offset), size);
        m_offset += size;
        return true;
    }

    bool AtEnd() const
    {
        return m_offset == m_bytes.size();
    }

    bool Fail(const char *message)
    {
        if (m_error && m_error->empty())
        {
            *m_error = message;
        }
        return false;
    }

    bool Need(size_t size)
    {
        if (size > m_bytes.size() - m_offset)
        {
            return Fail("unexpected end of file");
        }
        return true;
    }

  private:
    const std::vector<uint8_t> &m_bytes;
    std::string *m_error;
    size_t m_offset = 0;
};

void WriteLayout(Writer &writer, const VertexLayout &layout)
{
    writer.U32(layout.stride);
    writer.U32(static_cast<uint32_t>(layout.elements.size()));
    for (const auto &element : layout.elements)
    {
        writer.U32(element.index);
        writer.U32(element.size);
        writer.U32(static_cast<uint32_t>(element.type));
        writer.U32(element.offset);
    }
}

bool ReadLayout(Reader &reader, VertexLayout &layout)
{
    uint32_t count = 0;
    if (!reader.U32(layout.stride) || !reader.U32(count))
    {
        return false;
    }
    if (count > 64)
    {
        return reader.Fail("vertex layout has too many elements");
    }
    layout.elements.resize(count);
    for (auto &element : layout.elements)
    {
        uint32_t type = 0;
        if (!reader.U32(element.index) || !reader.U32(element.size) || !reader.U32(type) || !reader.U32(element.offset))
        {
            return false;
        }
        if (type != static_cast<uint32_t>(VertexElementType::Float))
        {
            return reader.Fail("unsupported vertex element type");
        }
        element.type = VertexElementType::Float;
        if (element.size == 0 || element.size > 4 || element.offset + element.size * sizeof(float) > layout.stride)
        {
            return reader.Fail("vertex element lies outside the vertex stride");
        }
    }
    return true;
}

void WriteBounds(Writer &writer, const MeshBounds &bounds)
{
    const float values[10] = {
        bounds.min.x, bounds.min.y,    bounds.min.z,    bounds.max.x,    bounds.max.y,
        bounds.max.z, bounds.center.x, bounds.center.y, bounds.center.z, bounds.radius,
    };
    for (float value : values)
    {
        writer.F32(value);
    }
}

bool ReadBounds(Reader &reader, MeshBounds &bounds)
{
    float values[10];
    for (float &value : values)
    {
        if (!reader.F32(value))
        {
            return false;
        }
    }
    bounds.min = glm::vec3(values[0], values[1], values[2]);
    bounds.max = glm::vec3(values[3], values[4], values[5]);
    bounds.center = glm::vec3(values[6], values[7], values[8]);
    bounds.radius = values[9];
    return true;
}

bool ReadHeader(
    Reader &reader, uint32_t expectedMagic, const char *kind, uint32_t *versionOut = nullptr,
    uint32_t newestVersion = FormatVersion
)
{
    uint32_t magic = 0;
    uint32_t version = 0;
    if (!reader.U32(magic) || !reader.U32(version))
    {
        return false;
    }
    if (magic != expectedMagic)
    {
        return reader.Fail("not a GeniEngine cooked file (bad magic)");
    }
    if (version < FormatVersion || version > newestVersion)
    {
        return reader.Fail("unsupported cooked file version");
    }
    if (versionOut)
    {
        *versionOut = version;
    }
    (void)kind;
    return true;
}

} // namespace

bool ReadMesh(const std::vector<uint8_t> &bytes, CookedMeshData &out, std::string *error)
{
    out = CookedMeshData{};
    if (error)
    {
        error->clear();
    }
    Reader reader(bytes, error);
    uint32_t version = 0;
    if (!ReadHeader(reader, MeshMagic, "mesh", &version, MeshFormatVersion))
    {
        return false;
    }

    uint32_t topology = 0;
    uint32_t vertexFloats = 0;
    uint32_t indexCount = 0;
    if (!ReadLayout(reader, out.layout) || !reader.U32(topology) || !reader.U32(vertexFloats) ||
        !reader.U32(indexCount) || !ReadBounds(reader, out.bounds))
    {
        return false;
    }
    if (topology > static_cast<uint32_t>(rhi::PrimitiveTopology::LineList))
    {
        return reader.Fail("unsupported primitive topology");
    }
    out.topology = static_cast<rhi::PrimitiveTopology>(topology);
    if (out.layout.stride == 0 || vertexFloats == 0 || vertexFloats % (out.layout.stride / sizeof(float)) != 0)
    {
        return reader.Fail("vertex data does not fit the vertex layout");
    }
    if (vertexFloats > (1u << 26) || indexCount > (1u << 26))
    {
        return reader.Fail("mesh is too large");
    }

    out.vertices.resize(vertexFloats);
    if (!reader.Raw(out.vertices.data(), out.vertices.size() * sizeof(float)))
    {
        return false;
    }
    out.indices.resize(indexCount);
    if (!reader.Raw(out.indices.data(), out.indices.size() * sizeof(uint32_t)))
    {
        return false;
    }

    uint32_t jointCount = 0;
    if (!reader.U32(jointCount))
    {
        return false;
    }
    if (jointCount > 1024)
    {
        return reader.Fail("skeleton has too many joints");
    }
    out.jointNames.resize(jointCount);
    out.inverseBindMatrices.resize(jointCount, glm::mat4(1.0f));
    for (uint32_t i = 0; i < jointCount; ++i)
    {
        if (!reader.String(out.jointNames[i]))
        {
            return false;
        }
        float values[16];
        if (!reader.Raw(values, sizeof(values)))
        {
            return false;
        }
        out.inverseBindMatrices[i] = glm::make_mat4(values);
    }

    // Version 2: LOD index ranges (docs/design/0007). Version 1 files have none, which
    // means one LOD covering every index.
    if (version >= 2)
    {
        uint32_t lodCount = 0;
        if (!reader.U32(lodCount))
        {
            return false;
        }
        if (lodCount > MaxMeshLods)
        {
            return reader.Fail("mesh has too many LODs");
        }
        out.lods.resize(lodCount);
        for (auto &lod : out.lods)
        {
            if (!reader.U32(lod.firstIndex) || !reader.U32(lod.indexCount) || !reader.Raw(&lod.error, sizeof(float)))
            {
                return false;
            }
            if (uint64_t(lod.firstIndex) + lod.indexCount > out.indices.size() || lod.indexCount == 0)
            {
                return reader.Fail("LOD index range is outside the index buffer");
            }
        }
    }

    return reader.AtEnd() ? true : reader.Fail("unexpected data after the mesh");
}

void WriteMesh(const CookedMeshData &data, std::vector<uint8_t> &out)
{
    Writer writer;
    writer.U32(MeshMagic);
    writer.U32(MeshFormatVersion);
    WriteLayout(writer, data.layout);
    writer.U32(static_cast<uint32_t>(data.topology));
    writer.U32(static_cast<uint32_t>(data.vertices.size()));
    writer.U32(static_cast<uint32_t>(data.indices.size()));
    WriteBounds(writer, data.bounds);
    writer.Raw(data.vertices.data(), data.vertices.size() * sizeof(float));
    writer.Raw(data.indices.data(), data.indices.size() * sizeof(uint32_t));
    writer.U32(static_cast<uint32_t>(data.jointNames.size()));
    for (size_t i = 0; i < data.jointNames.size(); ++i)
    {
        writer.String(data.jointNames[i]);
        const glm::mat4 &matrix = data.inverseBindMatrices[i];
        writer.Raw(glm::value_ptr(matrix), 16 * sizeof(float));
    }
    writer.U32(static_cast<uint32_t>(data.lods.size()));
    for (const auto &lod : data.lods)
    {
        writer.U32(lod.firstIndex);
        writer.U32(lod.indexCount);
        writer.Raw(&lod.error, sizeof(float));
    }
    out = std::move(writer.bytes);
}

std::string FormatSceneText(const nlohmann::ordered_json &document)
{
    std::string text = "{\n";
    bool first = true;
    auto separator = [&] {
        if (!first)
        {
            text += ",\n";
        }
        first = false;
    };
    for (const auto &[key, value] : document.items())
    {
        if (key == "nodes")
        {
            continue;
        }
        separator();
        text += "  " + nlohmann::ordered_json(key).dump() + ": " + value.dump();
    }
    const auto nodes = document.find("nodes");
    if (nodes != document.end())
    {
        separator();
        text += "  \"nodes\": [";
        if (nodes->empty())
        {
            text += "]";
        }
        else
        {
            for (size_t i = 0; i < nodes->size(); ++i)
            {
                text += i == 0 ? "\n    " : ",\n    ";
                text += (*nodes)[i].dump();
            }
            text += "\n  ]";
        }
    }
    text += "\n}\n";
    return text;
}

bool ReadAnimations(const std::vector<uint8_t> &bytes, std::vector<CookedAnimationData> &out, std::string *error)
{
    out.clear();
    if (error)
    {
        error->clear();
    }
    Reader reader(bytes, error);
    if (!ReadHeader(reader, AnimationMagic, "animation"))
    {
        return false;
    }

    uint32_t clipCount = 0;
    if (!reader.U32(clipCount))
    {
        return false;
    }
    if (clipCount > 4096)
    {
        return reader.Fail("animation file has too many clips");
    }
    out.resize(clipCount);
    for (auto &clip : out)
    {
        if (!reader.String(clip.name) || !reader.F32(clip.duration))
        {
            return false;
        }
        uint32_t looping = 0;
        uint32_t trackCount = 0;
        if (!reader.U32(looping) || !reader.U32(trackCount))
        {
            return false;
        }
        clip.looping = looping != 0;
        if (trackCount > 65536)
        {
            return reader.Fail("animation clip has too many tracks");
        }
        clip.tracks.resize(trackCount);
        for (auto &track : clip.tracks)
        {
            if (!reader.String(track.targetName))
            {
                return false;
            }
            uint32_t count = 0;
            if (!reader.U32(count))
            {
                return false;
            }
            track.positions.resize(count);
            for (auto &key : track.positions)
            {
                if (!reader.F32(key.time))
                {
                    return false;
                }
                float value[3];
                if (!reader.Raw(value, sizeof(value)))
                {
                    return false;
                }
                key.value = glm::vec3(value[0], value[1], value[2]);
            }
            if (!reader.U32(count))
            {
                return false;
            }
            track.rotations.resize(count);
            for (auto &key : track.rotations)
            {
                if (!reader.F32(key.time))
                {
                    return false;
                }
                float value[4];
                if (!reader.Raw(value, sizeof(value)))
                {
                    return false;
                }
                key.value = glm::quat(value[3], value[0], value[1], value[2]);
            }
            if (!reader.U32(count))
            {
                return false;
            }
            track.scales.resize(count);
            for (auto &key : track.scales)
            {
                if (!reader.F32(key.time))
                {
                    return false;
                }
                float value[3];
                if (!reader.Raw(value, sizeof(value)))
                {
                    return false;
                }
                key.value = glm::vec3(value[0], value[1], value[2]);
            }
        }
    }

    return reader.AtEnd() ? true : reader.Fail("unexpected data after the animations");
}

void WriteAnimations(const std::vector<CookedAnimationData> &clips, std::vector<uint8_t> &out)
{
    Writer writer;
    writer.U32(AnimationMagic);
    writer.U32(FormatVersion);
    writer.U32(static_cast<uint32_t>(clips.size()));
    for (const auto &clip : clips)
    {
        writer.String(clip.name);
        writer.F32(clip.duration);
        writer.U32(clip.looping ? 1u : 0u);
        writer.U32(static_cast<uint32_t>(clip.tracks.size()));
        for (const auto &track : clip.tracks)
        {
            writer.String(track.targetName);
            writer.U32(static_cast<uint32_t>(track.positions.size()));
            for (const auto &key : track.positions)
            {
                writer.F32(key.time);
                writer.F32(key.value.x);
                writer.F32(key.value.y);
                writer.F32(key.value.z);
            }
            writer.U32(static_cast<uint32_t>(track.rotations.size()));
            for (const auto &key : track.rotations)
            {
                writer.F32(key.time);
                writer.F32(key.value.x);
                writer.F32(key.value.y);
                writer.F32(key.value.z);
                writer.F32(key.value.w);
            }
            writer.U32(static_cast<uint32_t>(track.scales.size()));
            for (const auto &key : track.scales)
            {
                writer.F32(key.time);
                writer.F32(key.value.x);
                writer.F32(key.value.y);
                writer.F32(key.value.z);
            }
        }
    }
    out = std::move(writer.bytes);
}

bool ReadMaterial(const std::vector<uint8_t> &bytes, CookedMaterialData &out, std::string *error)
{
    out = CookedMaterialData{};
    if (error)
    {
        error->clear();
    }
    Reader reader(bytes, error);
    if (!ReadHeader(reader, MaterialMagic, "material"))
    {
        return false;
    }
    if (!reader.String(out.shader))
    {
        return false;
    }

    uint32_t count = 0;
    if (!reader.U32(count))
    {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string name;
        float value = 0.0f;
        if (!reader.String(name) || !reader.F32(value))
        {
            return false;
        }
        out.floats[name] = value;
    }
    if (!reader.U32(count))
    {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string name;
        float x = 0.0f;
        float y = 0.0f;
        if (!reader.String(name) || !reader.F32(x) || !reader.F32(y))
        {
            return false;
        }
        out.float2s[name] = glm::vec2(x, y);
    }
    if (!reader.U32(count))
    {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string name;
        float values[4];
        if (!reader.String(name) || !reader.Raw(values, sizeof(values)))
        {
            return false;
        }
        out.float4s[name] = glm::vec4(values[0], values[1], values[2], values[3]);
    }
    if (!reader.U32(count))
    {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string name;
        uint64_t value = 0;
        if (!reader.String(name) || !reader.U64(value))
        {
            return false;
        }
        out.textures[name] = AssetID{value};
    }

    return reader.AtEnd() ? true : reader.Fail("unexpected data after the material");
}

void WriteMaterial(const CookedMaterialData &data, std::vector<uint8_t> &out)
{
    Writer writer;
    writer.U32(MaterialMagic);
    writer.U32(FormatVersion);
    writer.String(data.shader);

    // Sorted keys keep cooked material files byte-identical across standard libraries.
    writer.U32(static_cast<uint32_t>(data.floats.size()));
    for (const auto &[name, value] : Sorted(data.floats))
    {
        writer.String(name);
        writer.F32(value);
    }
    writer.U32(static_cast<uint32_t>(data.float2s.size()));
    for (const auto &[name, value] : Sorted(data.float2s))
    {
        writer.String(name);
        writer.F32(value.x);
        writer.F32(value.y);
    }
    writer.U32(static_cast<uint32_t>(data.float4s.size()));
    for (const auto &[name, value] : Sorted(data.float4s))
    {
        writer.String(name);
        writer.F32(value.x);
        writer.F32(value.y);
        writer.F32(value.z);
        writer.F32(value.w);
    }
    writer.U32(static_cast<uint32_t>(data.textures.size()));
    for (const auto &[name, value] : Sorted(data.textures))
    {
        writer.String(name);
        writer.U64(value.value);
    }
    out = std::move(writer.bytes);
}

bool ReadFile(const std::filesystem::path &path, std::vector<uint8_t> &out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 0)
    {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (size > 0 && !file.read(reinterpret_cast<char *>(out.data()), size))
    {
        out.clear();
        return false;
    }
    return true;
}

bool WriteFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes)
{
    // A file that already has these bytes is left alone, so its modification time only
    // moves when the content does: hot reload (AssetManager::ReloadChanged) then reloads
    // what a re-cook actually changed, not everything it wrote.
    std::vector<uint8_t> existing;
    if (ReadFile(path, existing) && existing == bytes)
    {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }
    if (!bytes.empty())
    {
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return file.good();
}

} // namespace Geni::cooked
