// The Material panel (docs/design/0027): edits a .mat with live preview, saves it (the
// project's watcher re-cooks and reloads it), and makes editable copies of glTF materials.

#include "EditorApp.h"
#include "EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Geni::Editor
{

namespace
{

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

// The texture asset a material slot uses for an image: normal maps are cooked separately.
AssetID TextureFor(AssetManager &assets, const std::string &slot, const std::string &path)
{
    const std::string suffix = Lower(slot).find("normal") != std::string::npos ? "#normal" : "";
    AssetID id = assets.FindBySource(AssetType::Texture, path + suffix);
    return id.IsValid() ? id : assets.FindBySource(AssetType::Texture, path);
}

std::string Pretty(const std::string &name)
{
    // "uBaseColorFactor" -> "Base Color Factor", "normalTexture" -> "Normal Texture".
    std::string text =
        name.size() > 1 && name[0] == 'u' && std::isupper(static_cast<unsigned char>(name[1])) ? name.substr(1) : name;
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)))
        {
            out += ' ';
        }
        out += i == 0 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
    }
    return out;
}

} // namespace

bool EditorApp::OpenMaterial(const std::string &path)
{
    const std::filesystem::path file = m_project.GetConfig().assets / path;
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    MaterialSource source;
    std::string error;
    if (!in || document.is_discarded() || !source.Load(document, &error))
    {
        m_document->SetStatus("Could not open " + path + (error.empty() ? "" : ": " + error));
        return false;
    }
    m_material = std::move(source);
    m_materialPath = path;
    m_materialSaved = m_material.ToJson();
    m_materialHistory.clear();
    m_showMaterial = true;
    return true;
}

bool EditorApp::SaveMaterial()
{
    if (m_materialPath.empty())
    {
        return false;
    }
    const std::filesystem::path file = m_project.GetConfig().assets / m_materialPath;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file);
    out << m_material.ToJson().dump(2) << '\n';
    if (!out)
    {
        m_document->SetStatus("Could not save " + m_materialPath);
        return false;
    }
    m_materialSaved = m_material.ToJson();
    m_document->SetStatus("Saved " + m_materialPath + "; it is re-cooked and reloaded");
    return true;
}

void EditorApp::ApplyMaterialLive()
{
    AssetManager &assets = Engine::GetInstance().GetAssetManager();
    const AssetID id = assets.FindBySource(AssetType::Material, m_materialPath);
    if (!id.IsValid())
    {
        return; // not cooked yet: Save, and the cook makes it
    }
    const AssetHandle<Material> handle = assets.LoadMaterial(id, false);
    Material *material = handle.Get();
    if (!material)
    {
        return;
    }
    for (const MaterialParam &param : m_material.params)
    {
        switch (param.kind)
        {
        case MaterialParamKind::Float:
            material->SetParam(param.name, param.value.x);
            break;
        case MaterialParamKind::Float2:
            material->SetParam(param.name, param.value.x, param.value.y);
            break;
        case MaterialParamKind::Float4:
            material->SetParam(param.name, param.value);
            break;
        case MaterialParamKind::Texture:
            if (!param.texture.empty())
            {
                const AssetID texture = TextureFor(assets, param.name, param.texture);
                if (texture.IsValid())
                {
                    material->SetParam(param.name, assets.LoadTexture(texture, false).Shared());
                }
            }
            break;
        }
    }
}

