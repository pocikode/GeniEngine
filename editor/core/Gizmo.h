#pragma once

#include "EditorCommands.h"

#include "scene/Picking.h"

#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <utility>
#include <vector>

namespace Geni
{
class GameObject;
class Scene;
} // namespace Geni

namespace Geni::Editor
{

enum class GizmoMode
{
    Translate,
    Rotate,
    Scale
};

enum class GizmoSpace
{
    World,
    Local
};

enum class GizmoHandle
{
    None,
    X,
    Y,
    Z,
    // Translate only: move in the plane of two axes.
    XY,
    YZ,
    XZ,
    // Scale only: all three axes together.
    Uniform
};

// Where the gizmo sits. `size` is its length in world units; the viewport picks it so
// the gizmo keeps the same size on screen.
struct GizmoFrame
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float size = 1.0f;

    glm::vec3 Axis(int index) const;
};

// What a drag has done so far, relative to where it started.
struct GizmoDelta
{
    glm::vec3 translation{0.0f};
    // World-space rotation about the gizmo position.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    // Per local axis.
    glm::vec3 scale{1.0f};
};

struct GizmoSnap
{
    bool enabled = false;
    float translate = 0.5f;
    float rotateDegrees = 15.0f;
    float scale = 0.1f;
};

// Proportions of the handles, as fractions of GizmoFrame::size. The drawing code uses
// the same numbers, so what is drawn is what can be hit.
namespace GizmoShape
{
constexpr float AxisStart = 0.15f;
constexpr float AxisEnd = 1.0f;
constexpr float PlaneMin = 0.25f;
constexpr float PlaneMax = 0.5f;
constexpr float RingRadius = 1.0f;
constexpr float CenterRadius = 0.12f;
constexpr float HitTolerance = 0.08f;
} // namespace GizmoShape

// Translate / rotate / scale handles as ray geometry (docs/design/0006). No drawing and
// no input here: HitTest says which handle a ray touches, and a drag turns the ray under
// the cursor into a GizmoDelta.
class Gizmo
{
  public:
    GizmoMode mode = GizmoMode::Translate;
    GizmoSpace space = GizmoSpace::World;
    GizmoSnap snap;

    GizmoHandle HitTest(const Ray &ray, const GizmoFrame &frame) const;

    // Starts a drag on `handle`. False when the ray cannot grab it (looking along an
    // axis, or parallel to a plane).
    bool BeginDrag(GizmoHandle handle, const Ray &ray, const GizmoFrame &frame, const glm::vec3 &cameraForward);
    // The delta for the ray now under the cursor. Identity when no drag is active.
    GizmoDelta Drag(const Ray &ray) const;
    void EndDrag()
    {
        m_active = GizmoHandle::None;
    }
    bool IsDragging() const
    {
        return m_active != GizmoHandle::None;
    }
    GizmoHandle GetActiveHandle() const
    {
        return m_active;
    }

  private:
    GizmoHandle m_active = GizmoHandle::None;
    GizmoFrame m_frame;
    glm::vec3 m_cameraForward{0.0f, 0.0f, -1.0f};
    // Where the drag started: a distance along an axis, a point on a plane.
    float m_startParameter = 0.0f;
    glm::vec3 m_startPoint{0.0f};
};

// Objects a gizmo drag acts on, with the state each had when the drag began. Deltas are
// always applied to these start states, so a drag does not accumulate rounding.
class GizmoTargets
{
  public:
    // Captures `objects` (already without descendants of other selected objects).
    void Capture(const std::vector<GameObject *> &objects);
    void Clear()
    {
        m_entries.clear();
    }
    bool IsEmpty() const
    {
        return m_entries.empty();
    }
    // Moves the captured objects to start state + delta. `pivot` is the gizmo position.
    void Apply(Scene &scene, GizmoMode mode, const GizmoDelta &delta, const glm::vec3 &pivot) const;
    // Puts every object back where it started (a cancelled drag).
    void Revert(Scene &scene) const;
    // For EditorDocument::CommitNodeStates.
    std::vector<std::pair<uint64_t, NodeState>> GetStartStates() const;

  private:
    struct Entry
    {
        uint64_t id = 0;
        NodeState local;
        glm::vec3 worldPosition{0.0f};
        glm::quat worldRotation{1.0f, 0.0f, 0.0f, 0.0f};
    };
    std::vector<Entry> m_entries;
};

} // namespace Geni::Editor
