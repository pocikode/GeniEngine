#pragma once

#include "scene/Component.h"
#include <string>

struct ma_sound;

namespace Geni
{

class AudioComponent : public Component
{
  public:
    AudioComponent();
    ~AudioComponent();

    void Update(float deltaTime) override;

    bool Load(const std::string &path);
    void Play();
    void Stop();
    void SetLooping(bool looping);

    COMPONENT(AudioComponent);

  private:
    ma_sound *m_sound = nullptr;
    std::string m_path;
    bool m_looping = false;
};

} // namespace Geni
