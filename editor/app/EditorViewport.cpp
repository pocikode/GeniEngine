// The scene viewport: editor camera, picking, gizmo interaction and the overlays drawn
// over the scene (grid, selection boxes, light and camera markers, gizmo handles).

#include "EditorApp.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Geni::Editor
{

namespace
{

constexpr ImU32 kAxisColors[3] = {IM_COL32(230, 70, 70, 255), IM_COL32(110, 200, 70, 255), IM_COL32(70, 120, 235, 255)};
constexpr ImU32 kHighlight = IM_COL32(255, 220, 60, 255);
constexpr ImU32 kSelection = IM_COL32(255, 160, 40, 255);

// Draws world-space primitives into an ImGui draw list through one camera.
struct OverlayPainter
{
    ImDrawList &draw;
    glm::mat4 viewProjection;
    glm::vec2 origin; // viewport top-left in screen coordinates
    glm::vec2 size;

    // Clips the segment against the near plane (in clip space) and projects it.
    bool Project(glm::vec3 a, glm::vec3 b, ImVec2 &outA, ImVec2 &outB) const
    {
        glm::vec4 ca = viewProjection * glm::vec4(a, 1.0f);
        glm::vec4 cb = viewProjection * glm::vec4(b, 1.0f);
        const float epsilon = 1e-3f;
        if (ca.w < epsilon && cb.w < epsilon)
        {
            return false;
        }
        if (ca.w < epsilon)
        {
            ca = glm::mix(ca, cb, (epsilon - ca.w) / (cb.w - ca.w));
        }
        else if (cb.w < epsilon)
        {
            cb = glm::mix(cb, ca, (epsilon - cb.w) / (ca.w - cb.w));
        }
        auto toScreen = [&](const glm::vec4 &clip) {
            return ImVec2(
                origin.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x, origin.y + (0.5f - clip.y / clip.w * 0.5f) * size.y
            );
        };
        outA = toScreen(ca);
        outB = toScreen(cb);
        return true;
    }

    void Line(const glm::vec3 &a, const glm::vec3 &b, ImU32 color, float thickness = 1.0f) const
    {
        ImVec2 sa, sb;
        if (Project(a, b, sa, sb))
        {
            draw.AddLine(sa, sb, color, thickness);
        }
    }

    bool Point(const glm::vec3 &world, ImVec2 &out) const
    {
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w < 1e-3f)
        {
            return false;
        }
        out = ImVec2(origin.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x, origin.y + (0.5f - clip.y / clip.w * 0.5f) * size.y);
        return true;
    }

    void Circle(const glm::vec3 &center, const glm::vec3 &normal, float radius, ImU32 color, float thickness) const
    {
        // Any two axes perpendicular to the normal.
        const glm::vec3 helper = std::abs(normal.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 u = glm::normalize(glm::cross(normal, helper));
        const glm::vec3 v = glm::cross(normal, u);
        const int segments = 64;
        glm::vec3 previous = center + u * radius;
        for (int i = 1; i <= segments; ++i)
        {
            const float angle = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853f;
            const glm::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
            Line(previous, next, color, thickness);
            previous = next;
        }
    }

    void Box(const glm::vec3 &low, const glm::vec3 &high, const glm::mat4 &world, ImU32 color) const
    {
        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i)
        {
            const glm::vec3 local((i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y, (i & 4) ? high.z : low.z);
            corners[i] = glm::vec3(world * glm::vec4(local, 1.0f));
        }
        static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                         {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto &edge : edges)
        {
            Line(corners[edge[0]], corners[edge[1]], color, 1.5f);
        }
    }
};

const Mesh *MeshOf(GameObject &object)
{
    if (auto *mesh = object.GetComponent<MeshComponent>())
    {
        return mesh->GetMesh();
    }
    if (auto *skinned = object.GetComponent<SkinnedMeshComponent>())
    {
        return skinned->GetMesh();
    }
    return nullptr;
}

} // namespace

Ray EditorApp::MouseRay(const CameraData &camera) const
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    const glm::vec2 point(mouse.x - base.x - m_viewportPos.x, mouse.y - base.y - m_viewportPos.y);
    return ScreenPointToRay(camera, point, m_viewportSize);
}

