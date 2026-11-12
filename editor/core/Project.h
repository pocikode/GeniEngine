#pragma once

#include "assets/AssetID.h"
#include "core/JobSystem.h"
#include "io/FileWatcher.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Geni
{
class AssetManager;
class FileSystem;
} // namespace Geni

namespace Geni::Editor
{

struct ProjectConfig
{
    // The source assets folder: what the user edits and what scenes are saved into.
    std::filesystem::path assets;
    // Where cooked assets, the manifest and compiled shaders go.
    std::filesystem::path cooked;
    // geni_asset_cooker. Empty: source assets are not re-cooked.
    std::filesystem::path cooker;
    // slangc and the engine's shader module folder. Empty slangc: shaders are not recompiled.
    std::filesystem::path slangc;
    std::filesystem::path shaderModules;
};

// What a hot reload did, for the status bar and the log.
struct ReloadReport
{
    std::vector<std::string> messages;
    // Prefab assets that were reloaded: their instances need rebuilding.
    std::vector<AssetID> prefabs;
    int shaders = 0;
    int assets = 0;
    bool failed = false;
};

// Runs a program and waits for it. `output` receives what it printed. Returns its exit
// code, or -1 when it could not be started. With `logPath`, the program's output goes to
// that file as it runs (so another thread can follow it) and the file is left in place.
int RunProcess(
    const std::filesystem::path &program, const std::vector<std::string> &arguments, std::string *output,
    const std::filesystem::path &logPath = {}
);

// How far a running cook has got, from the cooker's progress lines.
struct CookProgress
{
    bool active = false;
    int done = 0;
    int total = 0;
    std::string current; // the source being cooked
};

// A project as the editor sees it: the two folders, the tools that turn sources into
// what the runtime loads, and the watcher that re-runs them (docs/design/0006).
class Project
{
  public:
    Project() = default;
    Project(const Project &) = delete;
    Project &operator=(const Project &) = delete;
    // Waits for a cook or compile still running.
    ~Project();

    void Init(const ProjectConfig &config, FileSystem &fileSystem, AssetManager *assets, JobSystem *jobs);
    const ProjectConfig &GetConfig() const
    {
        return m_config;
    }

    // "scenes/city.gscene" for a file inside the assets folder, "" for one outside it.
    std::string ToAssetPath(const std::filesystem::path &path) const;
    std::filesystem::path ToAbsolute(const std::string &assetPath) const;
    // Asset paths of the files with this extension (".gscene"), sorted.
    std::vector<std::string> ListAssets(const std::string &extension) const;

    // Cooks everything, blocking. False (with the cooker's output) when it failed.
    bool Cook(std::string *output = nullptr);
    // Compiles one .slang file's entry points to SPIR-V in the cooked folder, blocking.
    // A file without entry points is a module and compiles to nothing.
    bool CompileShader(const std::string &assetPath, std::string *output = nullptr);
    // Cooks when the manifest is missing or older than a source, and compiles shaders
    // whose SPIR-V is missing or older than the source. Blocking.
    bool Prepare(std::string *output = nullptr);
    // The same on a worker, so the editor can draw while a first cook runs (minutes for
    // a large project). Poll FinishPrepare once per frame on the main thread: it returns
    // true once, when the work is done and the manifest has been reloaded.
    // `forceCook` cooks even when nothing looks stale (Reimport All).
    void StartPrepare(bool forceCook = false);
    bool IsPreparing() const
    {
        return m_prepare != nullptr;
    }
    bool FinishPrepare(bool &ok, std::string *output = nullptr);
    // Progress of the cook running now, if any (startup, hot reload or Cook).
    CookProgress GetCookProgress() const;

    // Hot reload. Call once per frame with a clock in seconds: it polls the assets folder
    // (twice a second), re-cooks and recompiles what changed on a worker, and, back on
    // the calling thread, reloads the shaders and assets. Returns true when `report`
    // describes a reload that just finished.
    bool Update(double now, ReloadReport &report);
    // Polls now and waits for the reload to finish. For tests and scripted sessions.
    bool UpdateNow(ReloadReport &report);
    bool IsBusy() const
    {
        return m_pending != nullptr || m_prepare != nullptr;
    }

  private:
    // Cooks run on a thread of their own, not on the job system: they take minutes, and
    // the main thread's synchronous asset loads wait for every outstanding job.
    struct PendingWork
    {
        std::vector<std::string> shaders;
        bool cook = false;
        std::thread thread;
        std::atomic<bool> done{false};
        std::mutex mutex;
        std::vector<std::string> messages;
        bool failed = false;
    };

    struct PrepareWork
    {
        std::thread thread;
        std::mutex mutex;
        std::string log;
        bool ok = true;
        bool done = false;
    };

    // The cook and shader compiles of Prepare; safe on a worker.
    bool PrepareFiles(std::string &log, bool forceCook = false);
    std::filesystem::path ProgressLogPath() const;
    bool Start(const std::vector<FileWatcher::Change> &changes);
    void Run(PendingWork &work);
    void Finish(ReloadReport &report);
    bool IsShaderModule(const std::string &assetPath) const;

    ProjectConfig m_config;
    FileSystem *m_fileSystem = nullptr;
    AssetManager *m_assets = nullptr;
    JobSystem *m_jobs = nullptr;
    FileWatcher m_watcher;
    double m_nextPoll = 0.0;
    std::shared_ptr<PendingWork> m_pending;
    std::shared_ptr<PrepareWork> m_prepare;
    std::shared_ptr<std::atomic<bool>> m_cooking = std::make_shared<std::atomic<bool>>(false);
};

} // namespace Geni::Editor
