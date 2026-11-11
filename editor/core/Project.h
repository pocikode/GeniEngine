#pragma once

#include "assets/AssetID.h"
#include "core/JobSystem.h"
#include "io/FileWatcher.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
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
// code, or -1 when it could not be started.
int RunProcess(const std::filesystem::path &program, const std::vector<std::string> &arguments, std::string *output);

// A project as the editor sees it: the two folders, the tools that turn sources into
// what the runtime loads, and the watcher that re-runs them (docs/design/0006).
class Project
{
  public:
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
    // whose SPIR-V is missing or older than the source. Blocking; called at startup.
    bool Prepare(std::string *output = nullptr);

    // Hot reload. Call once per frame with a clock in seconds: it polls the assets folder
    // (twice a second), re-cooks and recompiles what changed on a worker, and, back on
    // the calling thread, reloads the shaders and assets. Returns true when `report`
    // describes a reload that just finished.
    bool Update(double now, ReloadReport &report);
    // Polls now and waits for the reload to finish. For tests and scripted sessions.
    bool UpdateNow(ReloadReport &report);
    bool IsBusy() const
    {
        return m_pending != nullptr;
    }

  private:
    struct PendingWork
    {
        std::vector<std::string> shaders;
        bool cook = false;
        JobSystem::JobHandle job;
        std::mutex mutex;
        std::vector<std::string> messages;
        bool failed = false;
    };

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
};

} // namespace Geni::Editor
