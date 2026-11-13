#include "audio/AudioSystem.h"

#include "core/Log.h"
#include "io/FileSystem.h"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Geni
{

struct AudioSystem::Impl
{
    struct Sound
    {
        std::unique_ptr<ma_sound> sound;
        bool loop = false;
        bool stopping = false;
    };

    ma_engine engine{};
    ma_sound_group music{};
    ma_sound_group effects{};
    bool hasDevice = false;
    FileSystem *fileSystem = nullptr;
    std::unordered_map<SoundId, Sound> sounds;
    SoundId nextId = 1;
    SoundId musicId = InvalidSound;
    std::string musicPath;
    float busVolume[3] = {1.0f, 1.0f, 1.0f};
    std::vector<float> scratch;

    ma_sound *Find(SoundId id)
    {
        auto it = sounds.find(id);
        return it == sounds.end() ? nullptr : it->second.sound.get();
    }

    ma_sound_group *Group(AudioBus bus)
    {
        switch (bus)
        {
        case AudioBus::Music:
            return &music;
        case AudioBus::Effects:
            return &effects;
        case AudioBus::Master:
            break;
        }
        return nullptr;
    }
};

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem()
{
    Shutdown();
}

bool AudioSystem::Init(const AudioConfig &config, FileSystem *fileSystem)
{
    if (m_impl || !config.enabled)
    {
        return m_impl != nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->fileSystem = fileSystem;
    m_variant = config.variant;
    if (m_variant.empty())
    {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || (defined(__APPLE__) && defined(GENI_PLATFORM_IOS))
        m_variant = "mobile";
#else
        m_variant = "desktop";
#endif
    }

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.channels = config.channels;
    engineConfig.sampleRate = config.sampleRate;
    ma_result result = MA_ERROR;
    if (config.device)
    {
        result = ma_engine_init(&engineConfig, &impl->engine);
        if (result != MA_SUCCESS)
        {
            GENI_LOG_WARN("Audio: no output device (%s); running silent", ma_result_description(result));
        }
        impl->hasDevice = result == MA_SUCCESS;
    }
    if (result != MA_SUCCESS)
    {
        engineConfig.noDevice = MA_TRUE;
        result = ma_engine_init(&engineConfig, &impl->engine);
    }
    if (result != MA_SUCCESS)
    {
        GENI_LOG_ERROR("Audio: could not start the mixer: %s", ma_result_description(result));
        return false;
    }
    ma_sound_group_init(&impl->engine, 0, nullptr, &impl->music);
    ma_sound_group_init(&impl->engine, 0, nullptr, &impl->effects);
    m_impl = std::move(impl);
    GENI_LOG_INFO(
        "Audio: %u Hz, %u channels, %s", ma_engine_get_sample_rate(&m_impl->engine),
        ma_engine_get_channels(&m_impl->engine), m_impl->hasDevice ? "output device open" : "no device"
    );
    return true;
}

void AudioSystem::Shutdown()
{
    if (!m_impl)
    {
        return;
    }
    for (auto &[id, sound] : m_impl->sounds)
    {
        (void)id;
        ma_sound_uninit(sound.sound.get());
    }
    m_impl->sounds.clear();
    ma_sound_group_uninit(&m_impl->music);
    ma_sound_group_uninit(&m_impl->effects);
    ma_engine_uninit(&m_impl->engine);
    m_impl.reset();
}

bool AudioSystem::IsInitialized() const
{
    return m_impl != nullptr;
}

bool AudioSystem::HasDevice() const
{
    return m_impl && m_impl->hasDevice;
}

void AudioSystem::Update(float deltaTime)
{
    if (!m_impl)
    {
        return;
    }
    if (!m_impl->hasDevice && deltaTime > 0.0f)
    {
        // Without a device nothing pulls the mixer: advance it by the frame's time so
        // sounds finish and fades run, exactly as they would with one.
        const uint32_t channels = ma_engine_get_channels(&m_impl->engine);
        const uint64_t frames = static_cast<uint64_t>(
            std::min(deltaTime, 0.25f) * static_cast<float>(ma_engine_get_sample_rate(&m_impl->engine))
        );
        m_impl->scratch.resize(static_cast<size_t>(frames * channels));
        ReadFrames(m_impl->scratch.data(), frames);
    }
    for (auto it = m_impl->sounds.begin(); it != m_impl->sounds.end();)
    {
        ma_sound *sound = it->second.sound.get();
        const bool finished =
            (!it->second.loop && ma_sound_at_end(sound)) || (it->second.stopping && !ma_sound_is_playing(sound));
        if (finished)
        {
            ma_sound_uninit(sound);
            if (it->first == m_impl->musicId)
            {
                m_impl->musicId = InvalidSound;
                m_impl->musicPath.clear();
            }
            it = m_impl->sounds.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void AudioSystem::SetCookedResolver(std::function<std::filesystem::path(const std::string &)> resolver)
{
    m_resolver = std::move(resolver);
}

SoundId AudioSystem::Play(const std::string &path, const SoundParams &params)
{
    if (!m_impl || path.empty())
    {
        return InvalidSound;
    }
    std::string file;
    if (m_resolver)
    {
        file = m_resolver(path).string();
    }
    if (file.empty())
    {
        file = m_impl->fileSystem ? m_impl->fileSystem->ResolveAssetPath(path).string() : path;
    }
    auto sound = std::make_unique<ma_sound>();
    ma_uint32 flags = params.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (!params.spatial)
    {
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    }
    const ma_result result =
        ma_sound_init_from_file(&m_impl->engine, file.c_str(), flags, m_impl->Group(params.bus), nullptr, sound.get());
    if (result != MA_SUCCESS)
    {
        GENI_LOG_WARN("Audio: cannot play %s: %s", path.c_str(), ma_result_description(result));
        return InvalidSound;
    }
    ma_sound_set_volume(sound.get(), params.volume);
    ma_sound_set_pitch(sound.get(), std::max(params.pitch, 0.01f));
    ma_sound_set_looping(sound.get(), params.loop ? MA_TRUE : MA_FALSE);
    if (params.spatial)
    {
        ma_sound_set_position(sound.get(), params.position.x, params.position.y, params.position.z);
        ma_sound_set_min_distance(sound.get(), params.minDistance);
        ma_sound_set_max_distance(sound.get(), params.maxDistance);
        ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
    }
    ma_sound_start(sound.get());

    const SoundId id = m_impl->nextId++;
    if (m_impl->nextId == InvalidSound)
    {
        m_impl->nextId = 1;
    }
    Impl::Sound entry;
    entry.sound = std::move(sound);
    entry.loop = params.loop;
    m_impl->sounds.emplace(id, std::move(entry));
    return id;
}

void AudioSystem::Stop(SoundId id, float fadeSeconds)
{
    if (!m_impl)
    {
        return;
    }
    auto it = m_impl->sounds.find(id);
    if (it == m_impl->sounds.end())
    {
        return;
    }
    it->second.stopping = true;
    if (fadeSeconds > 0.0f)
    {
        ma_sound_stop_with_fade_in_milliseconds(it->second.sound.get(), static_cast<ma_uint64>(fadeSeconds * 1000.0f));
    }
    else
    {
        ma_sound_stop(it->second.sound.get());
    }
}

bool AudioSystem::IsPlaying(SoundId id) const
{
    if (!m_impl)
    {
        return false;
    }
    auto it = m_impl->sounds.find(id);
    return it != m_impl->sounds.end() && !it->second.stopping && ma_sound_is_playing(it->second.sound.get());
}

void AudioSystem::SetPosition(SoundId id, const glm::vec3 &position)
{
    if (ma_sound *sound = m_impl ? m_impl->Find(id) : nullptr)
    {
        ma_sound_set_position(sound, position.x, position.y, position.z);
    }
}

void AudioSystem::SetVolume(SoundId id, float volume)
{
    if (ma_sound *sound = m_impl ? m_impl->Find(id) : nullptr)
    {
        ma_sound_set_volume(sound, std::max(volume, 0.0f));
    }
}

void AudioSystem::SetPitch(SoundId id, float pitch)
{
    if (ma_sound *sound = m_impl ? m_impl->Find(id) : nullptr)
    {
        ma_sound_set_pitch(sound, std::max(pitch, 0.01f));
    }
}

void AudioSystem::StopAll()
{
    if (!m_impl)
    {
        return;
    }
    for (auto &[id, sound] : m_impl->sounds)
    {
        (void)id;
        sound.stopping = true;
        ma_sound_stop(sound.sound.get());
    }
}

void AudioSystem::PlayMusic(const std::string &path, float fadeSeconds, float volume)
{
    if (!m_impl)
    {
        return;
    }
    if (path == m_impl->musicPath && m_impl->musicId != InvalidSound)
    {
        return;
    }
    StopMusic(fadeSeconds);
    SoundParams params;
    params.stream = true;
    params.loop = true;
    params.bus = AudioBus::Music;
    params.volume = volume;
    const SoundId id = Play(path, params);
    if (id == InvalidSound)
    {
        return;
    }
    if (fadeSeconds > 0.0f)
    {
        ma_sound_set_fade_in_milliseconds(m_impl->Find(id), 0.0f, 1.0f, static_cast<ma_uint64>(fadeSeconds * 1000.0f));
    }
    m_impl->musicId = id;
    m_impl->musicPath = path;
}

void AudioSystem::StopMusic(float fadeSeconds)
{
    if (!m_impl || m_impl->musicId == InvalidSound)
    {
        return;
    }
    Stop(m_impl->musicId, fadeSeconds);
    m_impl->musicId = InvalidSound;
    m_impl->musicPath.clear();
}

std::string AudioSystem::GetMusicPath() const
{
    return m_impl ? m_impl->musicPath : std::string();
}

void AudioSystem::SetBusVolume(AudioBus bus, float volume)
{
    if (!m_impl)
    {
        return;
    }
    volume = std::clamp(volume, 0.0f, 1.0f);
    m_impl->busVolume[static_cast<int>(bus)] = volume;
    if (bus == AudioBus::Master)
    {
        ma_engine_set_volume(&m_impl->engine, volume);
    }
    else
    {
        ma_sound_group_set_volume(m_impl->Group(bus), volume);
    }
}

float AudioSystem::GetBusVolume(AudioBus bus) const
{
    return m_impl ? m_impl->busVolume[static_cast<int>(bus)] : 0.0f;
}

void AudioSystem::SetListener(const glm::vec3 &position, const glm::vec3 &forward, const glm::vec3 &up)
{
    if (!m_impl)
    {
        return;
    }
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
}

uint64_t AudioSystem::ReadFrames(float *output, uint64_t frames)
{
    if (!m_impl || m_impl->hasDevice || frames == 0)
    {
        return 0;
    }
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m_impl->engine, output, frames, &read);
    return read;
}

uint32_t AudioSystem::GetChannels() const
{
    return m_impl ? ma_engine_get_channels(&m_impl->engine) : 0;
}

uint32_t AudioSystem::GetSampleRate() const
{
    return m_impl ? ma_engine_get_sample_rate(&m_impl->engine) : 0;
}

size_t AudioSystem::GetActiveSoundCount() const
{
    return m_impl ? m_impl->sounds.size() : 0;
}

} // namespace Geni
