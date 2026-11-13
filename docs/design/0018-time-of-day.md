# 0018: Time of day

Milestone: After v0.1 (open-world layers) · Status: Done · Last updated: 2026-11-13

## Problem

The sun, sky, fog and ambient light are fixed. The sun is a directional `LightComponent`
whose rotation is authored in the scene file, and the sky gradient, fog colour and ambient
intensity are `RendererConfig` fields set once at `Engine::Init`. Geni City is always
early afternoon; a game cannot move the clock, and the street lights in the city block
are never seen in the dark.

The sky shader (`src/render/shaders/sky.slang`) and the lighting module
(`geni/lighting.slang`) already read the sun direction and colour, the zenith and horizon
colours, fog and ambient from the per-frame uniforms. Time of day is therefore a CPU
problem: choose those values from a clock each frame.

**Done** means a scene gets a day-night cycle by adding one component to its sun; the
clock runs, stops or jumps from the inspector and from Lua; Geni City starts in the
afternoon and shows the time on its HUD; a screenshot test covers dusk in the metro world.

## Approach

**A pure function from the clock to the lighting, and a component that applies it.**

- `render/TimeOfDay.h`: `EvaluateTimeOfDay(hours, TimeOfDaySettings)` returns a
  `TimeOfDayState`: the light's travel direction, colour and intensity, whether it is the
  sun or the moon, the sun's elevation, and an `Atmosphere` (zenith, horizon, fog colour,
  fog density, ambient intensity). No engine state, so it is unit-tested directly.
- **Sun path**: sunrise at 06:00 due +X, sunset at 18:00 due −X, the highest point at noon
  `maxElevation` degrees above the horizon towards −Z; `pathYaw` turns the whole path about
  +Y. At night the moon sits opposite the sun, so it is as high at midnight as the sun at noon.
- **One directional light carries both.** Above −2° of sun elevation the light is the sun,
  below it the moon. The sun fades to zero by −1° and the moon fades in from −2° to −10°,
  so the switch happens while both are dark and shadows never jump. The sky shader draws
  the moon's disc with the same code as the sun's.
- **Colours are keyed on sun elevation, not on the hour**: night (−18°), blue hour (−6°),
  sunset (0°), golden hour (8°) and day (25° and above), interpolated linearly between keys.
  The day key equals today's `RendererConfig` defaults and the sun's noon colour equals
  Geni City's authored sun, so a scene at midday looks as it does now. A different
  `maxElevation` or latitude needs no new keys.
- `TimeOfDayComponent` goes on the object with the directional `LightComponent`. Reflected
  properties: `time` (hours, wrapped to 0..24), `dayLengthMinutes` (real minutes per game
  day; 0 stops the clock), `maxElevation`, `pathYaw`, `sunIntensity`, `moonIntensity`,
  `fogDensity`. `Update` advances the clock and applies the state; `EditUpdate` applies
  it without advancing, so scrubbing `time` in the inspector relights the editor viewport.
  It writes the owner's world rotation and the light's colour and intensity: the sun is
  driven, and a gizmo on it is overridden on the next frame.
- **The renderer takes the atmosphere for one frame.** `Renderer::SetAtmosphereOverride`
  replaces the config's sky, fog and ambient values for the next `RenderFrame` only, and
  the override is cleared after the frame uniforms are written. Removing the component, a
  scene change or leaving play mode returns to the config values with no cleanup code.
- **Lua** needs no new bindings: `scene.find("Sun"):get("TimeOfDayComponent").time = 21`
  works through reflection. Geni City adds the component in `game.lua`, starting at 15:00
  with a 24-minute day that runs only while playing (stopped on the title and pause
  menus), with `pathYaw` 180 so the afternoon sun reaches its north-south street, and
  shows the clock on the HUD.
- `city --world --time HOURS` adds a stopped clock to the metro sun, for the
  `screenshot_world_dusk` test.

Order of work: `TimeOfDay` and its unit tests; the renderer override; the component and
its tests; `--time` and the dusk screenshot; Geni City and docs.

## Rejected

- **Atmospheric scattering (Preetham, Hosek-Wilkie, Bruneton).** Physically based skies
  look better at dusk but need new shaders, a lookup table or per-pixel cost on mobile
  (rule 5), and the existing gradient sky has to keep working for 2D and stylised games.
  The state struct leaves room for a scattering sky later.
- **Keyframes by hour, authored per scene.** More art control, but every scene would need
  a curve editor before it looked right, and changing the latitude would break the keys.
- **A separate moon light.** A second directional light means a second shadow map set or
  a shadowless moon; the renderer supports one sun. One light that switches while dark costs
  nothing.
- **Writing the sky values into `RendererConfig` from the component.** They would stay
  after the component is gone (stop play in the editor, unload the scene) unless every exit
  path restored them.
- **Time of day as an engine subsystem.** The clock belongs to a scene; the editor, play
  mode and streamed worlds already handle scene-owned components.

## To verify

- Shadow acne or peter-panning at very low sun angles with the current cascade bias.
  Default: accept it; the light is nearly zero below 5°.
- Exposure: night is dark with fixed exposure. Default: leave exposure to the game
  (`Renderer::SetExposure`); revisit with auto-exposure.