bool EditorApp::GetGizmoFrame(GizmoFrame &frame)
{
    GameObject *primary = m_document->GetPrimary();
    if (!primary || !m_document->CanEdit(primary))
    {
        return false;
    }
    // While dragging, the gizmo stays where the drag started (a rotation turns objects
    // around that point, not around wherever they have got to).
    frame.position = m_gizmo.IsDragging() ? m_gizmoPivot : primary->GetWorldPosition();
    // Scale works along the object's own axes whatever the space setting says.
    const bool local = m_gizmo.space == GizmoSpace::Local || m_gizmo.mode == GizmoMode::Scale;
    frame.rotation = local ? primary->GetWorldRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    // A constant size on screen: proportional to the distance from the camera.
    const float distance = glm::length(frame.position - m_camera.GetPosition());
    frame.size = std::max(distance, 0.1f) * 0.16f * std::tan(glm::radians(m_camera.fieldOfViewDegrees) * 0.5f) / 0.5774f;
    return true;
}

void EditorApp::FocusSelection()
{
    const std::vector<GameObject *> selected = m_document->GetSelectedObjects();
    if (selected.empty())
    {
        return;
    }
    glm::vec3 low(0.0f);
    glm::vec3 high(0.0f);
    bool any = false;
    for (GameObject *object : selected)
    {
        glm::vec3 center = object->GetWorldPosition();
        float radius = 0.5f;
        if (const Mesh *mesh = MeshOf(*object))
        {
            const glm::mat4 world = object->GetWorldTransform();
            center = glm::vec3(world * glm::vec4(mesh->GetBounds().center, 1.0f));
            const float scale = std::max({glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))});
            radius = std::max(mesh->GetBounds().radius * scale, 0.1f);
        }
        low = any ? glm::min(low, center - radius) : center - radius;
        high = any ? glm::max(high, center + radius) : center + radius;
        any = true;
    }
    m_camera.Focus((low + high) * 0.5f, glm::length(high - low) * 0.5f);
}

