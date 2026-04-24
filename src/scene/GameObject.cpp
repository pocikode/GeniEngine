#include "scene/GameObject.h"
#include "core/Engine.h"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_float3.hpp"
#include "graphics/Texture.h"
#include "graphics/VertexLayout.h"
#include "graphics/ShaderProgram.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "scene/Skeleton.h"
#include "scene/components/AnimationComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/SkinnedMeshComponent.h"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// stb_image implementation lives in Texture.cpp; only declare the API here.
#include <stb_image.h>

namespace Geni
{

void GameObject::Update(float deltaTime)
{
    if (!m_active)
    {
        return;
    }

    for (auto &component : m_components)
    {
        component->Update(deltaTime);
    }

    for (auto it = m_children.begin(); it != m_children.end();)
    {
        if ((*it)->IsAlive())
        {
            (*it)->Update(deltaTime);
            ++it;
        }
        else
        {
            it = m_children.erase(it);
        }
    }
}

const std::string &GameObject::GetName() const
{
    return m_name;
}

void GameObject::SetName(const std::string &name)
{
    m_name = name;
}

GameObject *GameObject::GetParent()
{
    return m_parent;
}

bool GameObject::SetParent(GameObject *parent)
{
    if (!m_scene)
    {
        return false;
    }

    return m_scene->SetParent(this, parent);
}

Scene *GameObject::GetScene()
{
    return m_scene;
}

void GameObject::SetActive(bool active)
{
    m_active = active;
}

bool GameObject::IsActive() const
{
    return m_active;
}

bool GameObject::IsAlive() const
{
    return m_isAlive;
}

void GameObject::MarkForDestroy()
{
    m_isAlive = false;
}

GameObject *GameObject::FindChildByName(const std::string &name)
{
    if (m_name == name)
    {
        return this;
    }

    for (auto &child : m_children)
    {
        if (auto res = child->FindChildByName(name))
        {
            return res;
        }
    }

    return nullptr;
}

const std::vector<std::unique_ptr<GameObject>> &GameObject::GetChildren() const
{
    return m_children;
}

void GameObject::AddComponent(Component *component)
{
    if (!component)
    {
        return;
    }

    m_components.emplace_back(component);
    component->m_owner = this;
    component->Init();
}

glm::vec3 GameObject::GetPosition() const
{
    return m_position;
}

void GameObject::SetPosition(const glm::vec3 &pos)
{
    m_position = pos;
}

glm::quat GameObject::GetRotation() const
{
    return m_rotation;
}

void GameObject::SetRotation(const glm::quat &rot)
{
    m_rotation = rot;
}

glm::vec3 GameObject::GetScale() const
{
    return m_scale;
}

void GameObject::SetScale(const glm::vec3 &scale)
{
    m_scale = scale;
}

glm::mat4 GameObject::GetLocalTransform() const
{
    glm::mat4 mat(1.0f);

    // translation
    mat = glm::translate(mat, m_position);

    // rotation
    mat = mat * glm::mat4_cast(m_rotation);

    // scale
    mat = glm::scale(mat, m_scale);

    return mat;
}

glm::mat4 GameObject::GetWorldTransform() const
{
    if (m_parent)
    {
        return m_parent->GetWorldTransform() * GetLocalTransform();
    }

    return GetLocalTransform();
}

glm::vec3 GameObject::GetWorldPosition() const
{
    glm::vec4 hom = GetWorldTransform() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return glm::vec3(hom) / hom.w;
}

using NodeMap = std::unordered_map<cgltf_node *, GameObject *>;

static void ParseGLTFHierarchy(cgltf_node *node, GameObject *parent, NodeMap &nodeMap)
{
    const char *name = node->name ? node->name : "node";
    auto object = parent->GetScene()->CreateObject(name, parent);
    nodeMap[node] = object;

    if (node->has_matrix)
    {
        auto mat = glm::make_mat4(node->matrix);
        glm::vec3 translation, scale, skew;
        glm::vec4 perspective;
        glm::quat orientation;
        glm::decompose(mat, scale, orientation, translation, skew, perspective);

        object->SetPosition(translation);
        object->SetRotation(orientation);
        object->SetScale(scale);
    }
    else
    {
        if (node->has_translation)
        {
            object->SetPosition(glm::vec3(node->translation[0], node->translation[1], node->translation[2]));
        }
        if (node->has_rotation)
        {
            object->SetRotation(glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]));
        }
        if (node->has_scale)
        {
            object->SetScale(glm::vec3(node->scale[0], node->scale[1], node->scale[2]));
        }
    }

    for (cgltf_size ci = 0; ci < node->children_count; ++ci)
    {
        ParseGLTFHierarchy(node->children[ci], object, nodeMap);
    }
}

