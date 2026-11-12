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
    // Where a packaged game keeps its data (docs/design/0016): Contents/Resources when the
    // executable is inside a macOS .app bundle, otherwise the executable's folder.
    std::filesystem::path GetResourcesFolder() const;
    // True when the resources folder holds geni_package.json, which the packaging step
    // writes. A packaged game reads only the assets it ships: the build-time ASSETS_ROOT
    // is ignored, and games should not point SetAssetsFolder at their source tree.
    bool IsPackaged() const;

    // The folder asset paths are relative to. Resolution order: the override set with
    // SetAssetsFolder, then (not packaged) the build-time ASSETS_ROOT if it exists, then
    // `assets/` in the resources folder.
    std::filesystem::path GetAssetsFolder() const;
    // Overrides the assets folder. An empty path restores the default lookup.
    void SetAssetsFolder(const std::filesystem::path &path);

    // Where build outputs (cooked assets, compiled shaders) are looked up when the assets
    // folder does not have a file. Empty (the default) means `assets/` in the resources
    // folder. The editor points it at the project's cooked folder.
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
    // Worked out on first use; neither changes while the program runs.
    mutable std::filesystem::path m_resourcesFolder;
    mutable int m_packaged = -1;
    std::filesystem::path m_cookedFolderOverride;
};

} // namespace Geni