void EditorApp::UpdateViewport(float deltaTime)
{
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    const glm::vec2 mouse(io.MousePos.x - base.x - m_viewportPos.x, io.MousePos.y - base.y - m_viewportPos.y);
    const bool inside = mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x < m_viewportSize.x && mouse.y < m_viewportSize.y;
    // The central dock node is empty, so ImGui does not want the mouse there.
    m_viewportHovered = inside && !io.WantCaptureMouse;

    if (m_document->IsPlaying())
    {
        // The game has the viewport; Escape hands it back.
        m_cameraDragging = false;
        if (m_gizmo.IsDragging())
        {
            m_gizmo.EndDrag();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            TogglePlay();
        }
        return;
    }

    const float aspect = m_viewportSize.x / std::max(m_viewportSize.y, 1.0f);
    const CameraData camera = m_camera.GetCameraData(aspect);

    // --- Camera. A drag that started in the viewport keeps the camera until released. ---
    const bool anyCameraButton = io.MouseDown[1] || io.MouseDown[2] || (io.KeyAlt && io.MouseDown[0]);
    if (!m_cameraDragging && m_viewportHovered && !m_gizmo.IsDragging() &&
        (ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2) || (io.KeyAlt && ImGui::IsMouseClicked(0))))
    {
        m_cameraDragging = true;
    }
    if (!anyCameraButton)
    {
        m_cameraDragging = false;
    }
    CameraInput input;
    if (m_cameraDragging)
    {
        input.mouseDelta = glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
        input.look = io.MouseDown[1];
        input.orbit = !input.look && io.KeyAlt && io.MouseDown[0];
        input.pan = !input.look && !input.orbit && io.MouseDown[2];
        if (input.look && !io.WantTextInput)
        {
            input.move.x = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
            input.move.y = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
            input.move.z = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
            input.fast = io.KeyShift;
        }
    }
    if (m_viewportHovered)
    {
        input.scroll = io.MouseWheel;
    }
    m_camera.Update(input, deltaTime);

    // --- An asset dragged from the Assets panel and released over the scene. ---
    if (const ImGuiPayload *payload = ImGui::GetDragDropPayload())
    {
        if (payload->IsDataType("GENI_ASSET") && inside && ImGui::IsMouseReleased(0))
        {
            // Where the cursor's ray meets the ground plane, or a little way in front.
            const Ray ray = MouseRay(camera);
            glm::vec3 position = ray.At(10.0f);
            if (ray.direction.y < -1e-4f)
            {
                position = ray.At(-ray.origin.y / ray.direction.y);
            }
            ActivateAsset(AssetID{*static_cast<const uint64_t *>(payload->Data)}, &position);
        }
        return; // no picking or gizmo while something is being dragged in
    }

    // --- Gizmo and picking. ---
    const Ray ray = MouseRay(camera);
    GizmoFrame frame;
    const bool hasGizmo = GetGizmoFrame(frame);

    if (m_gizmo.IsDragging())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_gizmoTargets.Revert(*m_scene);
            m_gizmo.EndDrag();
            m_gizmoTargets.Clear();
        }
        else if (!io.MouseDown[0])
        {
            const char *names[] = {"Move", "Rotate", "Scale"};
            m_document->CommitNodeStates(names[static_cast<int>(m_gizmo.mode)], m_gizmoTargets.GetStartStates());
            m_gizmo.EndDrag();
            m_gizmoTargets.Clear();
        }
        else
        {
            // Holding Ctrl snaps for the length of the drag.
            const bool snapSetting = m_gizmo.snap.enabled;
            m_gizmo.snap.enabled = snapSetting != (io.KeyCtrl || io.KeySuper);
            m_gizmoTargets.Apply(*m_scene, m_gizmo.mode, m_gizmo.Drag(ray), m_gizmoPivot);
            m_gizmo.snap.enabled = snapSetting;
        }
        return;
    }

    m_hoveredHandle = GizmoHandle::None;
    if (!m_viewportHovered || m_cameraDragging)
    {
        return;
    }
    if (hasGizmo)
    {
        m_hoveredHandle = m_gizmo.HitTest(ray, frame);
    }
    if (ImGui::IsMouseClicked(0) && !io.KeyAlt)
    {
        if (m_hoveredHandle != GizmoHandle::None && m_gizmo.BeginDrag(m_hoveredHandle, ray, frame, m_camera.GetForward()))
        {
            EndNodeEdit();
            m_gizmoPivot = frame.position;
            std::vector<GameObject *> targets = m_document->GetSelectedRoots();
            targets.erase(
                std::remove_if(targets.begin(), targets.end(), [this](GameObject *object) { return !m_document->CanEdit(object); }),
                targets.end()
            );
            m_gizmoTargets.Capture(targets);
            return;
        }
        // Click on an object: select it. A click on part of a prefab instance selects
        // the instance, which is the thing that can be moved.
        PickResult hit = PickObject(*m_scene, ray);
        GameObject *picked = hit.object;
        while (picked && picked->IsFromPrefab() && picked->GetParent())
        {
            picked = picked->GetParent();
        }
        const bool additive = io.KeyShift || io.KeyCtrl || io.KeySuper;
        if (!picked)
        {
            if (!additive)
            {
                m_document->ClearSelection();
            }
        }
        else if (additive)
        {
            m_document->ToggleSelected(picked->GetID());
        }
        else
        {
            m_document->Select(picked->GetID());
        }
    }
}

