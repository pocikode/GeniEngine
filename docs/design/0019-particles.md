# 0019: Particles

Milestone: After v0.1 (rendering) · Status: Done · Last updated: 2026-11-13

## Problem

The engine has no particles. Smoke, sparks, dust, rain, splashes and pickup effects need
many small camera-facing quads that are born, move, fade and die every frame. Today a game
would have to create one `GameObject` with a `SpriteComponent` per particle, which costs a
scene node, a world-transform walk and a `SpriteCommand` each, and sprites only lie in
their object's XY plane, so they do not face a 3D camera. Weather (rain, snow) needs the
same thing.

**Done** means a `ParticleEmitterComponent` that emits, simulates and draws thousands of
particles from one object in 2D and 3D, edited in the inspector and driven from Lua
(`emit`, `clear`); the platformer uses it for coin sparkles and landing dust; a gpu test
checks that particles draw, blend additively and with alpha, and cause no validation
errors; a benchmark keeps 20,000 particles inside the frame budget.

## Approach

**Particles are sprite quads.** They reuse the sprite path from docs/design/0017 (per-frame
vertex buffer, texture bind group cache, sort by layer and depth, one indexed draw per
texture run). Two additions to `SpriteCommand`:

- `billboard`: when true, each `SpriteQuad` is placed at its `center` (new field, in the
  command's model space) and spans `min..max` along the camera's right and up axes,
  rotated by its `rotation` (new field, radians) about the view direction. The camera
  axes are those of the view matrix, so every particle faces the screen plane (no
  per-particle look-at; cheaper and stable under an orthographic 2D camera).
- `blend`: `SpriteBlend::Alpha` (the default, as today) or `SpriteBlend::Additive`
  (`src * a + dst`), a second sprite pipeline. Batches split on blend as well as texture.
  Alpha-blended billboard commands have their quads sorted back to front by the renderer;
  additive ones do not need it.

`ParticleEmitterComponent` (scene component, CPU simulation on the main thread):

- **Emission**: `rate` particles per second while `emitting`, plus `burst` particles
  when play starts (and from Lua `emit(n)`); `maxParticles` caps the pool (a fixed vector
  allocated once; new particles are dropped when full). `duration` > 0 stops emission after
  that many seconds unless `loop` is set.
- **Shape**: `point`, `sphere` (`radius`), `box` (`boxSize`), in the object's space. The
  initial velocity is `direction` (local) turned by a random angle up to `spread` degrees,
  times a random speed in `speedMin..speedMax`.
- **Motion**: per particle position, velocity, age, lifetime (`lifetimeMin..Max`), size,
  rotation and spin (`spinMin..spinMax`, degrees per second). Each `Update(dt)`: velocity
  += `gravity` * dt, velocity *= 1 / (1 + `drag` * dt), position += velocity * dt; dead
  particles are removed by swapping with the last.
- **Over life**: size goes from `startSize` to `endSize` and colour (sRGB + alpha) from
  `startColor` to `endColor`, linearly in the particle's normalised age.
- **Space**: with `worldSpace` (the default) particles are stored in scene space and stay
  behind when the emitter moves (smoke from a moving car); `OnOriginShift` moves them.
  Without it they are stored in the object's space and move with it.
- **Randomness**: one `std::minstd_rand`-style generator per emitter, seeded from `seed`,
  so a run with a fixed frame time repeats exactly (screenshots, tests).
- **Drawing**: `Update` and `EditUpdate` write one `SpriteQuad` per live particle and
  submit one billboard `SpriteCommand` with the texture, `blend`, `filter`, `layer` and
  a bounding sphere around the live particles. The texture can be an atlas of
  `columns` x `rows` frames; each particle picks a random frame (`randomFrame`) or plays
  them over its life.
- **Editor**: `EditUpdate` draws the particles that exist but does not simulate; play mode
  runs it. The inspector edits every property.
- **Lua**: properties are reflected (`emitting`, `rate`, ...); `emit(n)`, `clear()` and
  `count()` are methods.

Order of work: renderer billboards and additive blending with a gpu test; the component
and unit tests; the benchmark; the platformer effects; docs.

## Rejected

- **GPU simulation (compute shaders).** Faster for hundreds of thousands of particles, but
  the RHI has no compute yet, WebGPU-shaped compute would need a design of its own, and
  game logic (spawning, collisions with gameplay) is easier on the CPU. Tens of thousands
  is enough for the reference games; revisit with GPU-driven culling.
- **One GameObject per particle.** Easy to edit, far too slow (see Problem).
- **Lit particles.** Smoke lit by the sun and the cluster lights looks better but needs the
  lighting module in the sprite shader and normals; sprites are unlit today. Particles take
  the colour they are given; a game can tint them by time of day.
- **Soft particles (depth fade).** Needs the depth buffer sampled in the HDR pass; with
  MSAA that is a resolve of depth. Later, if hard edges against geometry are a problem.
- **Particle collisions with physics.** Raycasts per particle against Jolt are expensive;
  a ground plane height (`floorY`, particles stop or die on it) covers splashes and rain.
  Kept: `collideFloor` + `floorY` + `bounce`.

## To verify

- Sorting cost for alpha-blended emitters with many particles. Default: accept; use
  additive for large counts.
- The 20,000-particle benchmark budget under lavapipe in CI. Default: the same loose
  budget variable pattern as `benchmark_sprites` (`GENI_PARTICLE_BUDGET_MS`).