void EditorApp::DrawMaterialPanel()
{
    if (!m_showMaterial || m_materialPath.empty())
    {
        return;
    }
    if (!ImGui::Begin("Material", &m_showMaterial))
    {
        ImGui::End();
        return;
    }
    const bool dirty = m_material.ToJson() != m_materialSaved;
    ImGui::TextUnformatted(m_materialPath.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", m_material.shader.c_str(), dirty ? "  (unsaved)" : "");
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save"))
    {
        SaveMaterial();
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        const std::string path = m_materialPath;
        OpenMaterial(path);
        ApplyMaterialLive();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_materialHistory.empty());
    const bool undo = ImGui::Button("Undo") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                                                ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false));
    ImGui::EndDisabled();
    if (undo && !m_materialHistory.empty())
    {
        m_material.Load(m_materialHistory.back());
        m_materialHistory.pop_back();
        ApplyMaterialLive();
    }
    ImGui::Separator();

    // The project's images, for texture slots.
    AssetManager &assets = Engine::GetInstance().GetAssetManager();
    std::vector<std::string> images;
    for (const auto &entry : assets.GetManifest().GetEntries())
    {
        if (entry.type == AssetType::Texture && entry.source.find('#') == std::string::npos)
        {
            images.push_back(entry.source);
        }
    }
    std::sort(images.begin(), images.end());

    const nlohmann::json before = m_material.ToJson();
    bool changed = false;
    bool committed = false;
    for (MaterialParam &param : m_material.params)
    {
        ImGui::PushID(param.name.c_str());
        const std::string label = Pretty(param.name);
        const std::string lower = Lower(param.name);
        if (param.kind == MaterialParamKind::Texture)
        {
            ImGui::SetNextItemWidth(-140.0f);
            if (ImGui::BeginCombo(label.c_str(), param.texture.empty() ? "(none)" : param.texture.c_str()))
            {
                if (ImGui::Selectable("(none)", param.texture.empty()))
                {
                    param.texture.clear();
                    changed = committed = true;
                }
                for (const std::string &image : images)
                {
                    if (ImGui::Selectable(image.c_str(), image == param.texture))
                    {
                        param.texture = image;
                        changed = committed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        else if (param.name == "uMetallicRoughness" && m_material.IsStandard())
        {
            changed |= ImGui::SliderFloat("Metallic", &param.value.x, 0.0f, 1.0f);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= ImGui::SliderFloat("Roughness", &param.value.y, 0.0f, 1.0f);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        else if (param.name == "uNormalScaleOcclusion" && m_material.IsStandard())
        {
            changed |= ImGui::SliderFloat("Normal Scale", &param.value.x, 0.0f, 2.0f);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= ImGui::SliderFloat("Occlusion", &param.value.y, 0.0f, 1.0f);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        else if (
            param.kind == MaterialParamKind::Float4 &&
            (lower.find("color") != std::string::npos || lower.find("factor") != std::string::npos)
        )
        {
            changed |=
                ImGui::ColorEdit4(label.c_str(), &param.value.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        else
        {
            const int count = param.kind == MaterialParamKind::Float    ? 1
                              : param.kind == MaterialParamKind::Float2 ? 2
                                                                        : 4;
            changed |= ImGui::DragScalarN(label.c_str(), ImGuiDataType_Float, &param.value.x, count, 0.01f);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopID();
    }
    // A drag changes the material every frame; the history keeps one step per edit.
    static nlohmann::json dragStart;
    if (changed && dragStart.is_null())
    {
        dragStart = before;
    }
    if (changed)
    {
        ApplyMaterialLive();
    }
    if (committed && !dragStart.is_null())
    {
        m_materialHistory.push_back(dragStart);
        dragStart = nlohmann::json();
    }
    ImGui::End();
}

void EditorApp::DrawMeshMaterialActions(GameObject &object, int componentIndex)
{
    auto *mesh = static_cast<MeshComponent *>(object.GetComponents()[static_cast<size_t>(componentIndex)].get());
    const AssetManifestEntry *entry = Engine::GetInstance().GetAssetManager().GetManifest().Find(mesh->GetMaterialID());
    if (!entry)
    {
        return;
    }
    const bool file = entry->source.size() > 4 && entry->source.compare(entry->source.size() - 4, 4, ".mat") == 0;
    if (file ? ImGui::Button("Edit Material") : ImGui::Button("Make Material Editable"))
    {
        if (file)
        {
            OpenMaterial(entry->source);
        }
        else
        {
            MakeEditableMaterial(object, componentIndex);
        }
    }
    if (!file && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Copies %s into a .mat file and uses it on this mesh", entry->source.c_str());
    }
}

bool EditorApp::MakeEditableMaterial(GameObject &object, int componentIndex)
{
    auto *mesh = static_cast<MeshComponent *>(object.GetComponents()[static_cast<size_t>(componentIndex)].get());
    AssetManager &assets = Engine::GetInstance().GetAssetManager();
    const std::filesystem::path cookedFile = assets.FindCookedFile(mesh->GetMaterialID(), AssetType::Material);
    std::vector<uint8_t> bytes;
    cooked::CookedMaterialData cookedMaterial;
    if (cookedFile.empty() || !cooked::ReadFile(cookedFile, bytes) || !cooked::ReadMaterial(bytes, cookedMaterial))
    {
        m_document->SetStatus("Could not read the material of " + object.GetName());
        return false;
    }
    const MaterialSource source = MaterialSource::FromCooked(cookedMaterial, [&assets](AssetID id) {
        const AssetManifestEntry *texture = assets.GetManifest().Find(id);
        return texture ? texture->source.substr(0, texture->source.find('#')) : std::string();
    });
    // materials/<object>.mat, numbered if taken.
    const std::filesystem::path folder = m_project.GetConfig().assets;
    std::string stem;
    for (char c : object.GetName())
    {
        stem += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    std::string path = "materials/" + (stem.empty() ? std::string("material") : stem) + ".mat";
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(folder / path, ec); ++n)
    {
        path = "materials/" + stem + "_" + std::to_string(n) + ".mat";
    }
    m_material = source;
    m_materialPath = path;
    if (!SaveMaterial())
    {
        return false;
    }
    m_materialHistory.clear();
    m_showMaterial = true;
    // Cook the one new file now (small and quick), then assign it (UpdatePendingMaterial).
    std::string output;
    if (!m_project.CookFiles({path}, &output))
    {
        GENI_LOG_WARN("Make Material Editable: %s", output.c_str());
    }
    m_pendingMaterials.push_back({object.GetID(), componentIndex, path, m_time});
    m_document->SetStatus("Created " + path + "; it is used as soon as it is cooked");
    return true;
}

void EditorApp::UpdatePendingMaterial()
{
    AssetManager &assets = Engine::GetInstance().GetAssetManager();
    for (size_t i = 0; i < m_pendingMaterials.size();)
    {
        const PendingMaterialAssign &pending = m_pendingMaterials[i];
        const AssetID id = assets.FindBySource(AssetType::Material, pending.path);
        GameObject *object = m_scene->FindObjectByID(pending.object);
        if (id.IsValid() && object)
        {
            m_document->SetProperty(*object, pending.component, "material", nlohmann::json(id.ToHex()));
        }
        if (id.IsValid() || !object || m_time - pending.since > 120.0)
        {
            m_pendingMaterials.erase(m_pendingMaterials.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

} // namespace Geni::Editor
