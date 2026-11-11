#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Geni
{

// Notices files that were added, changed or removed under a folder by comparing
// modification times between calls to Poll. Polling keeps it identical on every
// platform; an assets folder is a few hundred files, so a scan costs well under a
// millisecond (docs/design/0006).
class FileWatcher
{
  public:
    enum class ChangeKind
    {
        Added,
        Modified,
        Removed
    };

    struct Change
    {
        // Relative to the watched folder, with forward slashes.
        std::string path;
        ChangeKind kind = ChangeKind::Modified;
    };

    // Starts watching `root` (recursively) and records what is there now, so the first
    // Poll reports only what changed afterwards. `extensions` are lower-case with the
    // dot (".slang"); empty watches every file. Folders named "cooked" and folders whose
    // name starts with a dot are skipped.
    void Watch(const std::filesystem::path &root, std::vector<std::string> extensions = {});
    // Rescans and returns the differences since the previous scan, sorted by path.
    std::vector<Change> Poll();

    const std::filesystem::path &GetRoot() const
    {
        return m_root;
    }

  private:
    std::map<std::string, int64_t> Scan() const;

    std::filesystem::path m_root;
    std::vector<std::string> m_extensions;
    std::map<std::string, int64_t> m_files;
};

} // namespace Geni