static std::shared_ptr<Skeleton> BuildSkeletonFromSkin(cgltf_skin *skin, const NodeMap &nodeMap)
{
    auto skeleton = std::make_shared<Skeleton>();

    for (cgltf_size ji = 0; ji < skin->joints_count; ++ji)
    {
        cgltf_node *jointNode = skin->joints[ji];
        auto it = nodeMap.find(jointNode);
        GameObject *jointObj = (it == nodeMap.end()) ? nullptr : it->second;

        glm::mat4 ibm(1.0f);
        if (skin->inverse_bind_matrices)
        {
            float raw[16] = {};
            cgltf_accessor_read_float(skin->inverse_bind_matrices, ji, raw, 16);
            ibm = glm::make_mat4(raw);
        }

        skeleton->AddJoint(jointObj, ibm);
    }

    return skeleton;
}

static void ParseGLTFMeshes(cgltf_node *node, const NodeMap &nodeMap, const std::filesystem::path &folder,
                            const std::shared_ptr<ShaderProgram> &skinnedShader)
{
    GameObject *object = nodeMap.at(node);

    if (node->mesh)
    {
        std::shared_ptr<Skeleton> skeleton;
        if (node->skin)
        {
            skeleton = BuildSkeletonFromSkin(node->skin, nodeMap);
        }

        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi)
        {
            auto &primitive = node->mesh->primitives[pi];
            if (primitive.type != cgltf_primitive_type_triangles)
            {
                continue;
            }

            auto readFloats = [](const cgltf_accessor *acc, cgltf_size i, float *out, int n) {
                std::fill(out, out + n, 0.0f);
                return cgltf_accessor_read_float(acc, i, out, n) == 1;
            };

            auto readIndex = [](const cgltf_accessor *acc, cgltf_size i) {
                cgltf_uint out = 0;
                cgltf_bool ok = cgltf_accessor_read_uint(acc, i, &out, 1);
                return ok ? static_cast<uint32_t>(out) : 0;
            };

            VertexLayout vertexLayout;
            // Enough slots for all supported attrs: pos/color/uv/normal/joints/weights.
            cgltf_accessor *accessors[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

            for (cgltf_size ai = 0; ai < primitive.attributes_count; ++ai)
            {
                auto &attr = primitive.attributes[ai];
                auto acc = attr.data;
                if (!acc)
                {
                    continue;
                }

                VertexElement element;
                element.type = GL_FLOAT;

                switch (attr.type)
                {
                case cgltf_attribute_type_position: {
                    accessors[VertexElement::PositionIndex] = acc;
                    element.index = VertexElement::PositionIndex;
                    element.size = 3;
                }
                break;
                case cgltf_attribute_type_color: {
                    if (attr.index != 0)
                    {
                        continue;
                    }
                    accessors[VertexElement::ColorIndex] = acc;
                    element.index = VertexElement::ColorIndex;
                    element.size = 3;
                }
                break;
                case cgltf_attribute_type_texcoord: {
                    if (attr.index != 0)
                    {
                        continue;
                    }
                    accessors[VertexElement::UVIndex] = acc;
                    element.index = VertexElement::UVIndex;
                    element.size = 2;
                }
                break;
                case cgltf_attribute_type_normal: {
                    accessors[VertexElement::NormalIndex] = acc;
                    element.index = VertexElement::NormalIndex;
                    element.size = 3;
                }
                break;
                case cgltf_attribute_type_joints: {
                    if (attr.index != 0)
                    {
                        continue;
                    }
                    accessors[VertexElement::JointsIndex] = acc;
                    element.index = VertexElement::JointsIndex;
                    element.size = 4;
                }
                break;
                case cgltf_attribute_type_weights: {
                    if (attr.index != 0)
                    {
                        continue;
                    }
                    accessors[VertexElement::WeightsIndex] = acc;
                    element.index = VertexElement::WeightsIndex;
                    element.size = 4;
                }
                break;
                default:
                    continue;
                }

                if (element.size > 0)
                {
                    element.offset = vertexLayout.stride;
                    vertexLayout.stride += element.size * sizeof(float);
                    vertexLayout.elements.push_back(element);
                }
            }

            if (!accessors[VertexElement::PositionIndex])
            {
                continue;
            }
            auto vertexCount = accessors[VertexElement::PositionIndex]->count;

            std::vector<float> vertices;
            vertices.resize((vertexLayout.stride / sizeof(float)) * vertexCount, 0.0f);

            for (cgltf_size vi = 0; vi < vertexCount; ++vi)
            {
                for (auto &el : vertexLayout.elements)
                {
                    if (!accessors[el.index])
                    {
                        continue;
                    }

                    auto index = (vi * vertexLayout.stride + el.offset) / sizeof(float);
                    float *outData = &vertices[index];
                    readFloats(accessors[el.index], vi, outData, el.size);
                }
            }

            std::shared_ptr<Mesh> mesh;
            if (primitive.indices)
            {
                auto indexCount = primitive.indices->count;
                std::vector<uint32_t> indices(indexCount);
                for (cgltf_size i = 0; i < indexCount; ++i)
                {
                    indices[i] = readIndex(primitive.indices, i);
                }
                mesh = std::make_shared<Mesh>(vertexLayout, vertices, indices);
            }
            else
            {
                mesh = std::make_shared<Mesh>(vertexLayout, vertices);
            }

            // Each primitive gets its own material instance so per-primitive textures
            // don't clobber each other on a shared material object.
            std::shared_ptr<Material> mat = std::make_shared<Material>();
            if (skeleton && skinnedShader)
            {
                mat->SetShaderProgram(skinnedShader);
            }
            else
            {
                mat->SetShaderProgram(Engine::GetInstance().GetGraphicsAPI().GetDefaultShaderProgram());
            }

            // Returns true if a real base-color texture was loaded; false otherwise.
            auto LoadTexture = [&](cgltf_texture *texture) -> bool {
                if (!texture || !texture->image)
                    return false;
                auto *image = texture->image;

                // .glb and some .gltf files embed images inline via a buffer_view.
                if (image->buffer_view)
                {
                    auto *bv = image->buffer_view;
                    const auto *imgData =
                        static_cast<const unsigned char *>(bv->buffer->data) + bv->offset;
                    int w, h, ch;
                    unsigned char *pixels = stbi_load_from_memory(imgData, (int)bv->size, &w, &h, &ch, 0);
                    if (pixels)
                    {
                        auto tex = std::make_shared<Texture>(w, h, ch, pixels);
                        stbi_image_free(pixels);
                        mat->SetParam("baseColorTexture", tex);
                        return true;
                    }
                    return false;
                }

                if (!image->uri)
                    return false;

                // glTF spec allows data URIs as a way to embed images inline in .gltf JSON.
                if (std::strncmp(image->uri, "data:", 5) == 0)
                {
                    const char *comma = std::strchr(image->uri, ',');
                    if (!comma)
                        return false;

                    // "data:<mime>;base64,..." — only the base64 form is supported by cgltf.
                    if (comma - image->uri < 7 || std::strncmp(comma - 7, ";base64", 7) != 0)
                        return false;

                    void *decoded = nullptr;
                    cgltf_options opt = {};
                    // Size unknown up front; base64 expands to ~3/4 of the encoded length, so
                    // this is an upper bound that cgltf will write the actual bytes into.
                    cgltf_size decodedSize = static_cast<cgltf_size>(std::strlen(comma + 1)) * 3 / 4;
                    if (cgltf_load_buffer_base64(&opt, decodedSize, comma + 1, &decoded) !=
                            cgltf_result_success ||
                        !decoded)
                    {
                        return false;
                    }

                    int w, h, ch;
                    unsigned char *pixels = stbi_load_from_memory(
                        static_cast<unsigned char *>(decoded), (int)decodedSize, &w, &h, &ch, 0);
                    std::free(decoded);
                    if (!pixels)
                        return false;
                    auto tex = std::make_shared<Texture>(w, h, ch, pixels);
                    stbi_image_free(pixels);
                    mat->SetParam("baseColorTexture", tex);
                    return true;
                }

                // External file referenced by URI. URL-decode first so `%20` etc. resolve
                // to real characters — cgltf_decode_uri mutates the buffer in place.
                std::string uriCopy(image->uri);
                cgltf_decode_uri(uriCopy.data());
                uriCopy.resize(std::strlen(uriCopy.c_str()));

                auto path = folder / uriCopy;
                auto tex = Engine::GetInstance().GetTextureManager().GetOrLoadTexture(path.string());
                if (tex)
                {
                    mat->SetParam("baseColorTexture", tex);
                    return true;
                }
                return false;
            };

            // Default to white factor so sampling `baseColorTexture * uBaseColorFactor`
            // passes the texture through untouched when no factor is specified.
            glm::vec4 baseColorFactor(1.0f, 1.0f, 1.0f, 1.0f);
            bool hasTexture = false;

            if (primitive.material)
            {
                auto gltfMat = primitive.material;
                if (gltfMat->has_pbr_metallic_roughness)
                {
                    const auto &pbr = gltfMat->pbr_metallic_roughness;
                    baseColorFactor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                                pbr.base_color_factor[2], pbr.base_color_factor[3]);
                    hasTexture = LoadTexture(pbr.base_color_texture.texture);
                }
                else if (gltfMat->has_pbr_specular_glossiness)
                {
                    const auto &sg = gltfMat->pbr_specular_glossiness;
                    baseColorFactor = glm::vec4(sg.diffuse_factor[0], sg.diffuse_factor[1],
                                                sg.diffuse_factor[2], sg.diffuse_factor[3]);
                    hasTexture = LoadTexture(sg.diffuse_texture.texture);
                }
            }

            mat->SetParam("uBaseColorFactor", baseColorFactor);
            // Bind a 1x1 white fallback so sampling the texture yields (1,1,1,1) and the
            // factor alone drives the color. Without this, GLSL samples an unbound unit
            // and returns black, masking the factor entirely.
            if (!hasTexture)
            {
                mat->SetParam("baseColorTexture",
                              Engine::GetInstance().GetTextureManager().GetWhiteTexture());
            }

            if (skeleton)
            {
                object->AddComponent(new SkinnedMeshComponent(mat, mesh, skeleton));
            }
            else
            {
                object->AddComponent(new MeshComponent(mat, mesh));
            }
        }
    }

    for (cgltf_size ci = 0; ci < node->children_count; ++ci)
    {
        ParseGLTFMeshes(node->children[ci], nodeMap, folder, skinnedShader);
    }
}