void EditorApp::DrawGizmo(ImDrawList &draw, const CameraData &camera)
{
    GizmoFrame frame;
    if (!GetGizmoFrame(frame))
    {
        return;
    }
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    OverlayPainter painter{
        draw, camera.projectionMatrix * camera.viewMatrix, glm::vec2(base.x + m_viewportPos.x, base.y + m_viewportPos.y),
        m_viewportSize
    };
    const GizmoHandle active = m_gizmo.IsDragging() ? m_gizmo.GetActiveHandle() : m_hoveredHandle;
    static const GizmoHandle axisHandles[3] = {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};
    const float size = frame.size;

    if (m_gizmo.mode == GizmoMode::Rotate)
    {
        for (int i = 0; i < 3; ++i)
        {
            const bool lit = active == axisHandles[i];
            painter.Circle(frame.position, frame.Axis(i), GizmoShape::RingRadius * size, lit ? kHighlight : kAxisColors[i], lit ? 3.0f : 2.0f);
        }
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        const glm::vec3 axis = frame.Axis(i);
        const bool lit = active == axisHandles[i] || active == GizmoHandle::Uniform;
        const ImU32 color = lit ? kHighlight : kAxisColors[i];
        const glm::vec3 tip = frame.position + axis * (GizmoShape::AxisEnd * size);
        painter.Line(frame.position + axis * (GizmoShape::AxisStart * size), tip, color, lit ? 3.5f : 2.5f);
        ImVec2 screenTip;
        if (painter.Point(tip, screenTip))
        {
            if (m_gizmo.mode == GizmoMode::Scale)
            {
                draw.AddRectFilled(ImVec2(screenTip.x - 5, screenTip.y - 5), ImVec2(screenTip.x + 5, screenTip.y + 5), color);
            }
            else
            {
                draw.AddCircleFilled(screenTip, 5.0f, color);
            }
        }
    }

    if (m_gizmo.mode == GizmoMode::Translate)
    {
        // Plane handles: a small square between each pair of axes, in the third's colour.
        static const GizmoHandle planes[3] = {GizmoHandle::XY, GizmoHandle::YZ, GizmoHandle::XZ};
        static const int planeAxes[3][3] = {{0, 1, 2}, {1, 2, 0}, {0, 2, 1}};
        for (int i = 0; i < 3; ++i)
        {
            const glm::vec3 u = frame.Axis(planeAxes[i][0]) * size;
            const glm::vec3 v = frame.Axis(planeAxes[i][1]) * size;
            const float low = GizmoShape::PlaneMin;
            const float high = GizmoShape::PlaneMax;
            const glm::vec3 corners[4] = {
                frame.position + u * low + v * low, frame.position + u * high + v * low,
                frame.position + u * high + v * high, frame.position + u * low + v * high
            };
            ImVec2 screen[4];
            bool visible = true;
            for (int c = 0; c < 4; ++c)
            {
                visible = visible && painter.Point(corners[c], screen[c]);
            }
            if (visible)
            {
                const ImU32 color = active == planes[i] ? kHighlight : kAxisColors[planeAxes[i][2]];
                draw.AddQuadFilled(screen[0], screen[1], screen[2], screen[3], (color & 0x00ffffff) | 0x70000000);
                draw.AddQuad(screen[0], screen[1], screen[2], screen[3], color, 1.5f);
            }
        }
    }
    else
    {
        ImVec2 center;
        if (painter.Point(frame.position, center))
        {
            draw.AddCircleFilled(center, 7.0f, active == GizmoHandle::Uniform ? kHighlight : IM_COL32(220, 220, 220, 255));
        }
    }
}

