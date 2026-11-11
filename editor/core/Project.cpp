#include "Project.h"

#include "assets/AssetManager.h"
#include "core/Log.h"
#include "io/FileSystem.h"
#include "render/Shader.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

namespace Geni::Editor
{

namespace
{

std::string Quote(const std::string &text)
{
#if defined(_WIN32)
    // cmd.exe: double quotes; a quote inside is doubled.
    std::string quoted = "\"";
    for (char c : text)
    {
        quoted += c == '"' ? std::string("\"\"") : std::string(1, c);
    }
    return quoted + "\"";
#else
    std::string quoted = "'";
    for (char c : text)
    {
        quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    }
    return quoted + "'";
#endif
}

std::string ReadText(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

int64_t FileTime(const std::filesystem::path &path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

// Entry points by the engine's naming rule (cmake/GeniShaders.cmake).
void FindEntryPoints(const std::string &source, bool &vertex, bool &fragment)
{
    static const std::regex vertexPattern("(^|[ \t\r\n])vertexMain[ \t]*\\(");
    static const std::regex fragmentPattern("(^|[ \t\r\n])fragmentMain[ \t]*\\(");
    vertex = std::regex_search(source, vertexPattern);
    fragment = std::regex_search(source, fragmentPattern);
}

const std::vector<std::string> &WatchedExtensions()
{
    static const std::vector<std::string> extensions = {".slang", ".gltf", ".glb", ".bin",    ".png",
                                                        ".jpg",   ".jpeg", ".meta", ".gscene", ".gprefab"};
    return extensions;
}

} // namespace

int RunProcess(
    const std::filesystem::path &program, const std::vector<std::string> &arguments, std::string *output,
    const std::filesystem::path &logPath
)
{
    static std::atomic<int> counter{0};
    std::error_code ec;
    const std::filesystem::path log =
        !logPath.empty() ? logPath
                         : std::filesystem::temp_directory_path(ec) / ("geni_editor_process_" + std::to_string(counter++) + ".log");

    std::string command = Quote(program.string());
    for (const auto &argument : arguments)
    {
        command += " " + Quote(argument);
    }
    command += " > " + Quote(log.string()) + " 2>&1";
#if defined(_WIN32)
    // cmd /c strips one pair of quotes around the whole line when it starts with one.
    command = "\"" + command + "\"";
#endif

    const int status = std::system(command.c_str());
    if (output)
    {
        *output = ReadText(log);
    }
    if (logPath.empty())
    {
        std::filesystem::remove(log, ec);
    }
    if (status == -1)
    {
        return -1;
    }
#if defined(_WIN32)
    return status;
#else
    // A wait status: the exit code is in the high byte, a signal in the low bits.
    return (status & 0x7f) == 0 ? (status >> 8) & 0xff : -1;
#endif
}

Project::~Project()
{
    if (m_pending && m_pending->thread.joinable())
    {
        m_pending->thread.join();
    }
    if (m_prepare && m_prepare->thread.joinable())
    {
        m_prepare->thread.join();
    }
}

void Project::Init(const ProjectConfig &config, FileSystem &fileSystem, AssetManager *assets, JobSystem *jobs)
{
    if (m_pending && m_pending->thread.joinable())
    {
        m_pending->thread.join();
    }
    if (m_prepare && m_prepare->thread.joinable())
    {
        m_prepare->thread.join();
    }
    m_config = config;
    m_fileSystem = &fileSystem;
    m_assets = assets;
    m_jobs = jobs;
    m_pending.reset();
    m_nextPoll = 0.0;

    fileSystem.SetAssetsFolder(config.assets);
    fileSystem.SetCookedFolder(config.cooked);
    if (assets)
    {
        assets->ReloadManifest();
    }
    m_watcher.Watch(config.assets, WatchedExtensions());
}

std::string Project::ToAssetPath(const std::filesystem::path &path) const
{
    std::error_code ec;
    const auto absolute = std::filesystem::weakly_canonical(path, ec);
    const auto root = std::filesystem::weakly_canonical(m_config.assets, ec);
    const std::string relative = absolute.lexically_relative(root).generic_string();
    if (relative.empty() || relative == "." || relative.rfind("..", 0) == 0)
    {
        return std::string();
    }
    return relative;
}

std::filesystem::path Project::ToAbsolute(const std::string &assetPath) const
{
    return m_config.assets / assetPath;
}

std::vector<std::string> Project::ListAssets(const std::string &extension) const
{
    std::vector<std::string> paths;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(m_config.assets, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        std::error_code entryError;
        if (it->is_directory(entryError))
        {
            const std::string name = it->path().filename().string();
            if (name == "cooked" || (!name.empty() && name[0] == '.'))
            {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (Lower(it->path().extension().string()) == extension)
        {
            paths.push_back(std::filesystem::relative(it->path(), m_config.assets, entryError).generic_string());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

bool Project::Cook(std::string *output)
{
    if (m_config.cooker.empty())
    {
        if (output)
        {
            *output = "no asset cooker configured";
        }
        return false;
    }
    std::string text;
    m_cooking->store(true);
    const int code = RunProcess(
        m_config.cooker,
        {"--source-root", m_config.assets.string(), "--output-root", m_config.cooked.string(), "--all", "--progress"},
        &text, ProgressLogPath()
    );
    m_cooking->store(false);
    // Progress lines are for whoever watched the cook, not for the log.
    std::string kept;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);)
    {
        if (line.rfind("progress ", 0) != 0)
        {
            kept += line + "\n";
        }
    }
    if (output)
    {
        *output = kept;
    }
    return code == 0;
}

bool Project::IsShaderModule(const std::string &assetPath) const
{
    bool vertex = false;
    bool fragment = false;
    FindEntryPoints(ReadText(ToAbsolute(assetPath)), vertex, fragment);
    return !vertex && !fragment;
}

bool Project::CompileShader(const std::string &assetPath, std::string *output)
{
    if (m_config.slangc.empty())
    {
        if (output)
        {
            *output = "no Slang compiler configured";
        }
        return false;
    }
    bool vertex = false;
    bool fragment = false;
    FindEntryPoints(ReadText(ToAbsolute(assetPath)), vertex, fragment);

    bool ok = true;
    std::string log;
    const std::pair<bool, std::pair<const char *, const char *>> stages[2] = {
        {vertex, {"vertex", "vert"}}, {fragment, {"fragment", "frag"}}
    };
    for (const auto &[present, names] : stages)
    {
        if (!present)
        {
            continue;
        }
        const std::string entry = std::string(names.first) + "Main";
        const std::filesystem::path out = m_config.cooked / (assetPath + "." + names.second + ".spv");
        // Compile next to the target and rename, so a failed compile leaves the old
        // SPIR-V in place and the running shader keeps working.
        const std::filesystem::path temporary = out.string() + ".tmp";
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);

        // The same flags as cmake/GeniShaders.cmake.
        std::vector<std::string> arguments = {
            ToAbsolute(assetPath).string(), "-target", "spirv", "-profile", "spirv_1_3", "-matrix-layout-column-major",
            "-warnings-as-errors", "all", "-I", m_config.assets.string(),
        };
        if (!m_config.shaderModules.empty())
        {
            arguments.push_back("-I");
            arguments.push_back(m_config.shaderModules.string());
        }
        arguments.insert(arguments.end(), {"-entry", entry, "-stage", names.first, "-o", temporary.string()});

        std::string text;
        if (RunProcess(m_config.slangc, arguments, &text) != 0)
        {
            ok = false;
            log += text;
            std::filesystem::remove(temporary, ec);
            continue;
        }
        std::filesystem::rename(temporary, out, ec);
        if (ec)
        {
            ok = false;
            log += "cannot write " + out.string() + ": " + ec.message() + "\n";
        }
    }
    if (output)
    {
        *output = log;
    }
    return ok;
}

std::filesystem::path Project::ProgressLogPath() const
{
    return m_config.cooked / "cook_progress.log";
}

CookProgress Project::GetCookProgress() const
{
    CookProgress progress;
    if (!m_cooking->load())
    {
        return progress;
    }
    progress.active = true;
    // The last complete progress line of the log the cooker is writing.
    std::ifstream file(ProgressLogPath(), std::ios::binary);
    if (!file)
    {
        return progress;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size - 4096);
    file.seekg(start);
    std::string tail(static_cast<size_t>(size - start), '\0');
    file.read(tail.data(), static_cast<std::streamsize>(tail.size()));
    const size_t last = tail.rfind("progress ");
    if (last == std::string::npos)
    {
        return progress;
    }
    const size_t end = tail.find('\n', last);
    if (end == std::string::npos)
    {
        return progress;
    }
    std::istringstream line(tail.substr(last + 9, end - last - 9));
    char slash = 0;
    line >> progress.done >> slash >> progress.total;
    std::getline(line >> std::ws, progress.current);
    return progress;
}

bool Project::PrepareFiles(std::string &log, bool forceCook)
{
    bool ok = true;
    const int64_t manifestTime = FileTime(m_config.cooked / "cooked_manifest.json");
    bool stale = forceCook || manifestTime == 0;
    for (const std::string &extension : WatchedExtensions())
    {
        if (extension == ".slang")
        {
            continue;
        }
        for (const std::string &path : ListAssets(extension))
        {
            stale = stale || FileTime(ToAbsolute(path)) > manifestTime;
        }
    }
    if (stale && !m_config.cooker.empty())
    {
        std::string text;
        if (!Cook(&text))
        {
            ok = false;
            log += text;
        }
    }

    if (!m_config.slangc.empty())
    {
        for (const std::string &path : ListAssets(".slang"))
        {
            bool vertex = false;
            bool fragment = false;
            FindEntryPoints(ReadText(ToAbsolute(path)), vertex, fragment);
            const int64_t sourceTime = FileTime(ToAbsolute(path));
            const bool missing = (vertex && FileTime(m_config.cooked / (path + ".vert.spv")) < sourceTime) ||
                                 (fragment && FileTime(m_config.cooked / (path + ".frag.spv")) < sourceTime);
            std::string text;
            if (missing && !CompileShader(path, &text))
            {
                ok = false;
                log += text;
            }
        }
    }
    return ok;
}

bool Project::Prepare(std::string *output)
{
    std::string log;
    const bool ok = PrepareFiles(log);
    if (m_assets)
    {
        m_assets->ReloadManifest();
    }
    // Whatever was just written is the baseline; only later edits are hot reloads.
    m_watcher.Watch(m_config.assets, WatchedExtensions());
    if (output)
    {
        *output = log;
    }
    return ok;
}

void Project::StartPrepare(bool forceCook)
{
    auto work = std::make_shared<PrepareWork>();
    m_prepare = work;
    PrepareWork *raw = work.get();
    work->thread = std::thread([this, raw, forceCook] {
        std::string log;
        const bool ok = PrepareFiles(log, forceCook);
        std::lock_guard<std::mutex> lock(raw->mutex);
        raw->log = std::move(log);
        raw->ok = ok;
        raw->done = true;
    });
}

bool Project::FinishPrepare(bool &ok, std::string *output)
{
    if (!m_prepare)
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_prepare->mutex);
        if (!m_prepare->done)
        {
            return false;
        }
        ok = m_prepare->ok;
        if (output)
        {
            *output = m_prepare->log;
        }
    }
    m_prepare->thread.join();
    m_prepare.reset();
    if (m_assets)
    {
        m_assets->ReloadManifest();
    }
    m_watcher.Watch(m_config.assets, WatchedExtensions());
    return true;
}

bool Project::Start(const std::vector<FileWatcher::Change> &changes)
{
    auto work = std::make_shared<PendingWork>();
    bool moduleChanged = false;
    for (const auto &change : changes)
    {
        const std::string extension = Lower(std::filesystem::path(change.path).extension().string());
        if (extension == ".slang")
        {
            if (change.kind == FileWatcher::ChangeKind::Removed)
            {
                continue;
            }
            if (IsShaderModule(change.path))
            {
                moduleChanged = true;
            }
            else
            {
                work->shaders.push_back(change.path);
            }
        }
        else if (extension == ".gscene" || extension == ".gprefab")
        {
            // The runtime reads these from the assets folder as they are; only the
            // manifest (the list of what exists) needs the cooker.
            work->cook = work->cook || change.kind != FileWatcher::ChangeKind::Modified;
        }
        else
        {
            work->cook = true;
        }
    }
    if (moduleChanged)
    {
        // Any shader may import the module: recompile them all.
        work->shaders.clear();
        for (const std::string &path : ListAssets(".slang"))
        {
            if (!IsShaderModule(path))
            {
                work->shaders.push_back(path);
            }
        }
    }
    work->cook = work->cook && !m_config.cooker.empty();
    if (m_config.slangc.empty())
    {
        work->shaders.clear();
    }

    m_pending = work;
    if (work->cook || !work->shaders.empty())
    {
        PendingWork *raw = work.get();
        work->thread = std::thread([this, raw] {
            Run(*raw);
            raw->done = true;
        });
    }
    else
    {
        Run(*work);
        work->done = true;
    }
    return true;
}

void Project::Run(PendingWork &work)
{
    std::vector<std::string> messages;
    bool failed = false;
    for (const std::string &shader : work.shaders)
    {
        std::string text;
        if (CompileShader(shader, &text))
        {
            messages.push_back("Compiled " + shader);
        }
        else
        {
            failed = true;
            messages.push_back("Shader " + shader + " did not compile:\n" + text);
        }
    }
    if (work.cook)
    {
        std::string text;
        if (Cook(&text))
        {
            messages.push_back("Cooked assets");
        }
        else
        {
            failed = true;
            messages.push_back("Cooking failed:\n" + text);
        }
    }
    std::lock_guard<std::mutex> lock(work.mutex);
    work.messages = std::move(messages);
    work.failed = failed;
}

void Project::Finish(ReloadReport &report)
{
    if (m_pending->thread.joinable())
    {
        m_pending->thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_pending->mutex);
        report.messages = m_pending->messages;
        report.failed = m_pending->failed;
    }
    m_pending.reset();

    report.shaders = Shader::ReloadChanged();
    if (m_assets)
    {
        const std::vector<AssetID> reloaded = m_assets->ReloadChanged();
        report.assets = static_cast<int>(reloaded.size());
        for (AssetID id : reloaded)
        {
            const AssetManifestEntry *entry = m_assets->GetManifest().Find(id);
            if (entry && entry->type == AssetType::Prefab)
            {
                report.prefabs.push_back(id);
            }
        }
    }
    for (const std::string &message : report.messages)
    {
        if (report.failed)
        {
            GENI_LOG_WARN("hot reload: %s", message.c_str());
        }
        else
        {
            GENI_LOG_INFO("hot reload: %s", message.c_str());
        }
    }
    if (report.shaders > 0 || report.assets > 0)
    {
        GENI_LOG_INFO("hot reload: %d shaders, %d assets reloaded", report.shaders, report.assets);
    }
}

bool Project::Update(double now, ReloadReport &report)
{
    report = ReloadReport();
    if (m_prepare)
    {
        return false;
    }
    if (m_pending)
    {
        if (!m_pending->done)
        {
            return false;
        }
        Finish(report);
        return true;
    }
    if (now < m_nextPoll)
    {
        return false;
    }
    m_nextPoll = now + 0.5;
    const std::vector<FileWatcher::Change> changes = m_watcher.Poll();
    if (!changes.empty())
    {
        Start(changes);
    }
    return false;
}

bool Project::UpdateNow(ReloadReport &report)
{
    report = ReloadReport();
    if (!m_pending)
    {
        const std::vector<FileWatcher::Change> changes = m_watcher.Poll();
        if (changes.empty())
        {
            return false;
        }
        Start(changes);
    }
    Finish(report);
    return true;
}

} // namespace Geni::Editor
