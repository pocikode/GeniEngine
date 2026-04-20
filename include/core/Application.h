#pragma once

namespace Geni
{

class Application
{
  public:
    virtual bool Init() = 0;
    // deltaTime in seconds
    virtual void Update(float deltaTime) = 0;
    virtual void Render() {}
    virtual void Destroy() = 0;

    void SetNeedsToBeClosed(bool value);
    bool NeedsToBeClosed() const;

  private:
    bool m_needsToBeClosed = false;
};

} // namespace Geni
