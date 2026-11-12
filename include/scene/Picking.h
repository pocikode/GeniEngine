#pragma once

#include "core/Common.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Geni
{

class GameObject;
class Scene;

struct Ray
{
    glm::vec3 origin{0.0f};
    // Normalised.
    glm::vec3 direction{0.0f, 0.0f, -1.0f};

    glm::vec3 At(float distance) const
    {
        return origin + direction * distance;
    }
};

// The ray through a pixel of the view. `point` is in pixels from the view's top-left
// corner and `viewSize` is the view's size; `camera` is what the view was rendered with.
Ray ScreenPointToRay(const CameraData &camera, const glm::vec2 &point, const glm::vec2 &viewSize);

// Where a world position lands in the view, in pixels from its top-left corner. False
// when the position is behind the camera (`out` is then meaningless).
bool WorldToScreen(const CameraData &camera, const glm::vec3 &world, const glm::vec2 &viewSize, glm::vec2 &out);

// Distance along the ray to the first intersection, when there is one in front of the
// origin. A ray that starts inside hits at distance 0.
bool RayIntersectsSphere(const Ray &ray, const glm::vec3 &center, float radius, float &distance);
// An axis-aligned box in the space of `world` (an oriented box in world space).
bool RayIntersectsBox(
    const Ray &ray, const glm::vec3 &localMin, const glm::vec3 &localMax, const glm::mat4 &world, float &distance
);

struct PickOptions
{
    // Lights and cameras have no mesh; they are picked by a sphere of this radius.
    float markerRadius = 0.5f;
    bool includeInactive = false;
};

struct PickResult
{
    GameObject *object = nullptr;
    float distance = 0.0f;

    explicit operator bool() const
    {
        return object != nullptr;
    }
};

// The nearest object the ray hits. Meshes are tested against their bounding box,
// oriented with the object: exact for boxes, generous for everything else (the runtime
// keeps no CPU vertex data to test triangles with).
PickResult PickObject(Scene &scene, const Ray &ray, const PickOptions &options = PickOptions());

} // namespace Geni
