#include "Gizmo.h"

#include "scene/GameObject.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

namespace Geni::Editor
{

namespace
{

// Closest approach of the line `point + s * axis` and the ray. False when they are
// (nearly) parallel. `s` is along the axis, `t` along the ray.
bool ClosestOnAxis(const Ray &ray, const glm::vec3 &point, const glm::vec3 &axis, float &s, float &t)
{
    const glm::vec3 w = point - ray.origin;
    const float b = glm::dot(axis, ray.direction);
    const float d = glm::dot(axis, w);
    const float e = glm::dot(ray.direction, w);
    const float denominator = 1.0f - b * b;
    if (denominator < 1e-5f)
    {
        return false;
    }
    s = (b * e - d) / denominator;
    t = (e - b * d) / denominator;
    return true;
}

bool HitPlane(const Ray &ray, const glm::vec3 &point, const glm::vec3 &normal, glm::vec3 &hit, float &t)
{
    const float facing = glm::dot(ray.direction, normal);
    if (std::abs(facing) < 1e-4f)
    {
        return false;
    }
    t = glm::dot(point - ray.origin, normal) / facing;
    if (t < 0.0f)
    {
        return false;
    }
    hit = ray.At(t);
    return true;
}

float Snap(float value, float step)
{
    return step > 0.0f ? std::round(value / step) * step : value;
}

int AxisIndex(GizmoHandle handle)
{
    switch (handle)
    {
    case GizmoHandle::X:
        return 0;
    case GizmoHandle::Y:
        return 1;
    case GizmoHandle::Z:
        return 2;
    default:
        return -1;
    }
}

// The two axes a plane handle moves along, and the axis that is its normal.
bool PlaneAxes(GizmoHandle handle, int &u, int &v, int &normal)
{
    switch (handle)
    {
    case GizmoHandle::XY:
        u = 0, v = 1, normal = 2;
        return true;
    case GizmoHandle::YZ:
        u = 1, v = 2, normal = 0;
        return true;
    case GizmoHandle::XZ:
        u = 0, v = 2, normal = 1;
        return true;
    default:
        return false;
    }
}

} // namespace

glm::vec3 GizmoFrame::Axis(int index) const
{
    glm::vec3 axis(0.0f);
    axis[index] = 1.0f;
    return rotation * axis;
}

GizmoHandle Gizmo::HitTest(const Ray &ray, const GizmoFrame &frame) const
{
    const float size = frame.size;
    const float tolerance = GizmoShape::HitTolerance * size;
    GizmoHandle best = GizmoHandle::None;
    float bestDistance = 0.0f;
    auto consider = [&](GizmoHandle handle, float distance) {
        if (best == GizmoHandle::None || distance < bestDistance)
        {
            best = handle;
            bestDistance = distance;
        }
    };
    static const GizmoHandle axes[3] = {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};

    if (mode == GizmoMode::Rotate)
    {
        for (int i = 0; i < 3; ++i)
        {
            glm::vec3 hit;
            float t = 0.0f;
            if (HitPlane(ray, frame.position, frame.Axis(i), hit, t) &&
                std::abs(glm::length(hit - frame.position) - GizmoShape::RingRadius * size) < tolerance)
            {
                consider(axes[i], t);
            }
        }
        return best;
    }

    if (mode == GizmoMode::Scale)
    {
        float t = 0.0f;
        if (RayIntersectsSphere(ray, frame.position, GizmoShape::CenterRadius * size, t))
        {
            return GizmoHandle::Uniform;
        }
    }

    if (mode == GizmoMode::Translate)
    {
        static const GizmoHandle planes[3] = {GizmoHandle::XY, GizmoHandle::YZ, GizmoHandle::XZ};
        for (GizmoHandle plane : planes)
        {
            int u = 0, v = 0, normal = 0;
            PlaneAxes(plane, u, v, normal);
            glm::vec3 hit;
            float t = 0.0f;
            if (!HitPlane(ray, frame.position, frame.Axis(normal), hit, t))
            {
                continue;
            }
            const float a = glm::dot(hit - frame.position, frame.Axis(u)) / size;
            const float b = glm::dot(hit - frame.position, frame.Axis(v)) / size;
            if (a >= GizmoShape::PlaneMin && a <= GizmoShape::PlaneMax && b >= GizmoShape::PlaneMin &&
                b <= GizmoShape::PlaneMax)
            {
                consider(plane, t);
            }
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        const glm::vec3 axis = frame.Axis(i);
        float s = 0.0f;
        float t = 0.0f;
        if (!ClosestOnAxis(ray, frame.position, axis, s, t) || t < 0.0f)
        {
            continue;
        }
        if (s < GizmoShape::AxisStart * size || s > GizmoShape::AxisEnd * size)
        {
            continue;
        }
        if (glm::length((frame.position + axis * s) - ray.At(t)) < tolerance)
        {
            consider(axes[i], t);
        }
    }
    return best;
}

bool Gizmo::BeginDrag(GizmoHandle handle, const Ray &ray, const GizmoFrame &frame, const glm::vec3 &cameraForward)
{
    m_active = GizmoHandle::None;
    m_frame = frame;
    m_cameraForward = cameraForward;
    float t = 0.0f;

    const int axis = AxisIndex(handle);
    if (axis >= 0 && mode == GizmoMode::Rotate)
    {
        glm::vec3 hit;
        if (!HitPlane(ray, frame.position, frame.Axis(axis), hit, t) || glm::length(hit - frame.position) < 1e-5f)
        {
            return false;
        }
        m_startPoint = hit;
    }
    else if (axis >= 0)
    {
        if (!ClosestOnAxis(ray, frame.position, frame.Axis(axis), m_startParameter, t))
        {
            return false;
        }
        if (mode == GizmoMode::Scale && std::abs(m_startParameter) < 1e-4f * frame.size)
        {
            return false;
        }
    }
    else if (handle == GizmoHandle::Uniform)
    {
        glm::vec3 hit;
        if (!HitPlane(ray, frame.position, -cameraForward, hit, t))
        {
            return false;
        }
        m_startPoint = hit;
        // Grabbing the centre itself would divide by zero; measure from a small radius.
        m_startParameter = std::max(glm::length(hit - frame.position), GizmoShape::CenterRadius * frame.size * 0.5f);
    }
    else
    {
        int u = 0, v = 0, normal = 0;
        if (!PlaneAxes(handle, u, v, normal) || !HitPlane(ray, frame.position, frame.Axis(normal), m_startPoint, t))
        {
            return false;
        }
    }
    m_active = handle;
    return true;
}

GizmoDelta Gizmo::Drag(const Ray &ray) const
{
    GizmoDelta delta;
    if (m_active == GizmoHandle::None)
    {
        return delta;
    }
    float t = 0.0f;
    const int axis = AxisIndex(m_active);

    if (axis >= 0 && mode == GizmoMode::Rotate)
    {
        const glm::vec3 normal = m_frame.Axis(axis);
        glm::vec3 hit;
        if (!HitPlane(ray, m_frame.position, normal, hit, t))
        {
            return delta;
        }
        const glm::vec3 from = m_startPoint - m_frame.position;
        const glm::vec3 to = hit - m_frame.position;
        if (glm::length(to) < 1e-5f)
        {
            return delta;
        }
        float angle = std::atan2(glm::dot(glm::cross(from, to), normal), glm::dot(from, to));
        if (snap.enabled)
        {
            angle = glm::radians(Snap(glm::degrees(angle), snap.rotateDegrees));
        }
        delta.rotation = glm::angleAxis(angle, normal);
    }
    else if (axis >= 0)
    {
        float s = 0.0f;
        if (!ClosestOnAxis(ray, m_frame.position, m_frame.Axis(axis), s, t))
        {
            return delta;
        }
        if (mode == GizmoMode::Scale)
        {
            float factor = s / m_startParameter;
            if (snap.enabled)
            {
                factor = Snap(factor, snap.scale);
            }
            delta.scale[axis] = std::max(factor, 0.01f);
        }
        else
        {
            float distance = s - m_startParameter;
            if (snap.enabled)
            {
                distance = Snap(distance, snap.translate);
            }
            delta.translation = m_frame.Axis(axis) * distance;
        }
    }
    else if (m_active == GizmoHandle::Uniform)
    {
        glm::vec3 hit;
        if (!HitPlane(ray, m_frame.position, -m_cameraForward, hit, t))
        {
            return delta;
        }
        float factor = glm::length(hit - m_frame.position) / m_startParameter;
        if (snap.enabled)
        {
            factor = Snap(factor, snap.scale);
        }
        delta.scale = glm::vec3(std::max(factor, 0.01f));
    }
    else
    {
        int u = 0, v = 0, normal = 0;
        glm::vec3 hit;
        if (!PlaneAxes(m_active, u, v, normal) || !HitPlane(ray, m_frame.position, m_frame.Axis(normal), hit, t))
        {
            return delta;
        }
        const glm::vec3 moved = hit - m_startPoint;
        float a = glm::dot(moved, m_frame.Axis(u));
        float b = glm::dot(moved, m_frame.Axis(v));
        if (snap.enabled)
        {
            a = Snap(a, snap.translate);
            b = Snap(b, snap.translate);
        }
        delta.translation = m_frame.Axis(u) * a + m_frame.Axis(v) * b;
    }
    return delta;
}

void GizmoTargets::Capture(const std::vector<GameObject *> &objects)
{
    m_entries.clear();
    for (GameObject *object : objects)
    {
        if (!object)
        {
            continue;
        }
        Entry entry;
        entry.id = object->GetID();
        entry.local = NodeState::Capture(*object);
        entry.worldPosition = object->GetWorldPosition();
        entry.worldRotation = object->GetWorldRotation();
        m_entries.push_back(std::move(entry));
    }
}

void GizmoTargets::Apply(Scene &scene, GizmoMode mode, const GizmoDelta &delta, const glm::vec3 &pivot) const
{
    for (const auto &entry : m_entries)
    {
        GameObject *object = scene.FindObjectByID(entry.id);
        if (!object)
        {
            continue;
        }
        GameObject *parent = object->GetParent();
        auto toLocalPosition = [&](const glm::vec3 &world) {
            return parent ? glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(world, 1.0f)) : world;
        };

        switch (mode)
        {
        case GizmoMode::Translate:
            object->SetPosition(toLocalPosition(entry.worldPosition + delta.translation));
            break;
        case GizmoMode::Rotate:
        {
            const glm::quat world = glm::normalize(delta.rotation * entry.worldRotation);
            const glm::quat parentRotation = parent ? parent->GetWorldRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            object->SetRotation(glm::normalize(glm::inverse(parentRotation) * world));
            // Several objects turn around the gizmo, not each around itself.
            object->SetPosition(toLocalPosition(pivot + delta.rotation * (entry.worldPosition - pivot)));
            break;
        }
        case GizmoMode::Scale:
            object->SetScale(entry.local.scale * delta.scale);
            break;
        }
    }
}

void GizmoTargets::Revert(Scene &scene) const
{
    for (const auto &entry : m_entries)
    {
        if (GameObject *object = scene.FindObjectByID(entry.id))
        {
            entry.local.Apply(*object);
        }
    }
}

std::vector<std::pair<uint64_t, NodeState>> GizmoTargets::GetStartStates() const
{
    std::vector<std::pair<uint64_t, NodeState>> states;
    states.reserve(m_entries.size());
    for (const auto &entry : m_entries)
    {
        states.emplace_back(entry.id, entry.local);
    }
    return states;
}

} // namespace Geni::Editor
