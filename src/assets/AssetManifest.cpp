#include "assets/AssetManifest.h"

#include "core/Log.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace Geni
{

bool AssetManifest::LoadFromFile(const std::filesystem::path &path, std::string *error)
{
    Clear();
    if (error)
    {
        error->clear();
    }

    std::ifstream file(path);
    if (!file)
    {
        if (error)
        {
            *error = "cannot open " + path.string();
        }
        return false;
    }

    nlohmann::json document;
    try
    {
        file >> document;
    }
    catch (const std::exception &e)
    {
        if (error)
        {
            *error = e.what();
        }
        return false;
    }

    if (document.value("format", std::string()) != "geni.manifest" || document.value("version", 0) != 1)
    {
        if (error)
        {
            *error = "not a GeniEngine cooked manifest";
        }
        return false;
    }

    const auto assets = document.find("assets");
    if (assets == document.end() || !assets->is_array())
    {
        if (error)
        {
            *error = "manifest has no assets array";
        }
        return false;
    }

    m_entries.reserve(assets->size());
    for (const auto &item : *assets)
    {
        AssetManifestEntry entry;
        entry.id = AssetID::FromHex(item.value("id", std::string()));
        entry.type = AssetTypeFromName(item.value("type", std::string()));
        entry.source = item.value("source", std::string());
        entry.path = item.value("path", std::string());
        entry.bcPath = item.value("bc", std::string());
        entry.astcPath = item.value("astc", std::string());
        if (!entry.id.IsValid() || entry.type == AssetType::Unknown || entry.path.empty())
        {
            GENI_LOG_WARN("cooked manifest %s: skipping an incomplete asset entry", path.string().c_str());
            continue;
        }
        if (m_byID.find(entry.id.value) != m_byID.end())
        {
            GENI_LOG_WARN("cooked manifest %s: duplicate asset ID %s", path.string().c_str(), entry.id.ToHex().c_str());
            continue;
        }
        m_byID[entry.id.value] = m_entries.size();
        m_entries.push_back(std::move(entry));
    }

    m_loaded = true;
    return true;
}

void AssetManifest::Clear()
{
    m_entries.clear();
    m_byID.clear();
    m_loaded = false;
}

const AssetManifestEntry *AssetManifest::FindBySource(AssetType type, const std::string &source) const
{
    for (const auto &entry : m_entries)
    {
        if (entry.type == type && entry.source == source)
        {
            return &entry;
        }
    }
    return nullptr;
}

const AssetManifestEntry *AssetManifest::Find(AssetID id) const
{
    auto it = m_byID.find(id.value);
    return it == m_byID.end() ? nullptr : &m_entries[it->second];
}

} // namespace Geni
