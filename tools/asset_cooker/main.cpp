#include "Cooker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    Geni::cook::CookOptions options;
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        auto next = [&](const char *name) -> const char * {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "geni_asset_cooker: %s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };

        if (std::strcmp(arg, "--source-root") == 0)
        {
            options.sourceRoot = next(arg);
        }
        else if (std::strcmp(arg, "--output-root") == 0)
        {
            options.outputRoot = next(arg);
        }
        else if (std::strcmp(arg, "--input") == 0)
        {
            options.inputs.emplace_back(next(arg));
        }
        else if (std::strcmp(arg, "--all") == 0)
        {
            options.cookAll = true;
        }
        else if (std::strcmp(arg, "--texture-quality") == 0)
        {
            const char *value = next(arg);
            if (!Geni::cook::TextureQualityFromName(value, options.textureQuality))
            {
                std::fprintf(stderr, "geni_asset_cooker: unknown texture quality '%s'\n", value);
                return 2;
            }
        }
        else if (std::strcmp(arg, "--write-meta") == 0)
        {
            options.writeMeta = true;
        }
        else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
        {
            std::printf(
                "Usage: geni_asset_cooker --source-root DIR --output-root DIR [--all] [--input REL ...]\n"
                "  --all          cook every .gltf/.glb/.png/.jpg/.gscene/.gprefab under the source root\n"
                "  --input REL    cook one path relative to the source root (repeatable)\n"
                "  --texture-quality fast|medium|thorough  ASTC search effort (default medium)\n"
                "  --write-meta   create a .meta file with a new GUID for every input that has none\n"
            );
            return 0;
        }
        else
        {
            std::fprintf(stderr, "geni_asset_cooker: unknown argument '%s'\n", arg);
            return 2;
        }
    }

    if (options.sourceRoot.empty() || options.outputRoot.empty())
    {
        std::fprintf(stderr, "geni_asset_cooker: --source-root and --output-root are required\n");
        return 2;
    }

    std::string error;
    Geni::cook::Cooker cooker;
    if (!cooker.Run(options, &error))
    {
        std::fprintf(stderr, "geni_asset_cooker: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
