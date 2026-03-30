#pragma once

#include <string>

struct ma_engine;

namespace Geni
{

class AudioManager
{
  public:
    AudioManager();
    ~AudioManager();

    bool Init();
    void Destroy();

    void SetListenerPosition(float x, float y, float z);
    void SetListenerDirection(float fx, float fy, float fz, float ux, float uy, float uz);

    ma_engine *GetEngine();

  private:
    ma_engine *m_engine = nullptr;
};

} // namespace Geni
