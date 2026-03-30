#include "audio/AudioManager.h"
#include <iostream>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace Geni
{

AudioManager::AudioManager()
{
}

AudioManager::~AudioManager()
{
    Destroy();
}

bool AudioManager::Init()
{
    m_engine = new ma_engine();
    ma_result result = ma_engine_init(NULL, m_engine);
    if (result != MA_SUCCESS)
    {
        std::cerr << "Failed to initialize miniaudio engine." << std::endl;
        delete m_engine;
        m_engine = nullptr;
        return false;
    }
    return true;
}

void AudioManager::Destroy()
{
    if (m_engine)
    {
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }
}

void AudioManager::SetListenerPosition(float x, float y, float z)
{
    if (m_engine)
    {
        ma_engine_listener_set_position(m_engine, 0, x, y, z);
    }
}

void AudioManager::SetListenerDirection(float fx, float fy, float fz, float ux, float uy, float uz)
{
    if (m_engine)
    {
        ma_engine_listener_set_direction(m_engine, 0, fx, fy, fz);
        ma_engine_listener_set_world_up(m_engine, 0, ux, uy, uz);
    }
}

ma_engine *AudioManager::GetEngine()
{
    return m_engine;
}

} // namespace Geni
