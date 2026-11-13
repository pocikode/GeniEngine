# 0028: Animation graph editor

Milestone: After v0.1 (visual tools) · Status: Accepted · Last updated: 2026-11-13

## Problem

Animation graphs (0011) are `.animgraph` JSON written by hand: states with a clip or a 1D
blend, transitions with conditions, durations and exit times. Without a picture of the
states and arrows, it is easy to leave a state unreachable or a transition pointing at a
misspelled state, and the error only shows when the game runs.

**Done** means the editor opens a `.animgraph` from the Assets browser in an Animation Graph
panel: states as boxes on a canvas (dragged, positions saved in the file), transitions as
arrows, the initial state marked; selecting a state or transition edits it in the panel
(clip or blend, loop, speed; conditions, duration, exit time); states and transitions are
added and deleted; every change is checked by the runtime parser and errors are shown; the
file saves, reverts and undoes. Unit tests cover the editing logic.

## Approach

- **`editor/core/AnimGraphEditing`** (no window): `AnimGraphDocument` holds the file as
  JSON (so fields the editor does not know about survive), with operations that keep it
  consistent: add, rename and remove states (renaming and removal update transitions and
  the initial state), set a state's clip or blend, loop and speed, add and remove
  transitions, set their duration, exit time and conditions (from text like
  `grounded == true; speed > 0.5; jump`), move nodes. Node positions live under
  `"editor": {"positions": {state: [x, y]}}`, which the runtime ignores; states without
  one are laid out in a row. `Validate` runs `AnimationGraph::Parse` and returns its error.
- **Animation Graph panel** (`EditorAnimGraph.cpp`, Window menu): a canvas drawn with
  ImGui draw lists (no node-editor library is vendored): boxes, arrows with arrowheads
  (from "Any" for transitions with `from: "any"`), pan with the middle or right mouse.
  The right column edits the selection. Save, Revert, Undo (a history of the document's
  JSON); the parser's error is shown in red and Save is refused while there is one.

## Rejected

- **A node-editor library (imnodes, imgui-node-editor).** Another dependency for a graph of
  a few states; boxes and arrows are a few hundred lines.
- **Editing the parsed `AnimationGraph`.** It drops unknown fields and has no writer;
  the JSON is the source of truth.
- **Live preview on a character in the panel.** Needs an offscreen viewport; play mode
  shows the result.

## To verify

- Readability of graphs with many states. Default: dragging and panning are enough for
  the sample graphs.
