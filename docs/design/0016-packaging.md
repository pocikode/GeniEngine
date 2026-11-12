# 0016: Packaging a game build

Milestone: M7 · Status: Accepted · Last updated: 2026-11-12

## Problem

A game made with the engine only runs from the build tree. `samples/city/city` finds its
assets through a path baked in at build time (`GENI_SAMPLE_ASSETS_DIR`, `ASSETS_ROOT`),
its font in `fonts/` next to the executable, and on macOS GLFW, the Vulkan loader and
MoltenVK in Homebrew's folders. Nothing can be handed to another person.

**Done** means one command produces, per desktop platform, a folder or archive that runs on
a machine without the engine's source, Homebrew or the Vulkan SDK, and CI builds it:

```sh
cmake --workflow --preset package     # build/package/packages/<game>-<version>-<platform>.<zip|tar.gz>
```

## Approach

**A CMake function per game, a CMake script per package, and one marker file.**

- `geni_add_package(<target> [NAME <name>] [BUNDLE_ID <id>])` (`cmake/GeniPackage.cmake`,
  called for each sample) adds the target `package_<target>`. It depends on the game and
  its cooked assets and runs `cmake/GeniPackageGame.cmake`, which lays out the package
  and archives it.
- **What goes in**: the executable; the game's assets folder from the build tree
  (cooked assets, `cooked_manifest.json`, compiled `.spv`, scene, script, input and audio
  files) **without** the source-only files the runtime never reads (`.gltf`, `.glb`,
  `.bin`, `.png`, `.jpg`, `.jpeg`, `.slang`, `.meta`, `.py`); the UI font; and the
  shared libraries that are not part of the operating system, found with
  `file(GET_RUNTIME_DEPENDENCIES)`.
- **The marker**: a file `geni_package.json` (game name, version, platform) in the
  package's resource folder. `FileSystem::IsPackaged()` looks for it. In a packaged build
  the assets and cooked folders are the package's `assets/`, and the build-time paths
  (`ASSETS_ROOT`, a sample's source folder) are never used, so a package run on the
  developer's own machine reads only what it ships. `FileSystem::GetResourcesFolder()`
  is `Contents/Resources` in a macOS bundle and the executable's folder elsewhere; the
  engine looks for its default UI font in `<resources>/fonts`.
- **macOS**: `<Name>.app` with the executable in `Contents/MacOS`, the dylibs (GLFW, the
  Vulkan loader, MoltenVK) in `Contents/Frameworks`, their install names rewritten to
  `@rpath` with `install_name_tool`, and MoltenVK's ICD manifest in
  `Contents/Resources/vulkan/icd.d/`, which the Vulkan loader searches inside a bundle.
  The bundle is signed ad hoc (`codesign --sign -`), which arm64 needs to run at all;
  signing with a Developer ID and notarisation stay the developer's step.
- **Windows**: a folder with the executable and the DLLs it needs (GLFW from vcpkg), as a
  `.zip`. `vulkan-1.dll` is not shipped: it comes with the GPU driver.
- **Linux and Steam Deck**: a folder with the executable and a `lib/` of its non-system
  libraries (GLFW), found through an `$ORIGIN/lib` run path, as a `.tar.gz`. `libvulkan`,
  `libc`, X11 and Wayland libraries come from the system. The `package` preset links
  `libstdc++` and `libgcc` statically (`GENI_STATIC_RUNTIME`) because the Steam Linux
  Runtime's are older than a current GCC's.
- **Presets**: configure preset `package` (Release, no tests, no Tracy, `build/package`),
  build preset `package` (targets `package_city`, `package_platformer`) and a workflow
  preset that runs both.
- **Checks**: `package_layout` (no GPU) packages the sandbox and checks the layout: the
  marker, the manifest, no source-only files. `package_run` (label `gpu`) starts the
  packaged sandbox from another working directory with `--hidden --frames 30`, which fails
  if any asset is missing. CI uploads each platform's archive.

## Rejected

- **CPack.** It archives an `install()` tree well, but the parts that take the work (the
  bundle layout, rewriting install names, the ICD manifest, filtering assets) would be
  custom code inside it anyway, and its generators differ per platform.
- **Steam or itch.io uploads, installers (MSI, DMG, AppImage).** Distribution channels are
  the game's choice; a plain folder or archive feeds all of them.
- **Packing assets into one archive file.** Faster to open and harder to tamper with, but
  the asset manager reads loose files and streaming relies on that. Later, if load times
  on hard disks call for it.

## To verify

- The Vulkan loader from Homebrew finds the ICD inside the bundle when no system-wide
  MoltenVK is installed (check on a Mac without Homebrew, or with Homebrew's ICD folder
  renamed).
- The Linux package runs inside the Steam Linux Runtime (sniper) on a Steam Deck.
- Windows: the DLL list from `GET_RUNTIME_DEPENDENCIES` is complete for the MSVC runtime
  (default: rely on the installed Visual C++ redistributable and say so in the docs).
