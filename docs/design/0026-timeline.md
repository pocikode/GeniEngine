# 0026: Timeline for cutscenes

Milestone: After v0.1 (visual tools) · Status: Done · Last updated: 2026-11-13

## Problem

Scripted sequences (a camera flying over the city before play, a door opening as a car
arrives, a sound and a subtitle at the same moment) are written as Lua timers today: the
timing lives in code, cannot be previewed in the editor, and changes need a programmer.
Engines ship a timeline for this: tracks of keys over time that move objects, cut between
cameras and fire events, scrubbed and keyed in the editor and played at run time.

**Done** means a `TimelineComponent` that plays transform tracks (smooth position,
rotation and scale keys for named objects), camera cuts, sound cues and named events;
Lua plays, pauses, seeks and reads its events; the editor has a Timeline panel to scrub,
preview, add a track for the selected object, key its transform at the playhead and delete
or move keys, all undoable; the scripted editor session and unit tests cover it.

## Approach

- **Data** (`scene/Timeline.h`, pure, used by the engine and the editor): `TimelineData`
  with transform tracks (target object name, keys of time, position, rotation, scale),
  camera cuts (time, camera object name), sound cues (time, sound path, volume) and events
  (time, name). Stored as JSON in the component's reflected `tracks` property, so scene
  files, prefabs, undo and the inspector need nothing new. Sampling: Catmull-Rom through
  the positions (smooth through every key), slerp between rotations, linear scale; before
  the first key and after the last the end keys hold.
- **`TimelineComponent`**: properties `tracks`, `time`, `duration` (0: the last key),
  `loop`, `autoplay`, `speed`. `Update` advances the time while playing and applies every
  track at it; cues and events between the previous and the current time fire once (sounds
  play through `AudioSystem`, cuts set the scene's main camera, events queue for Lua).
  `EditUpdate` applies transform tracks at `time` only, so scrubbing previews in the editor
  without sounds, cuts or events. Targets are found by name below the timeline's object
  first, then in the scene, and remembered by ID.
- **Lua**: `play()`, `pause()`, `stop()` (back to 0), `seek(t)`, `playing()`, `time()`,
  `events()` on the component.
- **Editor**: `editor/core/TimelineEditing` holds the edits as pure functions on
  `TimelineData` (add a track, set or move or remove a key, which keep keys sorted); the
  Timeline panel (Window menu) shows the timeline of the selected object (or of its
  nearest ancestor that has one): a ruler with the playhead to scrub, play/pause preview
  (advances `time` with the frame time while editing), one row per track with its keys
  (click to jump, drag to move, right-click to delete), "Add track" for the selected
  object and "Key" at the playhead. Every change goes through `EditorDocument::
  SetProperty` on `tracks`, so it undoes. Script commands: `timeline-key OBJECT TIME` and
  `expect keys OBJECT N`.

## Rejected

- **Curves editor with tangents.** Catmull-Rom through keys gives smooth motion with no
  tangent handles to learn; per-key easing is a later addition.
- **Animating arbitrary component properties.** Transform, cameras, sounds and events
  cover cutscenes; property tracks need typed interpolation per property type. Later.
- **Separate timeline asset files.** A timeline on an object in a scene or prefab is
  versioned, instanced and undone like everything else.

## To verify

- The JSON size of long timelines in scene files. Default: accept; a timeline of a few
  hundred keys is a few tens of kilobytes.
