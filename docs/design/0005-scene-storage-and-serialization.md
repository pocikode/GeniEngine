# 0005: Scene storage and serialization

Milestone: M3 · Status: Done · Last updated: 2026-11-10

## Problem

`Scene` owns root `GameObject`s, and each `GameObject` owns a
`std::vector<std::unique_ptr<Component>>`. That has three costs that M3 exposes:

- Systems that visit every component (lights, animation, later streaming and the
  editor) walk the scene tree and then a pointer list, so iteration is pointer chasing
  across allocations. There is no way to ask a scene for all cameras without a tree walk.
- Components cannot be created from a file: their type IDs are process-local and there
  are no property names, so save/load and the M4 inspector have nothing to work from.
- The only scene description is C++ code (`samples/city/main.cpp`), so nothing can be
  saved, diffed or edited.

**Done** means the city block loads from a text scene file, a scene saved from code
round-trips through the file, components expose reflected properties, and scene-wide
iteration over a component type is contiguous.

## Approach

**Keep `GameObject`/`Component`, add dense per-type indexes.** This is the ECS decision
the roadmap asks for: do not adopt an ECS in M3. The reference game needs stable object
identity and a hierarchy for the editor, glTF and animation; an archetype ECS would
rewrite all of that for a benefit that M2's profiling does not yet show. Instead,
`Scene` owns one `ComponentPool` per component type: a contiguous `Component*` array
over the components the `GameObject`s own. Component addresses stay stable (the public
API returns the pointer that was added, and tests rely on it), while systems iterate one
dense array instead of walking the tree and each object's vector. Iteration still
dereferences one pointer per component, so this removes the tree walk, not the pointer
chase. Each component stores its index in its pool, so removing one is O(1) (swap with
the last entry); unloading many objects at once does not degrade to O(n²). `GameObject` keeps
non-owning `Component*` pointers for `GetComponent` and update order.
`Scene::ForEachComponent<T>` and `Scene::CountComponents<T>` are the system-facing API;
lights use it, and later streaming and the editor will.

**Reflection (`scene/Reflection`).** A process-wide `TypeRegistry` maps a component type
name to a factory and a list of `PropertyDesc`. A property has a name, a value kind
(bool, int, float, vec2/3/4, quat, string, enum, asset) and get/set functions. Built-in
components register themselves in their `.cpp`; a game registers its own the same way.
Reflection is deliberately data-only: no runtime type IDs in files, no inheritance
metadata. That is enough for serialization now and for the inspector in M4.

**Scene format.** Text JSON with a format tag and a version:

```json
{
  "format": "geni.scene", "version": 1,
  "nodes": [
    { "name": "Ground", "parent": -1, "position": [0, 0, 0],
      "rotation": [0, 0, 0, 1], "scale": [1, 1, 1],
      "components": [
        { "type": "MeshComponent",
          "properties": { "mesh": "6f3a...", "material": "91c2..." } }
      ] }
  ],
  "mainCamera": 0
}
```

A flat node array with parent indices keeps the file diff-friendly: the writer
(`cooked::FormatSceneText`, shared by the cooker and `SceneSerializer::SaveToText`) puts
each node on one line, so adding a building is one added line, not a re-indented
subtree.

**Loading.** `SceneInstantiation` builds a document a slice at a time: `Step(n)` creates
up to `n` objects (pass 1), then up to `n` objects' components (pass 2), so a 10,000-node
scene can load over several frames. `SceneSerializer::Load` is the same thing in one
step. A malformed document (a wrong type anywhere makes nlohmann::json throw) fails the
load with an error instead of crashing; the objects it created are removed. The cooker writes the same format, so the city scene
is a cooked artifact rather than a separate schema. `SceneSerializer::Save` writes it
from a live scene through reflection, and `Load` instantiates it. Asset references are
IDs, never paths.

**Animation off the main thread.** `Scene::Update` runs animation before the object
traversal, in three steps over the dense `AnimationComponent` pool: advance every clock
on the main thread (cheap), sample every pose with one `ParallelFor` on the job system,
then write the transforms on the main thread. Skinned meshes updated later in the same
frame see this frame's pose. A component whose object is not in a scene still animates
itself in `Update`.

## Rejected

- **A full ECS now.** It would replace `GameObject`, glTF node mapping, hierarchy and
  every component's ownership. M2 has no profile showing component iteration as a
  bottleneck, and M4's inspector wants object identity. Dense per-type indexes get most
  of the cache benefit without the rewrite.
- **Pooling components by value.** It would make iteration truly contiguous, but adding
  a component would move it, invalidating the raw pointer the API just returned (and
  that `GetComponent` callers keep). The dense pointer index keeps the API stable; M5
  can revisit value pools if profiling justifies the break.
- **Binary scene files.** Faster to parse, but the roadmap asks for text and
  diff-friendly; scene files are small compared with meshes.
- **Automatic reflection (magic_enum, RTTR, a C++ parser).** A new dependency and a
  build step for a property list that is small and explicit. Hand-written descriptors
  also make the file schema stable when a field is renamed.
- **Per-component `Serialize`/`Deserialize` virtual methods.** That puts the file format
  in every component and makes it hard to serialize a component that is not loaded yet.
  Reflection keeps the format in one place.
- **Storing the ECS decision as "revisit later" without a default.** The roadmap asks
  for a decision; this page decides to keep the object model for M3 and revisit only if
  M5 streaming profiling shows pools are not enough.

## To verify

- **Pool growth.** The pools are plain vectors of pointers; re-measure with a streamed
  world in M5 and move to value pools if iteration shows up in profiles.
- **Editor undo (M4).** Reflection gives a property diff but no command history. Add
  transactions in M4 rather than complicating the descriptors now.
- **Cross-version files.** `version` is checked and rejected when unknown. Add migrations
  only when a format actually changes.
