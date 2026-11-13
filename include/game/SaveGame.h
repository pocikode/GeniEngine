#pragma once

// Named JSON documents in the user's data folder (docs/design/0024).

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace Geni
{

class SaveGame
{
  public:
    // <platform user data>/<gameName>/saves; `folder` (non-empty) replaces it.
    void Init(const std::string &gameName, const std::filesystem::path &folder = {});
    const std::filesystem::path &GetFolder() const
    {
        return m_folder;
    }

    // Names are letters, digits, '-', '_' and '.' (no folders); others are refused.
    bool Write(const std::string &name, const nlohmann::json &document, std::string *error = nullptr);
    bool Read(const std::string &name, nlohmann::json &document, std::string *error = nullptr) const;
    bool Exists(const std::string &name) const;
    bool Delete(const std::string &name);

    static bool IsValidName(const std::string &name);
    // The platform's folder for per-user application data.
    static std::filesystem::path UserDataFolder();

  private:
    std::filesystem::path PathOf(const std::string &name) const;
    std::filesystem::path m_folder;
};

} // namespace Geni
