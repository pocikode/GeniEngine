#pragma once

#include <filesystem>

namespace Orbis
{

class FileSystem
{
  public:
    std::filesystem::path GetExecutableFolder() const;
    std::filesystem::path GetAssetsFolder() const;
};

} // namespace Orbis