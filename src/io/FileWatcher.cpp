#include "io/FileWatcher.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace Geni
{

void FileWatcher::Watch(const std::filesystem::path &root, std::vector<std::string> extensions)
{
    m_root = root;
    m_extensions = std::move(extensions);
    m_files = Scan();
}

std::map<std::string, int64_t> FileWatcher::Scan() const
{
    std::map<std::string, int64_t> files;
    std::error_code ec;
    if (m_root.empty() || !std::filesystem::is_directory(m_root, ec))
    {
        return files;
    }
    for (auto it = std::filesystem::recursive_directory_iterator(m_root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        std::error_code entryError;
        if (it->is_directory(entryError))
        {
            if (name == "cooked" || (!name.empty() && name[0] == '.'))
            {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!m_extensions.empty())
        {
            std::string extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (std::find(m_extensions.begin(), m_extensions.end(), extension) == m_extensions.end())
            {
                continue;
            }
        }
        const auto time = std::filesystem::last_write_time(it->path(), entryError);
        if (entryError)
        {
            continue; // removed between listing and reading: the next scan sees it gone
        }
        const std::string relative = std::filesystem::relative(it->path(), m_root, entryError).generic_string();
        if (!entryError)
        {
            files[relative] = static_cast<int64_t>(time.time_since_epoch().count());
        }
    }
    return files;
}

std::vector<FileWatcher::Change> FileWatcher::Poll()
{
    std::map<std::string, int64_t> current = Scan();
    std::vector<Change> changes;
    for (const auto &[path, time] : current)
    {
        auto previous = m_files.find(path);
        if (previous == m_files.end())
        {
            changes.push_back({path, ChangeKind::Added});
        }
        else if (previous->second != time)
        {
            changes.push_back({path, ChangeKind::Modified});
        }
    }
    for (const auto &[path, time] : m_files)
    {
        (void)time;
        if (current.find(path) == current.end())
        {
            changes.push_back({path, ChangeKind::Removed});
        }
    }
    std::sort(changes.begin(), changes.end(), [](const Change &a, const Change &b) { return a.path < b.path; });
    m_files = std::move(current);
    return changes;
}

} // namespace Geni
