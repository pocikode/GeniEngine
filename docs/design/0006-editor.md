# 0006: Editor v1

Milestone: M4 · Status: Done · Last updated: 2026-11-11

## Problem

A scene can be saved and loaded (0005) but only code or the cooker can make one. The city
block's layout lives in a generated glTF (`tools/gen_assets.py`), nothing can be moved
without regenerating it, a shader or texture edit needs a rebuild and a restart, and there
is no way to reuse a group of objects. Nobody but the author can build anything.

**Done** means `geni_editor` opens a project's assets folder, the city block is a scene
file saved by the editor (`samples/city/assets/scenes/city.gscene`) that the sample loads
without any scene setup in C++, objects can be picked, moved with gizmos, inspected,
undone and redone, the scene can be played and stopped in place, shader and asset edits
show up without a restart, and a subtree can be saved as a prefab and instanced.

## Approach

**Where the code lives.** Anything a game also needs is in the engine: object IDs, edit-mode
update, prefabs, picking, the scene viewport, reloading, the file watcher. Editor-only
logic is `editor/` in two targets: `geni_editor_core` (document, selection, commands,
camera, gizmo maths, project; no ImGui, no GPU, unit-tested) and `geni_editor` (the ImGui
application). The editor is an ordinary `Application`.

**Viewport.** `Renderer::SetSceneViewport` gives the scene a pixel rectangle of the
backbuffer: the HDR targets take that size and the tonemap pass draws into the rectangle.
The editor uses an ImGui dock space with a pass-through central node and hands its
rectangle to the renderer, so panels dock around the scene. `Engine::SetViewOverride`
replaces the scene's main camera with the editor camera while editing.

**Edit mode.** `Scene::Update(dt, SceneUpdateMode::Edit)` calls `Component::EditUpdate`
instead of `Update`: mesh components submit their draws, nothing else runs.
`Engine::SetPhysicsPaused` stops the fixed-step physics. So an edited scene renders but
does not simulate.

**Identity and structure.** Every `GameObject` gets a 64-bit ID, unique in its scene
(`Scene::FindObjectByID`). IDs are not written to scene files; they exist so selection and
undo commands can name an object that was deleted and restored. `Scene` gains immediate
`DestroyObject`, `SetParent` with a sibling index, and `GameObject::RemoveComponent`.

**Undo and redo.** A command stack (`editor/CommandStack`). Commands hold object IDs and
JSON, never pointers: `NodeStateCommand` (name, active, transform), `PropertyCommand`
(one reflected property), `AddComponentCommand` / `RemoveComponentCommand`,
`ReparentCommand`, `CreateObjectsCommand` / `DeleteObjectsCommand` (a subtree snapshot
with IDs, so a restored object keeps its identity), `SetMainCameraCommand`, and a
composite. A drag (gizmo or inspector field) is one command, pushed when the drag ends.
The document is dirty when the stack is not at its saved position.

**Subtrees and prefabs.** `SceneSerializer::SaveSubtree` / `InstantiateSubtree` write and
read part of a scene in the scene format; duplicate, paste, undo of delete and prefabs all
use them. A prefab is a `.gprefab` file: the scene format with `"format": "geni.prefab"`
and a single root. A scene node with `"prefab": "<asset id>"` is an instance: on load the
prefab root's components and its children are created under the node and flagged
`IsFromPrefab`, and they are not written back when the scene is saved, so editing the
prefab file changes every instance. The instance keeps its own name, transform and active
flag; nothing else can be overridden in v1. Prefabs nest (depth-limited). The editor can
create a prefab from a selection, instantiate one, unpack an instance into plain objects
and open the prefab itself for editing.

**Scenes and prefabs as assets.** The cooker lists source `.gscene` and `.gprefab` files
in the manifest (`scene:<root name>`, `prefab:<root name>`), with the source-relative path
as the cooked path: text scenes are already the runtime format and the build copies them.
The editor reads and writes them in the source folder; `AssetManager::RegisterAsset` adds
a file the editor has just created so it is usable before the next cook.

**Picking and gizmos.** `scene/Picking`: a ray from a viewport pixel, tested against each
mesh's local bounding box, oriented with the object; lights and cameras, which have no
mesh, are picked by a small sphere where their marker is drawn. An empty object has nothing
to click and is selected in the hierarchy. The nearest hit wins.
It is box-accurate, not triangle-accurate, because the runtime does not keep CPU vertex
data. Gizmos are the editor's own: translate (axes and planes), rotate (rings) and scale
(axes and uniform), in world or local space, with snapping. The maths (`editor/Gizmo`)
works on rays and is unit-tested; drawing is ImGui draw lists over the viewport, as are
the selection boxes, light and camera markers and the ground grid.

**Play in editor.** Play serializes the scene to memory, reloads it from that snapshot (so
every component starts from the edited state) and switches to `SceneUpdateMode::Play`
with physics running and the scene's own camera. Stop reloads the snapshot again. The
undo stack is kept; commands refer to IDs, which a snapshot preserves.

