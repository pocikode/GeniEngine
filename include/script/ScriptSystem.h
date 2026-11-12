#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace Geni
{

class FileSystem;

struct ScriptConfig
{
    bool enabled = true;
    // Re-run changed script files while the game runs (docs/design/0013).
    bool hotReload = true;
    double hotReloadInterval = 0.5;
};

// The engine's Lua 5.4 state (docs/design/0013): safe standard libraries, the engine
// bindings (ScriptBindings.cpp), `require` from the assets folder's scripts/ folder, and
// the script classes ScriptComponent instances use. Main thread only.
class ScriptSystem
{
  public:
    ScriptSystem();
    ~ScriptSystem();
    ScriptSystem(const ScriptSystem &) = delete;
    ScriptSystem &operator=(const ScriptSystem &) = delete;

    bool Init(const ScriptConfig &config, FileSystem *fileSystem);
    void Shutdown();
    bool IsInitialized() const
    {
        return m_state != nullptr;
    }
    lua_State *GetState() const
    {
        return m_state;
    }
    FileSystem *GetFileSystem() const
    {
        return m_fileSystem;
    }

    // Runs a chunk of Lua. Errors are logged (with a traceback) and returned.
    bool Run(const std::string &code, const std::string &chunkName = "chunk", std::string *error = nullptr);
    // Runs a chunk and returns its first result as a string (tests, the console).
    bool Evaluate(const std::string &code, std::string &result, std::string *error = nullptr);

    // The class table a script file returns, loaded once and cached. Pushes nothing;
    // returns a registry reference to the per-file metatable whose __index is the class
    // (stable across hot reloads), or -1 on error.
    int GetScriptMetatable(const std::string &path, std::string *error = nullptr);
    // Bumped whenever any script file is reloaded; components that failed retry then.
    uint32_t GetReloadCount() const
    {
        return m_reloadCount;
    }
    // Checks loaded files for changes (rate-limited by the config). Engine calls it once
    // per frame.
    void Update(float deltaTime);
    // Re-runs every loaded script file whose modification time changed. Returns how many.
    int ReloadChanged();

    // Calls method `name` of the instance table referenced by `instanceRef` with `self`
    // and up to one number argument. False on error (logged). Missing methods are fine.
    bool CallMethod(int instanceRef, const char *name, bool hasNumber = false, double number = 0.0);
    // A new instance table with the script's metatable and `object` set to the bound
    // object (the object userdata must be on top of the stack; it is popped).
    int CreateInstance(int metatableRef);
    void ReleaseRef(int ref);

    uint64_t GetErrorCount() const
    {
        return m_errorCount;
    }
    void ReportError(const std::string &message);

  private:
    struct ScriptFile
    {
        std::string path;
        int metatableRef = -1;
        std::filesystem::file_time_type modified{};
        bool failed = false;
        std::string error;
    };

    bool LoadFile(ScriptFile &file, std::string *error);

    lua_State *m_state = nullptr;
    FileSystem *m_fileSystem = nullptr;
    ScriptConfig m_config;
    std::unordered_map<std::string, ScriptFile> m_files;
    uint32_t m_reloadCount = 0;
    uint64_t m_errorCount = 0;
    double m_sinceCheck = 0.0;
};

} // namespace Geni
