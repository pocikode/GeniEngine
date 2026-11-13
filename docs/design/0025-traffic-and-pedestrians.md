# 0025: Traffic and pedestrians

Milestone: After v0.1 (open-world layers) · Status: Accepted · Last updated: 2026-11-13

## Problem

Geni City's traffic is 220 lines of Lua (`samples/city/assets/scripts/traffic.lua`) that
only knows the metro world's 32 m grid: crossings are found by rounding positions, turns
are curves computed on the spot, and every car casts a ray each frame to find the car in
front. Another city layout, a highway or a roundabout would need a new script. Cars at a
crossing do not yield to each other; they wait four seconds and push through. Pedestrians
walk the navigation mesh but have no bodies, so cars drive through them and the player's
car passes through people (both are M6 known limits).

**Done** means an engine `RoadNetwork` (a lane graph, loaded from JSON or built as a grid)
and `TrafficSystem` that spawns cars around the focus, drives them along lanes with
car-following, turns them through intersections without collisions between their paths,
stops them for the player and pedestrians, and despawns them out of range; Lua starts and
configures it; Geni City uses it instead of `traffic.lua`; pedestrians are kinematic
capsules that cars stop for and the player's car bumps into.

## Approach

- **`RoadNetwork`** (`world/RoadNetwork.h`): nodes (world positions, double) and directed
  lanes (from, to, speed limit, an optional quadratic Bézier control point, and an
  intersection id for lanes inside a crossing). Lane length and sampling come from the
  curve. JSON `{"format": "geni.roads", "version": 1, "nodes": [...], "lanes": [...]}`.
  `RoadNetwork::Grid(origin, columns, rows, spacing, laneOffset, speed)` builds a
  right-hand-traffic street grid: per crossing, an entry and exit node for each of the
  four directions, straight lanes between crossings, and inside each crossing the straight,
  left and right connectors (curves), all tagged with the crossing's id. Two connectors of
  one intersection **conflict** when their curves come within 2.5 m (computed once per
  intersection, when first needed) and they do not start at the same node.
- **`TrafficSystem`** (owned by `Engine`, updated after `Application::Update`):
  `Start(TrafficConfig)` with the network, car models (cooked scenes or prefabs, spawned
  like `scene.spawn`), count, radius, cruise speeds and keep-clear zones; `Stop`. Each car
  has a lane, a distance along it, a speed and a kinematic box body. Every frame:
  - **Following**: the gap to the nearest car ahead on its lane (or on the lane it has
    chosen next) sets a target speed `(gap - 4 m) / 1.2 s`, capped by the cruise speed and
    the lane's limit; acceleration and braking are limited.
  - **Intersections**: a car chooses its next lane (never a U-turn) before the end of its
    lane; it enters a connector only when no car occupies a conflicting connector of that
    intersection, otherwise it brakes to stop at the line. Cars already inside go first.
  - **Everything else** (the player, the player's car, pedestrians): one ray ahead on the
    moving layer, ignoring other traffic cars, as before.
  - Cars spawn on lanes between 40 m and `radius` from the focus (the active view, or a
    focus object), not in keep-clear zones and not within 14 m of another car, and are
    removed beyond 1.6 × radius. Positions are kept in world coordinates.
- **Pedestrians** stay in Lua on the navigation crowd (0014), with a kinematic capsule
  each (`PhysicsComponent` kinematic follows the object), so traffic's ray sees them and
  the player's car collides with them.
- **Lua** `traffic.start{models, count, radius, grid = {...}}` or a `.groads` path,
  `traffic.stop()`, `traffic.stats()` (cars, metres driven), `traffic.keep_clear(world
  position, radius)`, `traffic.set_focus(object)`.

## Rejected

- **Physics-driven cars (Jolt vehicles) for traffic.** Realistic, but ten vehicle
  constraints cost more than the whole traffic system and need steering control that
  follows lanes; kinematic cars on curves look right at city speeds.
- **Traffic lights.** Conflict-based yielding keeps crossings clear without phases;
  lights are a later layer on the same intersection ids.
- **Lane changes, overtaking, parking.** Later.
- **Storing the metro network as JSON.** 125 x 125 crossings make about 250,000 lanes; the
  grid builder makes them in memory from five numbers.

## To verify

- Building the 125 x 125 grid at start (time and memory). Default: build lazily-computed
  conflicts; measure with `play_city`.
