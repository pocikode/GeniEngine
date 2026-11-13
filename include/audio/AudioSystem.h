#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Geni
{

class FileSystem;

struct AudioConfig
{
    bool enabled = true;
    // Open the output device. Off (or when opening it fails) the mixer still runs and
    // ReadFrames returns its output: tests and hidden runs use this.
    bool device = true;
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;
    // Which cooked variant of a sound to play (docs/design/0012): "desktop" or "mobile";
    // empty picks this platform's (web uses mobile's).
    std::string variant;
};

enum class AudioBus
{
    Master,
    Music,
    Effects
};

struct SoundParams
{
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    // 3D: attenuated and panned from `position` (scene coordinates).
    bool spatial = false;
    glm::vec3 position{0.0f};
    float minDistance = 2.0f;
    float maxDistance = 120.0f;
    // Decode while playing instead of loading the whole file first (long files).
    bool stream = false;
    AudioBus bus = AudioBus::Effects;
};

using SoundId = uint32_t;
constexpr SoundId InvalidSound = 0;

// Sound on miniaudio (docs/design/0012): fire-and-forget and controllable sounds in 2D
// and 3D, streamed music with fades, master/music/effects volumes. Paths are relative to
// the assets folder. Main thread only.
class AudioSystem
{
  public:
    AudioSystem();
    ~AudioSystem();
    AudioSystem(const AudioSystem &) = delete;
    AudioSystem &operator=(const AudioSystem &) = delete;

    bool Init(const AudioConfig &config, FileSystem *fileSystem);
    void Shutdown();
    bool IsInitialized() const;
    // True when sound goes to a real output device.
    bool HasDevice() const;

    // Releases finished one-shot sounds and advances music fades. Engine calls it once
    // per frame.
    void Update(float deltaTime);

    // `path` is the source file relative to the assets folder ("sounds/door.wav"); the
    // cooked variant is played when the resolver finds one.
    SoundId Play(const std::string &path, const SoundParams &params = SoundParams());
    // Maps a source path to its cooked file (empty: play the source file). Engine sets one
    // that asks the asset manifest for the configured variant.
    void SetCookedResolver(std::function<std::filesystem::path(const std::string &)> resolver);
    // The variant this system asks for: the config's, else this platform's.
    const std::string &GetVariant() const
    {
        return m_variant;
    }
    void Stop(SoundId sound, float fadeSeconds = 0.0f);
    bool IsPlaying(SoundId sound) const;
    void SetPosition(SoundId sound, const glm::vec3 &position);
    void SetVolume(SoundId sound, float volume);
    void SetPitch(SoundId sound, float pitch);
    void StopAll();

    // Streams `path` as the music track, fading out the current one over the same time.
    void PlayMusic(const std::string &path, float fadeSeconds = 1.0f, float volume = 1.0f);
    void StopMusic(float fadeSeconds = 1.0f);
    std::string GetMusicPath() const;

    void SetBusVolume(AudioBus bus, float volume);
    float GetBusVolume(AudioBus bus) const;

    void SetListener(const glm::vec3 &position, const glm::vec3 &forward, const glm::vec3 &up);

    // Mixes `frames` frames of interleaved float output (only without a device).
    uint64_t ReadFrames(float *output, uint64_t frames);
    uint32_t GetChannels() const;
    uint32_t GetSampleRate() const;
    // Sounds currently alive (playing or fading).
    size_t GetActiveSoundCount() const;

  private:
    std::function<std::filesystem::path(const std::string &)> m_resolver;
    std::string m_variant;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Geni
