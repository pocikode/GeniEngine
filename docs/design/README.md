# Design pages

One page per major system, written **before** the system is built (ROADMAP rule 3). Agents implement against the page; the review checks the code against the page.

## Index

| # | Page | Milestone | Status |
|---|---|---|---|
| 0001 | [Rendering interface and Vulkan backend](0001-rhi-and-vulkan-backend.md) | M1 | Accepted |
| 0002 | [Renderer v1](0002-renderer-v1.md) | M2 | Accepted |
| 0003 | [Slang shaders](0003-slang-shaders.md) | before M3 | Done |
| 0004 | [Jobs, assets and the cooker](0004-jobs-and-assets.md) | M3 | Done |
| 0005 | [Scene storage and serialization](0005-scene-storage-and-serialization.md) | M3 | Done |
| 0006 | [Editor v1](0006-editor.md) | M4 | Done |
| 0007 | [World streaming](0007-world-streaming.md) | M5 | Done |
| 0008 | [Physics on Jolt](0008-physics-on-jolt.md) | M5 | Done |
| 0009 | [Input actions and gamepads](0009-input-actions.md) | M6 | Done |
| 0010 | [Characters, vehicles and the third-person camera](0010-characters-vehicles-cameras.md) | M6 | Done |
| 0011 | [Animation blending and state machines](0011-animation-graph.md) | M6 | Done |
| 0012 | [Audio](0012-audio.md) | M6 | Done |
| 0013 | [Lua scripting](0013-lua-scripting.md) | M6 | Done |
| 0014 | [Navigation meshes and AI agents](0014-navigation.md) | M6 | Done |
| 0015 | [Game UI](0015-game-ui.md) | M6 | Done |
| 0016 | [Packaging a game build](0016-packaging.md) | M7 | Accepted |
| 0017 | [2D sprites, tilemaps and physics](0017-2d.md) | M7 | Done |
| 0018 | [Time of day](0018-time-of-day.md) | After v0.1 | Done |
| 0019 | [Particles](0019-particles.md) | After v0.1 | Done |
| 0020 | [Weather](0020-weather.md) | After v0.1 | Done |
| 0021 | [Sprite atlases, Tiled maps and lit sprites](0021-2d-atlases-tiled-lit-sprites.md) | After v0.1 | Done |
| 0022 | [Terrain](0022-terrain.md) | After v0.1 | Done |
| 0023 | [Vegetation](0023-vegetation.md) | After v0.1 | Done |
| 0024 | [Missions and saved games](0024-missions-and-saves.md) | After v0.1 | Done |
| 0025 | [Traffic and pedestrians](0025-traffic-and-pedestrians.md) | After v0.1 | Done |

Status is one of: **Proposed** (written, not yet started), **Accepted** (work started; changes to the page need a reason), **Done** (shipped; the page describes what exists), **Superseded** (replaced; link to the replacement).

## Writing a page

Name the file `NNNN-short-name.md` with the next free number and add it to the index. Keep it to about one page; if it needs more, the system is probably two systems.

Use these sections:

```markdown
# NNNN: Title

Milestone: Mx · Status: Proposed · Last updated: YYYY-MM-DD

## Problem
What is wrong or missing today, with file names. What "done" looks like.

## Approach
What will be built: the pieces, how they connect, the order of work.

## Rejected
Each alternative that was seriously considered and the reason it lost.

## To verify
Facts the approach depends on that have not been checked yet, and when to check them.
```

Write decisions, not options. Where something is still open, put it under "To verify" with the default that applies if nobody decides.
