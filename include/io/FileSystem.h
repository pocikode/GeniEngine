#pragma once

#include <filesystem>
#include <string>
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

    // Where build outputs (cooked assets, compiled shaders) are looked up when the assets
    // folder does not have a file. Empty (the default) means `assets/` next to the
    // executable. The editor points it at the project's cooked folder.
    void SetCookedFolder(const std::filesystem::path &path);
    std::filesystem::path GetCookedFolder() const;

    // Where an asset path points: the assets folder, or else the cooked folder. Returns
    // the assets-folder path when neither exists.
    std::filesystem::path ResolveAssetPath(const std::string &relativePath) const;

    std::vector<char> LoadFile(const std::filesystem::path &path);
    std::vector<char> LoadAssetFile(const std::string &relativePath);
    std::string LoadAssetFileText(const std::string &relativePath);

  private:
    std::filesystem::path m_assetsFolderOverride;
    std::filesystem::path m_cookedFolderOverride;
};

} // namespace Geni