# 0020: Weather

Milestone: After v0.1 (open-world layers) · Status: Accepted · Last updated: 2026-11-13

## Problem

Time of day (0018) moves the sun, but the sky is always clear: no overcast, no rain, no
snow, no thick fog. Open-world games use weather for mood and for gameplay (wet roads,
low visibility). The pieces exist: the frame atmosphere (sky, fog, ambient, darkness) is
replaced per frame, and particles (0019) draw billboards. What is missing is the state
(how cloudy, how much rain or snow, which way the wind blows), the way it changes the
lighting, and precipitation that covers wherever the camera is without emitters placed
across a 4 km world.

**Done** means a `WeatherComponent` that a scene adds once; its properties (and Lua) set
the target weather and it eases towards it; overcast dims the sun and greys the sky,
fog thickens, rain or snow falls around the camera; the editor previews the lighting; a
screenshot test covers rain in the metro world; Geni City has a rain shower in the
evening.

## Approach

- **`WeatherState`** (`render/Weather.h`, plain data): `cloudCover` 0..1, `fog` 0..1,
  `precipitation` 0..1, `snow` (false: rain), `wind` (vec3, m/s). `ApplyWeather(state,
  Atmosphere&, sunScale&)` is pure: cloud cover mixes the zenith and horizon towards a grey
  of their own luminance (cloudier is greyer and a little darker), scales the sun by
  `1 - 0.8 * cloudCover`, raises ambient slightly (skylight from every direction), and
  turns night-only lamps half on in a heavy storm (`darkness = max(darkness, 0.5 *
  smoothstep(0.7, 1, cloudCover))`); fog multiplies the fog density by up to 12 and pulls
  the fog colour to the grey horizon.
- **The renderer applies it for one frame**, after the atmosphere: `Renderer::SetWeather`
  (cleared in `PrepareFrame`, like the atmosphere override, so removing the component
  restores clear weather). This avoids an order dependency between the time of day and
  weather components, which both run in `Update`: whichever runs first, the renderer
  combines them.
- **`WeatherComponent`**: reflected targets (`cloudCover`, `fog`, `precipitation`, `snow`,
  `wind`) and `transitionSeconds`. `Update` moves the current state towards the targets at
  1 / `transitionSeconds` per second (switching rain to snow first fades precipitation
  out), sends it to the renderer, and simulates precipitation; `EditUpdate` snaps to the
  targets and sends the lighting only. Lua: properties plus `snap()`.
- **Precipitation is a box that follows the camera**: up to `maxDrops` (default 6000)
  drops live in a box (`radius` 30 m around, 25 m tall) centred on the view position (the
  editor's view override or the main camera). Each drop falls at its speed plus the wind;
  a drop that leaves the box (or falls below the box's floor) wraps to the other side, so
  nothing is spawned or destroyed and the density is even everywhere. The number drawn is
  `precipitation * maxDrops`. Drops are one billboard `SpriteCommand` (alpha blended, in
  scene space): rain is thin tall quads (`ParticleEmitterComponent` gains nothing), snow
  is small squares that drift. Drops do not stop at roofs.
- **Geni City**: `game.lua` adds the component; a shower builds from 17:00 and clears by
  19:00. `city --world --weather rain|snow|fog` sets a weather for captures; test
  `screenshot_world_rain`.

Order of work: `WeatherState`/`ApplyWeather` with unit tests; renderer hook; component
with precipitation and tests; city and the screenshot; docs.

## Rejected

- **Precipitation through `ParticleEmitterComponent`.** An emitter needs an owner object
  that follows the camera, emission and death; a wrapping box keeps a constant count with
  no spawning, and needs neither.
- **Wet surfaces (darker albedo, lower roughness, puddles).** Needs the standard shader
  to read a wetness value and a mask; worth doing with a material that has one. The state
  carries `precipitation`, so it can be added without changing the component.
- **Volumetric clouds and rain occlusion maps.** Far beyond the mobile budget (rule 5);
  the gradient sky and fog stay.
- **Weather as part of TimeOfDayComponent.** Many scenes want one without the other (a
  fixed-noon level in the rain).

## To verify

- Rain drawn through roofs and tunnels. Default: accept for now; an occlusion height map
  from the world cooker would fix it.
- Sorting 6,000 alpha drops each frame. Default: accept (benchmark_particles sorts more).
