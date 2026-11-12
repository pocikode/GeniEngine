#include "scene/Picking.h"

#include "render/Mesh.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/SkinnedMeshComponent.h"
#include "scene/components/SpriteComponent.h"
#include "scene/components/TilemapComponent.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <limits>

namespace Geni
{

Ray ScreenPointToRay(const CameraData &camera, const glm::vec2 &point, const glm::vec2 &viewSize)
{
    const float width = std::max(viewSize.x, 1.0f);
    const float height = std::max(viewSize.y, 1.0f);
    // Clip space has Y up; pixels have Y down.
    const glm::vec2 ndc(2.0f * point.x / width - 1.0f, 1.0f - 2.0f * point.y / height);

    // The projection is OpenGL-style (glm::perspective): depth runs from -1 to 1.
    const glm::mat4 inverse = glm::inverse(camera.projectionMatrix * camera.viewMatrix);
    glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    Ray ray;
    ray.origin = glm::vec3(nearPoint);
    const glm::vec3 delta = glm::vec3(farPoint) - glm::vec3(nearPoint);
    const float length = glm::length(delta);
    ray.direction = length > 0.0f ? delta / length : glm::vec3(0.0f, 0.0f, -1.0f);
    return ray;
}

bool WorldToScreen(const CameraData &camera, const glm::vec3 &world, const glm::vec2 &viewSize, glm::vec2 &out)
{
    const glm::vec4 clip = camera.projectionMatrix * camera.viewMatrix * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-6f)
    {
        return false;
    }
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    out = glm::vec2((ndc.x * 0.5f + 0.5f) * viewSize.x, (0.5f - ndc.y * 0.5f) * viewSize.y);
    return true;
}

bool RayIntersectsSphere(const Ray &ray, const glm::vec3 &center, float radius, float &distance)
{
    const glm::vec3 toCenter = center - ray.origin;
    const float along = glm::dot(toCenter, ray.direction);
    const float offsetSquared = glm::dot(toCenter, toCenter) - along * along;
    const float radiusSquared = radius * radius;
    if (offsetSquared > radiusSquared)
    {
        return false;
    }
    const float half = std::sqrt(radiusSquared - offsetSquared);
    if (along + half < 0.0f)
    {
        return false; // the whole sphere is behind the origin
    }
    distance = std::max(along - half, 0.0f);
    return true;
}

bool RayIntersectsBox(
    const Ray &ray, const glm::vec3 &localMin, const glm::vec3 &localMax, const glm::mat4 &world, float &distance
)
{
    if (std::abs(glm::determinant(world)) < 1e-12f)
    {
        return false; // a zero scale: nothing to hit
    }
    // Into the box's space. The direction is left unnormalised so the slab parameter is
    // still a distance along the world-space ray.
    const glm::mat4 inverse = glm::inverse(world);
    const glm::vec3 origin = glm::vec3(inverse * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 direction = glm::vec3(inverse * glm::vec4(ray.direction, 0.0f));

    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(direction[axis]) < 1e-12f)
        {
            if (origin[axis] < localMin[axis] || origin[axis] > localMax[axis])
            {
                return false;
            }
            continue;
        }
        float t0 = (localMin[axis] - origin[axis]) / direction[axis];
        float t1 = (localMax[axis] - origin[axis]) / direction[axis];
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax)
        {
            return false;
        }
    }
    distance = tMin;
    return true;
}

namespace
{

void PickRecursive(GameObject &object, const Ray &ray, const PickOptions &options, PickResult &best)
{
    if (!object.IsAlive() || (!options.includeInactive && !object.IsActive()))
    {
        return;
    }

    const Mesh *mesh = nullptr;
    if (auto *component = object.GetComponent<MeshComponent>())
    {
        mesh = component->GetMesh();
    }
    else if (auto *skinned = object.GetComponent<SkinnedMeshComponent>())
    {
        mesh = skinned->GetMesh();
    }

    float distance = 0.0f;
    bool hit = false;
    if (mesh)
    {
        const MeshBounds &bounds = mesh->GetBounds();
        // A flat mesh (a quad) still needs some thickness to be hit.
        const glm::vec3 pad(1e-4f);
        hit = RayIntersectsBox(ray, bounds.min - pad, bounds.max + pad, object.GetWorldTransform(), distance);
    }
    else if (auto *sprite = object.GetComponent<SpriteComponent>())
    {
        // The rectangle in the object's XY plane, as SpriteComponent::BuildQuad lays it out.
        const glm::vec2 lo = -sprite->GetPivot() * sprite->GetSize();
        const glm::vec2 hi = lo + sprite->GetSize();
        const glm::vec3 pad(0.0f, 0.0f, 1e-4f);
        hit = sprite->IsVisible() &&
              RayIntersectsBox(ray, glm::vec3(lo, 0.0f) - pad, glm::vec3(hi, 0.0f) + pad, object.GetWorldTransform(), distance);
        // Flat sprites often share a depth: the one drawn on top (higher layer) wins.
        distance -= 1e-3f * static_cast<float>(sprite->GetLayer());
    }
    else if (auto *tilemap = object.GetComponent<TilemapComponent>())
    {
        const glm::vec2 size(
            static_cast<float>(tilemap->GetWidth()) * tilemap->GetTileSize().x,
            static_cast<float>(tilemap->GetHeight()) * tilemap->GetTileSize().y
        );
        const glm::vec3 pad(0.0f, 0.0f, 1e-4f);
        hit = RayIntersectsBox(
            ray, glm::vec3(0.0f, -size.y, 0.0f) - pad, glm::vec3(size.x, 0.0f, 0.0f) + pad, object.GetWorldTransform(),
            distance
        );
        distance -= 1e-3f * static_cast<float>(tilemap->GetLayer());
    }
    else if (object.GetComponent<LightComponent>() || object.GetComponent<CameraComponent>())
    {
        hit = RayIntersectsSphere(ray, object.GetWorldPosition(), options.markerRadius, distance);
    }
    if (hit && (!best.object || distance < best.distance))
    {
        best.object = &object;
        best.distance = distance;
    }

    for (const auto &child : object.GetChildren())
    {
        PickRecursive(*child, ray, options, best);
    }
}

} // namespace

PickResult PickObject(Scene &scene, const Ray &ray, const PickOptions &options)
{
    PickResult best;
    for (const auto &root : scene.GetObjects())
    {
        PickRecursive(*root, ray, options, best);
    }
    return best;
}

} // namespace Geni
