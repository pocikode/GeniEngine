# 0012: Audio

Milestone: M6 · Status: Done · Last updated: 2026-11-13

## Problem

The engine has no sound. The reference game needs 3D sounds that follow objects (a car's
engine whose pitch follows its RPM, footsteps), streamed music that fades between tracks,
and volume settings in the menu. CI and tests run on machines without an audio device.

**Done** means an engine-owned audio system plays one-shot and looping sounds in 2D and 3D,
streams music with fades, has master, music and effects volumes, updates the listener from
the main camera, and runs silently (but fully) when there is no device.

## Approach

**miniaudio** (submodule, 0.11.23), compiled once into the engine in its own translation
unit. Only `src/audio/` includes it.

**`audio/AudioSystem`**, owned by `Engine` and initialised from `EngineConfig::audio`:

- One `ma_engine` with its resource manager (decoding and streaming on its own thread).
  WAV, FLAC and MP3 decode out of the box.
- Three groups: `master` → `music`, `effects`. Volumes are linear 0..1, set by the game.
- `Play(path, SoundParams)` for fire-and-forget sounds (position, volume, pitch); returns a
  `SoundId` that can be stopped or moved while it plays.
- `PlayMusic(path, fadeSeconds)` streams the file (`MA_SOUND_FLAG_STREAM`) and fades out
  the previous track over the same time; `StopMusic(fade)`.
- `SetListener(position, forward, up)`. `Engine::Simulate` sets it from the render camera
  every frame. Positions are scene coordinates: listener and sources rebase together.
- **No device** (`EngineConfig::audio.device = false`, or when opening the device fails):
  the engine is created with `noDevice`, so sounds still load and play and tests can read
  mixed frames with `ReadFrames`. Hidden runs and tests use this.

**`AudioSourceComponent`**: `clip` (path in the assets folder), `volume`, `pitch`, `loop`,
`spatial`, `minDistance`, `maxDistance`, `autoplay`, `stream`. It owns one `ma_sound`,
moves it with its object each frame, and is the engine sound of the car (the game sets
pitch from RPM) and the music source in the editor.

**Cooked audio (added 2026-11-13, M7).** Game code still names sounds by their source
path. The cooker turns every `.wav` into one variant per platform,
`cooked/audio/<desktop|mobile>/<path>.gsnd` (asset type `sound`, source
`"<path>#<platform>"`): the same 16-bit PCM, or **IMA ADPCM** in a WAV container (4 bits per
sample, a quarter of the size; miniaudio decodes it, by content, whatever the extension).
Desktop compresses sounds of 2 s or more (music, ambience) and keeps short effects as PCM;
mobile (and web) compress everything. A `.meta` `"compression": "pcm"` or `"adpcm"`
overrides the rule; files that are not 16-bit PCM are copied unchanged. `AudioSystem::Play`
asks a resolver for the cooked file first (`Engine` sets one that looks up the manifest for
`AudioConfig::variant`, this platform's by default) and falls back to the source file.
Packages leave out the source WAVs and the other platforms' variants. The last ADPCM block
is cut short, so a sound decodes at its source length plus at most 7 frames, which keeps
loops tight.

## Rejected

- **FMOD or Wwise.** Licensed, closed, and a second toolchain for contributors.
- **OpenAL Soft.** LGPL and no built-in decoding or streaming; miniaudio has both in two
  files with a public-domain licence.
- **Cooking audio in M6.** The reference game ships a few small WAV files; compression
  matters on mobile (M7).
- **Vorbis or Opus for cooked audio.** Better quality per byte, but encoding needs libvorbis
  or libopus in the cooker (and a decoder build at run time); IMA ADPCM needs about 150
  lines and nothing new. Revisit if package size on mobile demands it.

## To verify

- Latency and device switching (headphones plugged in) on macOS and Windows.
- That PulseAudio-less Linux CI falls back to no device without a long timeout.
