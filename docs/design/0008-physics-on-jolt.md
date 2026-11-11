# 0008: Physics on Jolt

Milestone: M5 · Status: Done · Last updated: 2026-11-11

## Problem

`physics/` wraps Bullet 2 (`btDiscreteDynamicsWorld`). M5 streams collision with the
world's cells, and M6 needs characters and vehicles. Bullet is the wrong base for both:

- Adding or removing thousands of static bodies when a cell loads goes one at a time
  through a dynamic AABB tree; there is no batch insert.
- Positions are `float`; in a 4 km world either everything rebases (thousands of bodies
  moved every kilometre) or precision drops.
- Its character controller and vehicle are dated and largely unmaintained, which is what
  M6 needs most.
- Single-threaded stepping.

**Done** means Bullet is gone from the repository, the public `physics/` API (`RigidBody`,
`Collider`, `PhysicsComponent`, `PhysicsManager::Step`) keeps its shape, the existing tests
and the sandbox's falling crates work on Jolt, and cells add and remove their collision in
batches without a hitch.

## Approach

Jolt Physics (submodule `thirdparty/JoltPhysics`, v5.5.0) built from its `Build/` CMake
project, linked `PRIVATE` like Bullet was. No Jolt type appears in a public header:
`RigidBody` keeps a 32-bit body ID, `Collider` describes its shape and builds the Jolt
shape inside `src/physics/`.

- **Double precision** (`DOUBLE_PRECISION=ON`). Body positions are world positions in
  doubles: `PhysicsComponent` adds the scene's origin (docs/design/0007) when it writes a
  body and subtracts it when it reads one back, so rebasing never touches the physics
  world.
- **Two object layers** (static, moving) and two broad-phase layers, the minimal Jolt
  setup; static bodies never test against each other.
- **Batched adds and removes.** `PhysicsManager::AddRigidBody` creates the body at once (so
  its transform can be read and written) but queues the broad-phase insert;
  `RemoveRigidBody` queues the removal and destruction. `Step` flushes both queues first
  with `AddBodiesPrepare`/`AddBodiesFinalize` and `RemoveBodies`/`DestroyBodies`, so a cell
  of a thousand buildings is one insert.
- **Threads.** Jolt's `JobSystemThreadPool` with a small worker count
  (`max(1, hardware threads / 4)`); the engine's own job system idles while physics steps.
- **Scale.** A collider's size is in the object's local space: the world scale is applied
  when the shape is built, so a unit cube scaled into a building gets a building-sized box.
- **Same simulation contract.** One `Update` of exactly the fixed step, one collision step,
  gravity −9.81 m/s². Only dynamic bodies write back to their objects.

## Rejected

- **Keep Bullet and rebase it.** Moving every body every kilometre is the hitch rebasing
  is meant to avoid, and it leaves M6 on Bullet's character and vehicle code.
- **PhysX 5.** Capable and open source, but a much larger build (and GPU parts the engine
  will not use), and its CMake integration is heavier than Jolt's single library.
- **Single precision Jolt with rebasing.** Works, but costs a broad-phase update for every
  body on each rebase. Double precision costs a little per step and nothing on rebase.
- **Running Jolt's jobs on the engine's `JobSystem`.** The right end state (one pool), but
  Jolt's job interface needs barriers and dependencies the engine's job system does not
  have yet. Revisit when the job system gets a task graph.

## To verify

- **Step cost with streaming.** Measure `PhysicsManager::Step` with the world's static
  bodies loaded around a moving camera on the Steam Deck.
- **Determinism.** Jolt is deterministic for the same inputs and order. The fixed-step
  tests check that frame rate does not change results; cross-platform determinism
  (`CROSS_PLATFORM_DETERMINISTIC`) is off until networking needs it.
- **Windows build.** Jolt's CMake defaults to the static MSVC runtime; the engine turns
  that off. Check on Windows CI.
