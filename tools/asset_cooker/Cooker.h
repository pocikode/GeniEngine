#pragma once

#include "TextureCooker.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Geni::cook
{

struct CookOptions
{
    std::filesystem::path sourceRoot;
    std::filesystem::path outputRoot;
    // Relative paths to cook. Empty with cookAll set discovers .gltf/.glb/.png/.jpg and
    // the editor's .gscene/.gprefab files.
    std::vector<std::string> inputs;
    // A full cook (cookAll, or no inputs) also deletes files under <output>/cooked that
    // it did not write, so formats or assets that went away do not linger.
    bool cookAll = false;
    // astc-encoder effort for every texture.
    TextureQuality textureQuality = TextureQuality::Medium;
    // Give every input without a `<file>.meta` sidecar a new one with a random GUID.
    // Writes into the source tree; commit the .meta files with their sources.
    bool writeMeta = false;
};

// Asset identity (docs/design/0004). A source file's IDs are derived from its root name:
// "guid:<16 hex digits>" when `<file>.meta` holds {"guid": "..."}, so the IDs survive
// renaming or moving the file together with its .meta, and the file's path otherwise.
// Objects inside a glTF (meshes, materials, skins, images) are addressed by their name
// when it is unique in the file, so reordering them keeps their IDs, and by index
// otherwise. A .meta may also set {"usage": "color" | "linear" | "normal"} for a
// standalone image; glTF materials set the usage of the images they reference.

// Converts source assets (glTF, images) into the engine's cooked formats and writes
// cooked_manifest.json. See docs/design/0004-jobs-and-assets.md.
class Cooker
{
  public:
    bool Run(const CookOptions &options, std::string *error = nullptr);

  private:
    struct Impl;
};

} // namespace Geni::cook
