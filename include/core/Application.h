#pragma once

namespace Geni
{

class Application
{
  public:
    virtual ~Application() = default;

    virtual bool Init() = 0;
    // Simulation step. Called zero or more times per frame with a constant
    // fixedDeltaTime (EngineConfig::fixedTimestep, 1/60 s by default), right after the
    // physics step. Put gameplay that must not depend on frame rate here.
    virtual void FixedUpdate(float fixedDeltaTime)
    {
    }
    // Called once per rendered frame. deltaTime in seconds.
    virtual void Update(float deltaTime) = 0;
    virtual void Render()
    {
    }
    virtual void Destroy() = 0;

    void SetNeedsToBeClosed(bool value);
    bool NeedsToBeClosed() const;

  private:
    bool m_needsToBeClosed = false;
};

} // namespace Geni
