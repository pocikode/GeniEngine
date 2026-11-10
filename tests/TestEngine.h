#pragma once

#include "core/Application.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Geni
{
class Scene;
}

namespace GeniTest
{

// Application that only records how the engine drives it.
class RecordingApp : public Geni::Application
{
  public:
    bool Init() override
    {
        return true;
    }

    void FixedUpdate(float fixedDeltaTime) override;
    void Update(float deltaTime) override;

    void Destroy() override
    {
    }

    int fixedUpdates = 0;
    int updates = 0;
    float lastFixedDelta = 0.0f;
    // When set, Update calls Scene::Update on it, as a real application would.
    Geni::Scene *sceneToUpdate = nullptr;
};

// Starts the engine once, with a hidden window, assets rooted at tests/assets and a
// 1/60 s fixed step. Returns the application the engine is running.
// Fails the calling test if no OpenGL 3.3 context can be created.
RecordingApp &RequireEngine();

// Gives the engine a new empty scene (destroying the previous one) and returns it.
Geni::Scene *ResetScene();

void ShutdownEngine();

inline bool Near(float a, float b, float epsilon = 1e-4f)
{
    return a - b < epsilon && b - a < epsilon;
}

inline bool Near(const glm::vec3 &a, const glm::vec3 &b, float epsilon = 1e-4f)
{
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

inline bool Near(const glm::mat4 &a, const glm::mat4 &b, float epsilon = 1e-4f)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!Near(a[column][row], b[column][row], epsilon))
            {
                return false;
            }
        }
    }
    return true;
}

// Two quaternions are the same rotation if they are equal or exactly opposite.
inline bool SameRotation(const glm::quat &a, const glm::quat &b, float epsilon = 1e-4f)
{
    const float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    return Near(dot < 0.0f ? -dot : dot, 1.0f, epsilon);
}

} // namespace GeniTest
