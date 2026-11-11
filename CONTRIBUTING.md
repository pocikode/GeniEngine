# Contributing to GeniEngine

Thank you for helping. GeniEngine is built in the open by a solo maintainer with AI
agents writing much of the code, so the process is deliberately written down and
checked by machines. Small, focused changes with tests are the easiest to accept.

## Before you start

- **Bugs**: open an issue with what you did, what you expected and what happened, the
  platform and GPU, and the log (or crash report, `geni_crash_*.log`).
- **Features**: open an issue first. The [roadmap](ROADMAP.md) decides what comes next
  (open-world 3D first; a feature the reference game does not use is postponed), so
  agree on the change before writing it.
- **Major systems** need a design page first (ROADMAP rule 3): copy the template in
  [docs/design/README.md](docs/design/README.md), state the problem, the approach and
  what you rejected, and get it reviewed before the code.

## Build and test

```sh
cmake --workflow --preset dev         # Debug, validation layers on, all tests
cmake --workflow --preset dev-no-gpu  # without a GPU: skips window and Vulkan tests
```

Nothing merges without (ROADMAP rule 4):

1. **Unit tests** for new behaviour (`tests/`, doctest; list new files in
   `tests/CMakeLists.txt`). Test suites whose name starts with `gpu` need a window and a
   Vulkan device; keep everything else GPU-free.
2. **Screenshot comparisons** still matching (`ctest -R screenshot`). After an intended
   visual change, regenerate with `GENI_UPDATE_REFERENCES=1 ctest --preset dev -R screenshot`
   and look at every new image before committing it.
3. **Performance budgets** still met (`benchmark_city`, `benchmark_world`), and the
   reference game's scripted run passing (`play_city`).
4. **Zero Vulkan validation errors**: the debug tests fail on any.

CI builds and tests every pushed branch on macOS, Windows and Linux
(`.github/workflows/ci.yml`).

## Code

- C++17, formatted with the repository's `.clang-format` (run `clang-format -i` on the
  files you touched).
- Engine headers in `include/<area>/`, sources in `src/<area>/`. Third-party headers
  stay private to the area that wraps them (Jolt in `src/physics/`, Recast in `src/nav/`,
  Lua in `src/script/`, miniaudio in `src/audio/`); public headers use engine types only.
- A new component: derive from `Component`, use the `COMPONENT(Name)` macro, register
  its properties with `RegisterComponentType<T>` in its `.cpp`, and add a `Link...()`
  call to `include/scene/BuiltinComponents.h`. It then appears in the editor's inspector,
  is saved in scene files and is scriptable from Lua.
- Comments say why, not what. Names are full words.
- Dependencies: prefer an existing, maintained library for a solved problem (rule 6),
  added as a pinned git submodule in `thirdparty/` with its licence compatible with MIT.

## Commits and pull requests

- One topic per pull request. The description says what changed, why, and how it was
  tested (which tests, which platforms and GPUs).
- Commit messages: a short summary line (`feat(audio): ...`, `fix(nav): ...`), a blank
  line, then the reasons.
- Update the relevant design page when behaviour changes, and the README or the
  [getting-started guide](docs/getting-started.md) when usage changes.

## Licence

GeniEngine is MIT-licensed ([LICENSE](LICENSE)). By contributing you agree that your
contribution is licensed under the same terms.
