// geni_editor: the GeniEngine scene editor (ROADMAP M4, docs/design/0006-editor.md).
//
// Usage: geni_editor [--project DIR | --assets DIR [--cooked DIR]] [options]
//   (none)            the project manager: recent projects, open or create one
//   --project DIR     a project folder: assets in DIR/assets, cooked output in DIR/.geni/cooked
//   --assets DIR      the source assets folder to edit
//   --cooked DIR      where cooked assets and compiled shaders go (default: next to the
//                     assets folder, in .geni/cooked)
//   --scene PATH      scene or prefab to open, relative to the assets folder
//   --run "COMMAND"   scripted session: run this editor command, then the next --run, then
//                     quit (repeatable; see EditorApp::RunScriptCommand)
//   --script FILE     the same, one command per line
//   --hidden          no window (for scripted sessions and CI)
//   --width W --height H   window size (default 1600x900)
//   --cooker FILE     geni_asset_cooker to use
//   --slangc FILE     Slang compiler to use
//   --no-hot-reload   do not watch the assets folder
//
// Exit codes: 0 ok, 1 could not start, 2 bad arguments, 3 Vulkan validation errors,
// 5 a scripted command failed.

#include "EditorApp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{

std::filesystem::path Absolute(const std::string &text)
{
    std::error_code ec;
    const auto path = std::filesystem::absolute(text, ec);
    return ec ? std::filesystem::path(text) : path.lexically_normal();
}

// A tool that was not named: the path baked in at build time when it still exists,
// otherwise a file of that name next to the editor.
std::filesystem::path FindTool(const char *bakedPath, const char *name)
{
    std::error_code ec;
    if (bakedPath && bakedPath[0] != '\0' && std::filesystem::exists(bakedPath, ec))
    {
        return bakedPath;
    }
    const auto beside = Geni::FileSystem().GetExecutableFolder() / name;
    for (const char *suffix : {"", ".exe"})
    {
        const auto candidate = std::filesystem::path(beside.string() + suffix);
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate;
        }
    }
    return {};
}

} // namespace

#if !defined(GENI_EDITOR_COOKER)
#define GENI_EDITOR_COOKER ""
#endif
#if !defined(GENI_EDITOR_SLANGC)
#define GENI_EDITOR_SLANGC ""
#endif
#if !defined(GENI_EDITOR_SHADER_MODULES)
#define GENI_EDITOR_SHADER_MODULES ""
#endif
#if !defined(GENI_EDITOR_FONT_DIR)
#define GENI_EDITOR_FONT_DIR ""
#endif