**Hot reload.** `io/FileWatcher` polls a folder's modification times. In the editor a
changed `.slang` is recompiled with the pinned `slangc` (its path and the module path are
baked in at configure time) and `Shader::ReloadChanged` swaps the modules of every shader
loaded from that file, keeping the `Shader` object so materials keep pointing at it; a
shader that fails to compile or validate keeps its old modules. A changed glTF, image or
`.meta` re-runs `geni_asset_cooker` as a child process on a job thread, then
`AssetManager::ReloadChanged` reloads every loaded asset whose cooked file changed and
swaps the new object into the same slot; components reach assets through their handles, so
they see the new object the next frame. A changed prefab re-instantiates its instances.

**The city block.** `samples/city/assets/models/city_kit.gltf` now holds only the nine
meshes and materials. The layout is `scenes/city.gscene`, written by the editor from the
M3 layout (`geni_editor --import ... --save ...`), and edited in the editor from here on.
`Edit > Duplicate as grid` is the tool for laying out such a block by hand.

**Scripted sessions.** `geni_editor --run "<command>"` (or `--script FILE`) runs editor
commands one per frame without a window, through the same `EditorDocument` methods the
menus call, with `expect` commands that fail the run. CI uses it, the city scene was
migrated with it, and it is a way to make a batch edit from a shell.

**Checks.** Unit tests for the engine additions (`tests/test_scene_editing.cpp`,
`tests/test_hot_reload.cpp`) and for `geni_editor_core` (`tests/test_editor.cpp`: every
command undoes and redoes, gizmo drags, prefab round trip, play/stop restores the scene,
a shader compiles and a broken one keeps the old SPIR-V). `editor_smoke` runs the real
editor hidden through a scripted session on a copy of the city project, which the editor
has to cook itself; `screenshot_editor` compares the editor's window with a reference;
the city screenshot and benchmark keep guarding the scene the editor saved.

## Rejected

- **Scene in an ImGui image (render to texture).** Needs sampled-texture registration in
  the ImGui backend and a second tonemap target. The viewport rectangle gets the same
  result with one renderer setting; revisit when the editor needs two views.
- **ImGuizmo.** It is the usual answer, but it reaches into ImGui internals and tracks
  ImGui releases on its own schedule (the engine pins a docking tag), and its drag logic
  cannot be tested headless. The gizmo here is about 400 lines of ray maths with tests.
- **Whole-scene snapshots for undo.** Simple and always correct, but serializing 10,000
  nodes per edit is tens of milliseconds and megabytes per step. Snapshots are used only
  where they are the natural unit (delete, play/stop).
- **Pointers or scene-file indices in commands.** A deleted object's pointer is gone and
  indices shift with every structural edit; IDs survive both.
- **Persisting object IDs in scene files.** Would allow per-object prefab overrides, but
  adds a field to every line of every scene. Do it when overrides are designed.
- **Prefab overrides beyond the root.** Property overrides need stable identity inside a
  prefab and a diff format; v1 ships whole-prefab instances and "unpack".
- **Triangle-accurate or GPU ID-buffer picking.** Needs CPU mesh data or another render
  pass and a readback. Box picking is exact for the city block; revisit with real art.
- **Linking the cooker into the editor.** `geni_cook` compiles engine sources of its own
  and C++20 encoders; a child process keeps the cooker the only place those are linked and
  keeps a cooker crash out of the editor.
- **OS file notifications (FSEvents, inotify, ReadDirectoryChangesW).** Three backends for
  a folder of a few hundred files; polling twice a second is not measurable.

## To verify

- **Windows and Linux.** Built and tested on macOS (MoltenVK) only so far; CI has to show
  the editor, its tests and `screenshot_editor` (ImGui text rasterised on another
  platform) green on the other two.
- **In-editor shader reload on a visible material.** The reload path is tested at engine
  level and the compile step against the real `slangc`, but cooked materials only use the
  built-in shaders today, so no scene in the repo shows a custom shader changing live.
  Check it with the first sample that cooks a material with its own shader.
- **A failed asset reload is retried only when its file changes again**, and assets
  loaded through `Material::Load` (`.mat` files) are not watched.
- **Cooker as a child process on Windows.** `std::system` quoting is handled, but only
  macOS and Linux have been exercised; check on Windows CI.
- **Viewport resize cost.** Dragging a dock splitter recreates the HDR targets every
  frame. Destruction is deferred, so it is safe; debounce it if it shows up in a profile.
- **Scene size.** The hierarchy panel uses a list clipper over visible rows; re-measure at
  M5 world sizes, where the editor edits one region at a time anyway.
- **Prefab loading is synchronous** (a small JSON file on the first instance). Make it
  asynchronous with streaming in M5 if prefabs get large.
