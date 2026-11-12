# 0011: Animation blending and state machines

Milestone: M6 · Status: Done · Last updated: 2026-11-11

## Problem

`AnimationComponent` plays one clip at a time and switches instantly. A walking character
needs idle, walk and run blended by speed, a jump that starts on a trigger and returns to
locomotion on landing, and smooth transitions between all of them, driven by gameplay
values rather than by code that calls `Play`.

**Done** means a graph file describes states, blend spaces and transitions; an
`AnimatorComponent` evaluates it each frame with cross-fades, sampling on the job system
like `AnimationComponent`; gameplay (C++ or Lua) only sets parameters; and clip events
(footsteps) reach the game.

## Approach

**Graph file** (`.animgraph`, JSON, read from the assets folder like a `.mat`):

```json
{ "parameters": { "speed": 0.0, "grounded": true, "jump": "trigger" },
  "initial": "move",
  "states": {
    "move": { "blend": { "parameter": "speed", "clips": [["Idle", 0], ["Walk", 1.6], ["Run", 5.5]] } },
    "jump": { "clip": "Jump", "loop": false, "speed": 1.2 }
  },
  "transitions": [
    { "from": "move", "to": "jump", "when": [["jump"]], "duration": 0.1 },
    { "from": "jump", "to": "move", "when": [["grounded", "==", true]], "exitTime": 0.5, "duration": 0.2 },
    { "from": "*", "to": "drive", "when": [["driving", "==", true]], "duration": 0.25 }
  ],
  "events": [ { "clip": "Walk", "time": 0.0, "name": "footstep" } ] }
```

Parameters are float, bool or trigger (a bool that a transition consumes). A condition is
`[name]` (trigger or true bool) or `[name, op, value]` with `== != < <= > >=`. A
transition fires when all its conditions hold and, if it has `exitTime`, the source
state's normalised time has passed it. `"*"` is any state. Transitions are checked in file
order; the first that fires wins.

**States** are a single clip or a 1D blend space: clips at parameter positions, the two
neighbours of the current value weighted linearly. Clips in a blend space share a
normalised phase so feet stay in step when walk blends into run; the phase advances at
the weighted rate of the two clips.

**Evaluation** (`animation/AnimationGraph.h`, no scene dependency, unit-tested):
`AnimationGraph` is the parsed, immutable definition; `AnimationGraphInstance` holds
parameters, current state, phase and an optional transition in progress. A pose is one
translation, rotation and scale per bound joint; clips are bound to joints once (by track
target name), so sampling is array indexing. During a transition the source and target
poses are sampled and mixed by `elapsed / duration` (rotations with nlerp along the
shortest arc). The source keeps playing during the fade.

**`AnimatorComponent`** has an `animation` asset (the clips, as `AnimationComponent`) and a
`graph` path. `Scene::UpdateAnimation` gives it the same three steps as
`AnimationComponent`: advance clocks and transitions on the main thread, sample on
workers, apply on the main thread. Events whose time was crossed this frame by a clip
with weight above one half are queued and read with `ConsumeEvents`.

**Joint transforms** are written to the scene objects that `SkinnedMeshComponent`'s
skeleton already reads, so skinning is unchanged.

## Rejected

- **Ozz-animation.** A fine runtime, but it brings its own skeleton and offline format,
  and our joints are scene objects (0005). Revisit if skeletons grow past ~100 joints or
  thousands of animated characters are needed.
- **2D blend spaces, layers and masks.** Not used by the reference game (rule 2). The
  instance stores weights per clip, so layers can be added without a format change.
- **Root motion.** Characters are moved by the controller (0010); clips are in place.

## To verify

- Sampling cost with fifty pedestrians in view on the Steam Deck.
