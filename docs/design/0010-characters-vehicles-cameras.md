# 0010: Characters, vehicles and the third-person camera

Milestone: M6 · Status: Done · Last updated: 2026-11-11

## Problem

The only ways to move today are `PlayerControllerComponent` (moves a transform through
walls) and the editor-style fly and orbit cameras. Physics (0008) has rigid bodies only:
no character that walks up steps and stands on slopes, no vehicle, no ray casts, no
kinematic bodies that move. The reference game needs to walk around the streamed city,
get into a car, drive it and get out, with a camera that follows either and does not go
through buildings.

**Done** means a capsule character walks, runs, jumps and climbs kerbs on the world's
streamed collision; a four-wheeled car accelerates, steers, brakes and collides with the
city; the camera orbits behind the active one and is pulled in by walls; and all three are
components with reflected properties, so they appear in the editor and in Lua.

## Approach

All physics stays behind `src/physics/` (0008); new public types hold IDs and plain data.

**Fixed-step components.** `Component` gains `FixedUpdate(float)` with an opt-in
`HasFixedUpdate()`. `Engine::Simulate` calls `Scene::FixedUpdate` in each fixed step
*before* `PhysicsManager::Step` (unless physics is paused, as in the editor), so input
turned into forces or velocities this step is simulated this step. Only component types
that opt in are visited, through the per-type pools (0005).

**Interpolation.** Characters and vehicles keep the previous and current simulated
transform and set their object's transform in `Update` from
`FixedTimestep::GetAlpha()`, so a 144 Hz display does not show 60 Hz steps.

**Character: `physics/CharacterController` on Jolt's `CharacterVirtual`.** A capsule moved
by collision queries rather than simulated, with `ExtendedUpdate` for stair stepping
(0.4 m) and sticking to the ground going down slopes. It has an inner kinematic body on the
moving layer so cars and crates hit it, and it pushes dynamic bodies. Positions are world
doubles like every body. `CharacterControllerComponent` reads `move_x`/`move_y`, `sprint`
and `jump` relative to the camera's yaw when `playerInput` is on, or takes a desired
velocity from code or Lua when it is off; it turns the object towards its motion and
reports speed and grounded state (which the animation graph reads).

**Vehicle: `physics/Vehicle` on Jolt's `VehicleConstraint` with a
`WheeledVehicleController`.** A box chassis with a lowered centre of mass, four wheels
(front steering, rear or all-wheel drive), engine torque curve, automatic gearbox,
anti-roll bars and ray-cast suspension against both layers. `VehicleComponent` builds it
from reflected properties, reads `throttle`, `brake`, `steer`, `handbrake` when
`playerInput` is on (or takes input from code), places child objects named `Wheel0`..`Wheel3`
at the wheel transforms, and reports speed, RPM and gear for the HUD and engine sound.

**Kinematic bodies move.** `PhysicsComponent` on a kinematic body hands its object's
transform to `PhysicsManager::SetKinematicTarget` every frame; the next physics step moves
the body there with `MoveKinematic` (and stops it when no new target comes), so scripted
traffic pushes the player's car instead of passing through it.

**Queries.** `PhysicsManager::CastRay` and `CastSphere` (closest hit, with a layer mask and
an ignored body) for the camera, interaction checks and AI.

**Late update.** `Component::LateUpdate` runs after every object's `Update` in play mode,
for types that opt in: the camera sees where its target was put this frame.

**Camera: `ThirdPersonCameraComponent`.** Follows a target object by name, orbiting with
`look_x`/`look_y` and pitch limits; when the target is a vehicle it swings behind it after
a short delay without look input. Each frame it sphere-casts from the pivot to the desired
position against static geometry and pulls in to the hit, then eases back out.

## Rejected

- **Rigid-body character** (a dynamic capsule with locked rotation). Jitters on steps and
  slopes, and needs friction hacks; `CharacterVirtual` is Jolt's recommended path.
- **Raycast vehicle written in house.** Jolt's vehicle has tyre friction curves, gearbox,
  differentials and anti-roll bars already tested; ours would be a worse copy.
- **Characters and vehicles in `Application::FixedUpdate`.** Works for one game, but
  every game would rewrite it and the editor's play mode would not run it.

## To verify

- Vehicle tuning at 30 Hz fixed steps on mobile (M7); Jolt recommends 60 Hz or more.
- Character on streamed cell borders: a cell's collision arrives before its objects are
  shown (0007), so walking into a loading cell is safe; checked by the city play test.
