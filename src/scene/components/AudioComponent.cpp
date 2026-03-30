#include "scene/components/AudioComponent.h"
#include "audio/AudioManager.h"
#include "core/Engine.h"
#include "io/FileSystem.h"
#include "scene/GameObject.h"
#include "miniaudio.h"
#include <iostream>

namespace Geni
{

AudioComponent::AudioComponent()
{
}

AudioComponent::~AudioComponent()
{
    if (m_sound)
    {
        ma_sound_uninit(m_sound);
        delete m_sound;
        m_sound = nullptr;
    }
}

void AudioComponent::Update(float deltaTime)
{
    if (m_sound)
    {
        auto pos = m_owner->GetWorldPosition();
        ma_sound_set_position(m_sound, pos.x, pos.y, pos.z);
    }
}

bool AudioComponent::Load(const std::string &path)
{
    m_path = path;
    auto& engine = Engine::GetInstance();
    auto audioManager = engine.GetAudioManager();

    if (!audioManager || !audioManager->GetEngine())
    {
        return false;
    }

    if (m_sound)
    {
        ma_sound_uninit(m_sound);
    }
    else
    {
        m_sound = new ma_sound();
    }

    std::string fullPath = engine.GetFileSystem().GetAssetsFolder().string() + "/" + path;

    ma_result result = ma_sound_init_from_file(audioManager->GetEngine(), fullPath.c_str(), 0, NULL, NULL, m_sound);
    if (result != MA_SUCCESS)
    {
        std::cerr << "Failed to load sound: " << fullPath << std::endl;
        delete m_sound;
        m_sound = nullptr;
        return false;
    }

    ma_sound_set_looping(m_sound, m_looping ? MA_TRUE : MA_FALSE);

    return true;
}

void AudioComponent::Play()
{
    if (m_sound)
    {
        ma_sound_start(m_sound);
    }
}

void AudioComponent::Stop()
{
    if (m_sound)
    {
        ma_sound_stop(m_sound);
    }
}

void AudioComponent::SetLooping(bool looping)
{
    m_looping = looping;
    if (m_sound)
    {
        ma_sound_set_looping(m_sound, looping ? MA_TRUE : MA_FALSE);
    }
}

} // namespace Geni
