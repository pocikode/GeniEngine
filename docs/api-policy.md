# API stability policy

Last updated: 2026-11-12 · Applies from v0.2

This page says what a game built on GeniEngine can rely on from one release to the next.
It is part of ROADMAP M7 (Desktop 1.0).

## Versions

GeniEngine uses [semantic versioning](https://semver.org): `MAJOR.MINOR.PATCH`. The version
is in `include/core/Version.h` (`GENI_VERSION_STRING`, `GENI_VERSION_NUMBER`), and CMake
reads it from there. Between releases the suffix is `-dev`.

| Release | Before 1.0 (0.x) | From 1.0 |
|---|---|---|
| Patch (0.2.0 → 0.2.1) | Bug fixes only. Never breaks a game that built and ran on the previous patch. | Same. |
| Minor (0.2 → 0.3) | May break the public API, but only as described under "Breaking changes" below. | Adds features; never breaks the public API. Deprecates instead. |
| Major (1.x → 2.0) | — | May remove what was deprecated in a released 1.x version. |

Each release is a git tag `vMAJOR.MINOR.PATCH` with a `CHANGELOG.md` section.

## What counts as public API

These are covered by this policy:

- **C++ headers in `include/`** that `include/geni.h` reaches, in namespace `Geni` and
  `Geni::rhi`: class names, public member functions and their meaning, enums and their
  values, struct fields, and the macros documented in those headers.
- **The Lua API** in `docs/scripting.md`.
- **File formats a game author writes or edits**: `.gscene`, `.gprefab` and `.gworld`
  (the scene format, docs/design/0005), `.mat` materials, `input/actions.json`, `.meta`
  files, animation graph JSON, and the reflected component property names stored in them.
  A newer engine reads every older file of these formats. It may write a newer one.
- **Shader contracts** for game shaders: the `geni.*` Slang modules and the bindings and
  vertex locations in `render/ShaderInterface.h`.
- **CMake functions** games call: `geni_add_sample`, `geni_add_package`,
  `geni_compile_shaders`, `geni_cook_assets`, and the `GENI_*` options in `CMakeLists.txt`.
- **Command-line flags** of `geni_asset_cooker` and `geni_editor`.

These are **not** public, and may change in any release:

- Anything in `src/`, in a `Detail` namespace, or named `Internal`; anything in a header
  under `src/` (for example `src/physics/JoltInternal.h`).
- Cooked files (`cooked/`, `cooked_manifest.json`, `.gmesh`, `.ganim`, cooked `.ktx2`
  layout). They are rebuilt from the sources; re-cook after upgrading.
- The editor's own files (`~/.geni/`, `.geni/editor_state.json`) and its UI layout.
- Debug overlay contents, log text, `RenderStats` fields added for diagnostics, and
  sample programs (`samples/`), which show how to use the engine but are not API.
- Behaviour that the documentation calls a known limit or a bug.

## Breaking changes before 1.0

A 0.x minor release may break the public API when keeping it would block the roadmap. It
must then:

1. List every break under a **Breaking** heading in `CHANGELOG.md`, with what to change in
   a game (the old call, the new call).
2. Prefer a deprecation for one minor release where the old form can still work.
3. Keep old file formats loading. A format change is never a break for files: the loader
   reads the old version (scene files have a `version` field for that).

## Deprecation

Public API that is going away is marked first:

```cpp
GENI_DEPRECATED("use SetBackground(false, color)") void SetClearColor(const glm::vec3 &color);
```

- `GENI_DEPRECATED(message)` (in `core/Version.h`) makes the compiler warn at every use; the
  message names the replacement.
- Deprecated API keeps working, and is tested, until it is removed.
- Before 1.0 it is removed no earlier than the next minor release. From 1.0, no earlier
  than the next major release.
- Deprecations are listed under **Deprecated** in `CHANGELOG.md` in the release that adds
  them, and again under **Removed** in the release that removes them.
- A reflected component property that is renamed keeps reading its old name for the same
  period, so saved scenes load.

## The changelog

`CHANGELOG.md` has one section per release, newest first, and an **Unreleased** section at
the top that every change adds to. Headings, in this order and only when not empty:
**Breaking**, **Added**, **Changed**, **Deprecated**, **Removed**, **Fixed**, **Known limits**.
Releasing renames "Unreleased" to the version and date.

## Checking it

- Reviewers check a change against this page: a change to `include/` either keeps the API,
  or adds a CHANGELOG entry under Breaking or Deprecated.
- The scene, prefab and world fixtures in `tests/assets` are files from earlier versions;
  the tests that load them stay, so old files keep loading.
