# 0027: Material editor

Milestone: After v0.1 (visual tools) · Status: Accepted · Last updated: 2026-11-13

## Problem

Materials come from two places that the editor cannot change. glTF materials are cooked
from the model, so an edit would be lost at the next cook; `.mat` files (a shader plus
parameters, `Material::Load`) are only loaded by code and cannot be put on a mesh in a
scene, because the cooker does not cook them and `MeshComponent::material` is a cooked
asset ID. Tweaking a colour or roughness means editing JSON by hand and restarting.

**Done** means `.mat` files are cooked assets that meshes can use; the editor has a
Material panel that edits a `.mat` with live preview in the viewport and saves it (the
project re-cooks and hot-reloads it); a mesh with a glTF material gets an editable copy
in one click, assigned undoably; tests cover the cook and the editing logic.

## Approach

- **Cooking `.mat`**: the cooker reads `{"shader": "builtin:default"` (or `{"vertex":
  path, "fragment": path}` naming one `.slang`), `"params": {"float": {...}, "float2":
  {...}, "float4": {...}, "textures": [{"name", "path"}]}}`, cooks each texture image
  (normal maps by their `.meta` usage or a name containing "normal") and writes a cooked
  material listed as `material:<path>`. `Material::Load` keeps reading the same files at
  run time for code that loads them by path.
- **Editing logic** (`editor/core/MaterialEditing`, no window): `MaterialSource` loads
  and saves the JSON, lists parameters (for the built-in shader every standard parameter,
  with glTF defaults, even when the file leaves it out), sets values and textures, and
  builds a source from a cooked glTF material (`FromCooked`, with each texture's source
  image found in the manifest).
- **Material panel** (Window menu): opens from a `.mat` double-clicked in the Assets
  browser or from "Edit" next to a mesh's material in the inspector. Colours (`...Factor`
  `float4`s) get colour pickers, scalars sliders, textures a combo of the project's
  images. Every change is applied at once to the loaded `Material` (`SetParam`), so the
  viewport shows it; "Save" writes the file and the project's watcher re-cooks and
  reloads it; "Revert" reloads the file. The panel keeps its own undo history of the
  file's JSON (Ctrl+Z while it has focus).
- **Editable copy**: for a mesh whose material is cooked from a glTF, "Make editable"
  writes `materials/<object>.mat` from `FromCooked`, waits for the project's cook, and
  sets the `MeshComponent`'s `material` to the new asset through `EditorDocument::
  SetProperty` (so the assignment undoes; the file stays).

## Rejected

- **Editing cooked glTF materials in place.** The next cook would overwrite the change.
- **Material instances (a parent material plus overrides).** Useful for many variants of
  one look; files first, inheritance later.
- **A node-based shader graph.** A separate, large tool; parameters of existing shaders
  cover the common tweaks.

## To verify

- Hot-reload latency between Save and the viewport (one cook of one file). Default: accept;
  the live preview already shows the edit before saving.
