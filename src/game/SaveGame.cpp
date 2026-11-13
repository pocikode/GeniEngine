#include "game/SaveGame.h"

#include "core/Log.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Geni
{

std::filesystem::path SaveGame::UserDataFolder()
{
#if defined(_WIN32)
    if (const char *appData = std::getenv("APPDATA"))
    {
        return std::filesystem::path(appData);
    }
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME"))
    {
        return std::filesystem::path(home) / "Library" / "Application Support";
    }
#else
    if (const char *data = std::getenv("XDG_DATA_HOME"); data && *data)
    {
        return std::filesystem::path(data);
    }
    if (const char *home = std::getenv("HOME"))
    {
        return std::filesystem::path(home) / ".local" / "share";
    }
#endif
    return std::filesystem::temp_directory_path();
}

void SaveGame::Init(const std::string &gameName, const std::filesystem::path &folder)
{
    m_folder =
        folder.empty() ? UserDataFolder() / (gameName.empty() ? std::string("GeniGame") : gameName) / "saves" : folder;
}

bool SaveGame::IsValidName(const std::string &name)
{
    if (name.empty() || name.size() > 64 || name == "." || name == "..")
    {
        return false;
    }
    for (char c : name)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_' || c == '.';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

std::filesystem::path SaveGame::PathOf(const std::string &name) const
{
    return m_folder / (name + ".json");
}

bool SaveGame::Write(const std::string &name, const nlohmann::json &document, std::string *error)
{
    auto fail = [error](const std::string &message) {
        if (error)
        {
            *error = message;
        }
        GENI_LOG_ERROR("save: %s", message.c_str());
        return false;
    };
    if (!IsValidName(name))
    {
        return fail("invalid save name '" + name + "'");
    }
    if (m_folder.empty())
    {
        return fail("the save folder is not set");
    }
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    // Write a temporary file and rename it over the old save, so a crash or a full disk
    // leaves the previous save intact.
    const std::filesystem::path target = PathOf(name);
    const std::filesystem::path temporary = m_folder / (name + ".json.tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return fail("cannot write " + temporary.string());
        }
        file << document.dump(2) << '\n';
        if (!file)
        {
            return fail("cannot write " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec)
    {
        // Windows refuses to rename over an existing file in some cases.
        std::filesystem::remove(target, ec);
        std::filesystem::rename(temporary, target, ec);
        if (ec)
        {
            return fail("cannot replace " + target.string() + ": " + ec.message());
        }
    }
    return true;
}

bool SaveGame::Read(const std::string &name, nlohmann::json &document, std::string *error) const
{
    if (!IsValidName(name))
    {
        if (error)
        {
            *error = "invalid save name";
        }
        return false;
    }
    std::ifstream file(PathOf(name), std::ios::binary);
    if (!file)
    {
        if (error)
        {
            *error = "no save named " + name;
        }
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded())
    {
        if (error)
        {
            *error = "save " + name + " is not valid JSON";
        }
        return false;
    }
    return true;
}

bool SaveGame::Exists(const std::string &name) const
{
    std::error_code ec;
    return IsValidName(name) && std::filesystem::exists(PathOf(name), ec);
}

bool SaveGame::Delete(const std::string &name)
{
    std::error_code ec;
    return IsValidName(name) && std::filesystem::remove(PathOf(name), ec);
}

} // namespace Geni