auto ReadScalar = [](cgltf_accessor *acc, cgltf_size index) {
    float res = 0.0f;
    cgltf_accessor_read_float(acc, index, &res, 1);
    return res;
};

auto ReadVec3 = [](cgltf_accessor *acc, cgltf_size index) {
    glm::vec3 res;
    cgltf_accessor_read_float(acc, index, glm::value_ptr(res), 3);
    return res;
};

auto ReadQuat = [](cgltf_accessor *acc, cgltf_size index) {
    float res[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    cgltf_accessor_read_float(acc, index, res, 4);
    return glm::quat(res[3], res[0], res[1], res[2]);
};

auto ReadTimes = [](cgltf_accessor *acc, std::vector<float> &outTimes) {
    outTimes.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        outTimes[i] = ReadScalar(acc, i);
    }
};

auto ReadOutputVec3 = [](cgltf_accessor *acc, std::vector<glm::vec3> &outValues) {
    outValues.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        outValues[i] = ReadVec3(acc, i);
    }
};

auto ReadOutputQuat = [](cgltf_accessor *acc, std::vector<glm::quat> &outValues) {
    outValues.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        outValues[i] = ReadQuat(acc, i);
    }
};

GameObject *GameObject::LoadGLTF(const std::string &path)
{
    auto contents = Engine::GetInstance().GetFileSystem().LoadAssetFileText(path);
    if (contents.empty())
        return nullptr;

    cgltf_options options = {};
    cgltf_data *data = nullptr;
    cgltf_result res = cgltf_parse(&options, contents.data(), contents.size(), &data);
    if (res != cgltf_result_success)
        return nullptr;

    auto fullPath = Engine::GetInstance().GetFileSystem().GetAssetsFolder() / path;
    auto fullFolderPath = fullPath.remove_filename();
    auto relativeFolderPath = std::filesystem::path(path).remove_filename();

    res = cgltf_load_buffers(&options, data, fullFolderPath.string().c_str());
    if (res != cgltf_result_success)
    {
        cgltf_free(data);
        return nullptr;
    }

    auto resultObject = Engine::GetInstance().GetScene()->CreateObject("Result");
    auto scene = &data->scenes[0];

    // Pass 1: build node hierarchy as GameObjects and record cgltf_node → GameObject mapping.
    NodeMap nodeMap;
    for (cgltf_size i = 0; i < scene->nodes_count; ++i)
    {
        ParseGLTFHierarchy(scene->nodes[i], resultObject, nodeMap);
    }

    // Load the skinned shader once — shared across all skinned primitives; each primitive
    // gets its own Material instance (so textures don't clobber each other).
    std::shared_ptr<ShaderProgram> skinnedShader;
    if (data->skins_count > 0)
    {
        auto skinnedMat = Material::Load("materials/skinned.mat");
        if (skinnedMat)
            skinnedShader = skinnedMat->GetShaderProgramShared();
        if (!skinnedShader)
            skinnedShader = Engine::GetInstance().GetGraphicsAPI().GetDefaultShaderProgram();
    }

    // Pass 2: attach mesh / skinned-mesh components.
    for (cgltf_size i = 0; i < scene->nodes_count; ++i)
    {
        ParseGLTFMeshes(scene->nodes[i], nodeMap, relativeFolderPath, skinnedShader);
    }

    std::vector<std::shared_ptr<AnimationClip>> clips;
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
    {
        auto &anim = data->animations[ai];

        auto clip = std::make_shared<AnimationClip>();
        clip->name = anim.name ? anim.name : "noname";
        clip->duration = 0.0f;

        std::unordered_map<cgltf_node *, size_t> trackIndexOf;

        auto GetOrCreateTrack = [&](cgltf_node *node) -> TransformTrack & {
            auto it = trackIndexOf.find(node);
            if (it != trackIndexOf.end())
            {
                return clip->tracks[it->second];
            }

            TransformTrack track;
            track.targetName = node->name;
            clip->tracks.push_back(track);
            size_t idx = clip->tracks.size() - 1;
            trackIndexOf[node] = idx;
            return clip->tracks[idx];
        };

        for (cgltf_size ci = 0; ci < anim.channels_count; ++ci)
        {
            auto &channel = anim.channels[ci];
            auto sampler = channel.sampler;

            if (!channel.target_node || !sampler || !sampler->input || !sampler->output)
            {
                continue;
            }

            std::vector<float> times;
            ReadTimes(sampler->input, times);

            auto &track = GetOrCreateTrack(channel.target_node);

            switch (channel.target_path)
            {
            case cgltf_animation_path_type_translation: {
                std::vector<glm::vec3> values;
                ReadOutputVec3(sampler->output, values);
                track.positions.resize(times.size());
                for (size_t i = 0; i < times.size(); ++i)
                {
                    track.positions[i].time = times[i];
                    track.positions[i].value = values[i];
                }
            }
            break;
            case cgltf_animation_path_type_rotation: {
                std::vector<glm::quat> values;
                ReadOutputQuat(sampler->output, values);
                track.rotations.resize(times.size());
                for (size_t i = 0; i < times.size(); ++i)
                {
                    track.rotations[i].time = times[i];
                    track.rotations[i].value = values[i];
                }
            }
            break;
            case cgltf_animation_path_type_scale: {
                std::vector<glm::vec3> values;
                ReadOutputVec3(sampler->output, values);
                track.scales.resize(times.size());
                for (size_t i = 0; i < times.size(); ++i)
                {
                    track.scales[i].time = times[i];
                    track.scales[i].value = values[i];
                }
            }
            break;
            default:
                break;
            }

            clip->duration = std::max(clip->duration, times.back());
        }

        clips.push_back(std::move(clip));
    }

    if (!clips.empty())
    {
        auto animComp = new AnimationComponent();
        resultObject->AddComponent(animComp);
        for (auto &clip : clips)
        {
            animComp->RegisterClip(clip->name, clip);
        }
    }

    cgltf_free(data);
    return resultObject;
}

} // namespace Geni