int main(int argc, char **argv)
{
    Geni::Editor::EditorOptions options;
    bool hidden = false;
    int width = 1600;
    int height = 900;
    std::string cooker;
    std::string slangc;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        // Older macOS passes a process serial number when an app is opened from Finder.
        if (arg.rfind("-psn_", 0) == 0)
        {
            continue;
        }
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "geni_editor: %s needs a value\n", arg.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--project")
        {
            const auto project = Absolute(next());
            options.project.assets = project / "assets";
            options.project.cooked = project / ".geni" / "cooked";
        }
        else if (arg == "--assets")
        {
            options.project.assets = Absolute(next());
        }
        else if (arg == "--cooked")
        {
            options.project.cooked = Absolute(next());
        }
        else if (arg == "--scene")
        {
            options.scene = next();
        }
        else if (arg == "--run")
        {
            options.script.push_back(next());
        }
        else if (arg == "--script")
        {
            const std::string file = next();
            std::ifstream stream(file);
            if (!stream)
            {
                std::fprintf(stderr, "geni_editor: cannot read %s\n", file.c_str());
                return 2;
            }
            for (std::string line; std::getline(stream, line);)
            {
                options.script.push_back(line);
            }
        }
        else if (arg == "--hidden")
        {
            hidden = true;
        }
        else if (arg == "--width")
        {
            width = std::atoi(next().c_str());
        }
        else if (arg == "--height")
        {
            height = std::atoi(next().c_str());
        }
        else if (arg == "--cooker")
        {
            cooker = next();
        }
        else if (arg == "--slangc")
        {
            slangc = next();
        }
        else if (arg == "--no-hot-reload")
        {
            options.hotReload = false;
        }
        else
        {
            std::fprintf(
                stderr,
                "geni_editor: unknown argument '%s'\n"
                "Usage: geni_editor (--project DIR | --assets DIR [--cooked DIR]) [--scene PATH] [--run COMMAND]...\n"
                "                   [--script FILE] [--hidden] [--width W] [--height H] [--cooker FILE] [--slangc FILE]\n"
                "                   [--no-hot-reload]\n",
                arg.c_str()
            );
            return 2;
        }
    }

    std::error_code ec;
    if (options.project.assets.empty())
    {
        // Without a project the editor starts on the project manager, which needs a window
        // (or a script, to capture it).
        if (hidden && options.script.empty())
        {
            std::fprintf(
                stderr, "geni_editor: --hidden has no window for the project manager; give --project DIR "
                        "(or run without --hidden to pick a project)\n"
            );
            return 2;
        }
    }
    else
    {
        if (!std::filesystem::is_directory(options.project.assets, ec))
        {
            std::fprintf(stderr, "geni_editor: %s is not a folder\n", options.project.assets.string().c_str());
            return 2;
        }
        if (options.project.cooked.empty())
        {
            options.project.cooked = options.project.assets.parent_path() / ".geni" / "cooked";
        }
        std::filesystem::create_directories(options.project.cooked, ec);
    }

    options.project.cooker = cooker.empty() ? FindTool(GENI_EDITOR_COOKER, "geni_asset_cooker") : Absolute(cooker);
    options.project.slangc = slangc.empty() ? FindTool(GENI_EDITOR_SLANGC, "slangc") : Absolute(slangc);
    options.project.shaderModules = GENI_EDITOR_SHADER_MODULES;
    options.fontFolder = GENI_EDITOR_FONT_DIR;
    if (options.fontFolder.empty() || !std::filesystem::exists(options.fontFolder / "Roboto-Medium.ttf", ec))
    {
        options.fontFolder = Geni::FileSystem().GetExecutableFolder() / "fonts";
    }
    if (options.project.cooker.empty())
    {
        GENI_LOG_WARN("No asset cooker found: changed models and textures will not be re-cooked (--cooker FILE)");
    }
    if (options.project.slangc.empty())
    {
        GENI_LOG_WARN("No Slang compiler found: changed shaders will not be recompiled (--slangc FILE)");
    }

    Geni::EngineConfig config;
    config.title = "GeniEngine";
    config.width = width;
    config.height = height;
    config.visible = !hidden;
    config.debugOverlay = true;
    config.debugOverlayVisible = false; // the stats window; F1 or View > Engine Stats
    config.imguiDocking = true;
    if (hidden)
    {
        // A scripted run must behave the same on every machine.
        config.deterministicFrameTime = 1.0 / 60.0;
        config.vsync = false;
    }
    else
    {
        // The dock layout is the user's, the same for every project, as in Unity and Godot.
#if defined(_WIN32)
        const char *home = std::getenv("USERPROFILE");
#else
        const char *home = std::getenv("HOME");
#endif
        if (home && home[0] != '\0')
        {
            const std::filesystem::path folder = std::filesystem::path(home) / ".geni";
            std::filesystem::create_directories(folder, ec);
            config.imguiIniFile = (folder / "editor_layout.ini").string();
        }
    }

    auto app = new Geni::Editor::EditorApp(options);
    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(app);
    if (!engine.Init(config))
    {
        engine.Destroy();
        return 1;
    }
    engine.Run();
    const int exitCode = app->GetExitCode();
    engine.Destroy();

    if (const auto errors = Geni::rhi::GetValidationErrorCount())
    {
        GENI_LOG_ERROR("%llu Vulkan validation errors", static_cast<unsigned long long>(errors));
        return 3;
    }
    return exitCode;
}
