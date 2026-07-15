#include "io/FileSystem.h"
#include "config.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

namespace Geni
{

std::filesystem::path FileSystem::GetExecutableFolder() const
{
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    return std::filesystem::path(buf).remove_filename();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string tmp(size, '\0');
    _NSGetExecutablePath(tmp.data(), &size);
    return std::filesystem::weakly_canonical(std::filesystem::path(tmp)).remove_filename();
#elif defined(__linux__)
    return std::filesystem::weakly_canonical(std::filesystem::read_symlink("/proc/self/exe")).remove_filename();
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path FileSystem::GetAssetsFolder() const
{
#if defined(ASSETS_ROOT)
    // ASSETS_ROOT is an absolute path baked in at build time (the build host's
    // source assets/ folder) so the app can run straight from the build tree.
    // On any other machine it may reference a drive that exists but is not ready
    // (an empty card reader, optical, or removable drive). The throwing overload
    // of exists() raises std::filesystem_error ("The device is not ready") in
    // that case, which — uncaught — aborts the process on startup. Use the
    // non-throwing overload so a bad/unreachable ASSETS_ROOT simply falls through
    // to the executable-relative assets folder that ships next to the binary.
    std::error_code ec;
    auto path = std::filesystem::path(std::string(ASSETS_ROOT));
    if (std::filesystem::exists(path, ec))
    {
        return path;
    }
#endif
    std::error_code canonEc;
    auto assets = std::filesystem::weakly_canonical(GetExecutableFolder() / "assets", canonEc);
    if (canonEc)
    {
        assets = GetExecutableFolder() / "assets";
    }
    return assets;
}

std::vector<char> FileSystem::LoadFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return {};
    }

    auto size = file.tellg();
    file.seekg(0);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size))
    {
        return {};
    }

    return buffer;
}

std::vector<char> FileSystem::LoadAssetFile(const std::string &relativePath)
{
    return LoadFile(GetAssetsFolder() / relativePath);
}

std::string FileSystem::LoadAssetFileText(const std::string &relativePath)
{
    auto buffer = LoadAssetFile(relativePath);
    return std::string(buffer.begin(), buffer.end());
}

} // namespace Geni