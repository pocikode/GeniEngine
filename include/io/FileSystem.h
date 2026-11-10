#pragma once

#include <filesystem>
#include <vector>

namespace Geni
{

class FileSystem
{
  public:
    std::filesystem::path GetExecutableFolder() const;
    // The folder asset paths are relative to. Resolution order: the override set with
    // SetAssetsFolder, then the build-time ASSETS_ROOT if it exists, then `assets/`
    // next to the executable.
    std::filesystem::path GetAssetsFolder() const;
    // Overrides the assets folder. An empty path restores the default lookup.
    void SetAssetsFolder(const std::filesystem::path &path);

    std::vector<char> LoadFile(const std::filesystem::path &path);
    std::vector<char> LoadAssetFile(const std::string &relativePath);
    std::string LoadAssetFileText(const std::string &relativePath);

  private:
    std::filesystem::path m_assetsFolderOverride;
};

} // namespace Geni