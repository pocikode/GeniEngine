#pragma once

#include "assets/AssetID.h"

#include <string>

namespace Geni
{

// Base for things that live in the asset cache: meshes, textures, materials, animations
// and cooked scenes. The ID is stable across runs; the source path is only for
// diagnostics and the cooker manifest.
class Asset
{
  public:
    virtual ~Asset() = default;

    AssetID GetAssetID() const
    {
        return m_assetID;
    }
    void SetAssetID(AssetID id)
    {
        m_assetID = id;
    }

    const std::string &GetSourcePath() const
    {
        return m_sourcePath;
    }
    void SetSourcePath(std::string path)
    {
        m_sourcePath = std::move(path);
    }

  private:
    AssetID m_assetID;
    std::string m_sourcePath;
};

} // namespace Geni
