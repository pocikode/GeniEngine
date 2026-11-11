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