void EditorApp::DrawOverlays()
{
    if (m_document->IsPlaying())
    {
        return;
    }
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    const ImVec2 low(base.x + m_viewportPos.x, base.y + m_viewportPos.y);
    const ImVec2 high(low.x + m_viewportSize.x, low.y + m_viewportSize.y);
    // Behind every panel, over the scene (which the renderer has already drawn).
    ImDrawList &draw = *ImGui::GetBackgroundDrawList();
    draw.PushClipRect(low, high, true);

    const float aspect = m_viewportSize.x / std::max(m_viewportSize.y, 1.0f);
    const CameraData camera = m_camera.GetCameraData(aspect);
    OverlayPainter painter{draw, camera.projectionMatrix * camera.viewMatrix, glm::vec2(low.x, low.y), m_viewportSize};

    if (m_showGrid)
    {
        // A ground grid around the point the camera looks at; the cell size grows with
        // the camera's height so it reads at any zoom.
        const float height = std::max(std::abs(m_camera.GetPosition().y), 1.0f);
        const float cell = std::pow(10.0f, std::floor(std::log10(height)));
        const int half = 20;
        const glm::vec3 pivot = m_camera.GetPivot();
        const float originX = std::round(pivot.x / cell) * cell;
        const float originZ = std::round(pivot.z / cell) * cell;
        const float extent = static_cast<float>(half) * cell;
        for (int i = -half; i <= half; ++i)
        {
            const float offset = static_cast<float>(i) * cell;
            const float x = originX + offset;
            const float z = originZ + offset;
            const ImU32 minor = IM_COL32(255, 255, 255, 28);
            painter.Line(glm::vec3(x, 0, originZ - extent), glm::vec3(x, 0, originZ + extent), std::abs(x) < cell * 0.01f ? kAxisColors[2] : minor);
            painter.Line(glm::vec3(originX - extent, 0, z), glm::vec3(originX + extent, 0, z), std::abs(z) < cell * 0.01f ? kAxisColors[0] : minor);
        }
    }

    if (m_showMarkers)
    {
        // Lights and cameras have nothing to see otherwise. Capped: a city has many lights.
        int budget = 400;
        m_scene->ForEachComponent<LightComponent>([&](LightComponent &light) {
            GameObject *owner = light.GetOwner();
            ImVec2 screen;
            if (budget-- <= 0 || !owner || !painter.Point(owner->GetWorldPosition(), screen))
            {
                return;
            }
            const glm::vec3 color = glm::clamp(light.GetColor(), glm::vec3(0.0f), glm::vec3(1.0f));
            const ImU32 tint = IM_COL32(static_cast<int>(color.r * 255), static_cast<int>(color.g * 255), static_cast<int>(color.b * 255), 255);
            draw.AddCircleFilled(screen, 4.0f, tint);
            draw.AddCircle(screen, 6.0f, IM_COL32(0, 0, 0, 200), 0, 1.5f);
            if (light.GetType() != LightType::Point)
            {
                const glm::vec3 position = owner->GetWorldPosition();
                const float length = glm::length(position - m_camera.GetPosition()) * 0.08f;
                painter.Line(position, position + light.GetDirection() * length, tint, 2.0f);
            }
        });
        m_scene->ForEachComponent<CameraComponent>([&](CameraComponent &component) {
            GameObject *owner = component.GetOwner();
            ImVec2 screen;
            if (owner && painter.Point(owner->GetWorldPosition(), screen))
            {
                draw.AddRect(ImVec2(screen.x - 7, screen.y - 5), ImVec2(screen.x + 7, screen.y + 5), IM_COL32(230, 230, 230, 255), 2.0f, 0, 1.5f);
                draw.AddText(ImVec2(screen.x + 10, screen.y - 7), IM_COL32(230, 230, 230, 255), owner->GetName().c_str());
            }
        });
    }

    // Selection: each selected object's bounding box, oriented with the object.
    int budget = 256;
    for (GameObject *object : m_document->GetSelectedObjects())
    {
        if (budget-- <= 0)
        {
            break;
        }
        if (const Mesh *mesh = MeshOf(*object))
        {
            painter.Box(mesh->GetBounds().min, mesh->GetBounds().max, object->GetWorldTransform(), kSelection);
        }
        else
        {
            ImVec2 screen;
            if (painter.Point(object->GetWorldPosition(), screen))
            {
                draw.AddCircle(screen, 9.0f, kSelection, 0, 2.0f);
            }
        }
    }

    DrawGizmo(draw, camera);
    draw.PopClipRect();
}

} // namespace Geni::Editor
