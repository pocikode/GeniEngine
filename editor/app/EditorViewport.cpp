// The scene viewport: editor camera, picking, gizmo interaction and the overlays drawn
// over the scene (grid, selection boxes, light and camera markers, gizmo handles).

#include "EditorApp.h"
#include "EditorTheme.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Geni::Editor
{

namespace
{

constexpr ImU32 kAxisColors[3] = {Theme::AxisX, Theme::AxisY, Theme::AxisZ};
constexpr ImU32 kHighlight = IM_COL32(255, 220, 60, 255);
constexpr ImU32 kSelection = IM_COL32(255, 160, 40, 255);

ImU32 WithAlpha(ImU32 color, float alpha)
{
    const float base = static_cast<float>((color >> 24) & 0xff) / 255.0f;
    const int a = static_cast<int>(std::clamp(alpha * base, 0.0f, 1.0f) * 255.0f);
    return (color & 0x00ffffffu) | (static_cast<ImU32>(a) << 24);
}

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

// Frames everything in the scene: mesh bounds where there are meshes, positions otherwise.
void EditorApp::FrameScene()
{
    glm::vec3 low(0.0f);
    glm::vec3 high(0.0f);
    bool any = false;
    std::vector<GameObject *> stack;
    for (const auto &root : m_scene->GetObjects())
    {
        stack.push_back(root.get());
    }
    while (!stack.empty())
    {
        GameObject *object = stack.back();
        stack.pop_back();
        glm::vec3 center = object->GetWorldPosition();
        float radius = 0.0f;
        if (const Mesh *mesh = MeshOf(*object))
        {
            const glm::mat4 world = object->GetWorldTransform();
            center = glm::vec3(world * glm::vec4(mesh->GetBounds().center, 1.0f));
            const float scale = std::max({glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))});
            radius = mesh->GetBounds().radius * scale;
        }
        low = any ? glm::min(low, center - radius) : center - radius;
        high = any ? glm::max(high, center + radius) : center + radius;
        any = true;
        for (const auto &child : object->GetChildren())
        {
            stack.push_back(child.get());
        }
    }
    if (!any)
    {
        m_camera.Focus(glm::vec3(0.0f), 6.0f);
        return;
    }
    m_camera.Focus((low + high) * 0.5f, std::max(glm::length(high - low) * 0.5f, 3.0f));
}

void EditorApp::UpdateViewport(float deltaTime)
{
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    const glm::vec2 mouse(io.MousePos.x - base.x - m_viewportPos.x, io.MousePos.y - base.y - m_viewportPos.y);
    const bool inside = mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x < m_viewportSize.x && mouse.y < m_viewportSize.y;
    // The central dock node is empty, so ImGui does not want the mouse there.
    m_viewportHovered = inside && !io.WantCaptureMouse && !m_orientationHovered;

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
            ImVec2 screenBase;
            if (m_gizmo.mode == GizmoMode::Scale)
            {
                draw.AddRectFilled(ImVec2(screenTip.x - 5, screenTip.y - 5), ImVec2(screenTip.x + 5, screenTip.y + 5), color, 1.5f);
            }
            else if (painter.Point(frame.position + axis * (GizmoShape::AxisEnd * size * 0.82f), screenBase))
            {
                // A cone seen from the side: a triangle along the axis on screen.
                float dx = screenTip.x - screenBase.x;
                float dy = screenTip.y - screenBase.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 1.0f)
                {
                    dx /= length;
                    dy /= length;
                    const float head = std::max(length, 9.0f);
                    const ImVec2 tipPoint(screenBase.x + dx * head, screenBase.y + dy * head);
                    draw.AddTriangleFilled(
                        tipPoint, ImVec2(screenBase.x - dy * 6.0f, screenBase.y + dx * 6.0f),
                        ImVec2(screenBase.x + dy * 6.0f, screenBase.y - dx * 6.0f), color
                    );
                }
                else
                {
                    draw.AddCircleFilled(screenTip, 5.0f, color);
                }
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
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    const ImVec2 low(base.x + m_viewportPos.x, base.y + m_viewportPos.y);
    const ImVec2 high(low.x + m_viewportSize.x, low.y + m_viewportSize.y);
    // Behind every panel, over the scene (which the renderer has already drawn).
    ImDrawList &draw = *ImGui::GetBackgroundDrawList();
    if (m_document->IsPlaying())
    {
        // A tinted frame says the game has the viewport, as Unity tints its play mode.
        draw.AddRect(ImVec2(low.x + 1.0f, low.y + 1.0f), ImVec2(high.x - 1.0f, high.y - 1.0f), m_paused ? Theme::Warning : Theme::Accent, 0.0f, 0, 3.0f);
        return;
    }
    if (m_stage == Stage::ProjectManager)
    {
        return;
    }
    draw.PushClipRect(low, high, true);

    const float aspect = m_viewportSize.x / std::max(m_viewportSize.y, 1.0f);
    const CameraData camera = m_camera.GetCameraData(aspect);
    OverlayPainter painter{draw, camera.projectionMatrix * camera.viewMatrix, glm::vec2(low.x, low.y), m_viewportSize};

    if (m_showGrid)
    {
        // A ground grid around the point the camera looks at. Two levels of lines a factor
        // of ten apart, the finer one fading out as the camera rises, and every line fading
        // with distance so the grid has no hard edge.
        const float height = std::max(std::abs(m_camera.GetPosition().y), 1.0f);
        const float level = std::log10(height * 0.5f);
        const float cell = std::pow(10.0f, std::floor(level));
        const float fineFade = 1.0f - (level - std::floor(level));
        const int half = 30;
        const glm::vec3 pivot = m_camera.GetPivot();
        const float extent = static_cast<float>(half) * cell;
        const float originX = std::round(pivot.x / (cell * 10.0f)) * cell * 10.0f;
        const float originZ = std::round(pivot.z / (cell * 10.0f)) * cell * 10.0f;
        const int segments = 12;
        auto gridLine = [&](glm::vec3 a, glm::vec3 b, ImU32 color, float thickness) {
            for (int k = 0; k < segments; ++k)
            {
                const glm::vec3 p0 = glm::mix(a, b, static_cast<float>(k) / segments);
                const glm::vec3 p1 = glm::mix(a, b, static_cast<float>(k + 1) / segments);
                const glm::vec3 mid = (p0 + p1) * 0.5f;
                const float distance = glm::length(glm::vec2(mid.x - pivot.x, mid.z - pivot.z)) / extent;
                const float alpha = std::clamp(1.0f - distance, 0.0f, 1.0f);
                if (alpha > 0.01f)
                {
                    painter.Line(p0, p1, WithAlpha(color, alpha * alpha), thickness);
                }
            }
        };
        for (int i = -half; i <= half; ++i)
        {
            const float offset = static_cast<float>(i) * cell;
            const float x = originX + offset;
            const float z = originZ + offset;
            const bool majorX = std::abs(std::fmod(std::abs(x), cell * 10.0f)) < cell * 0.01f;
            const bool majorZ = std::abs(std::fmod(std::abs(z), cell * 10.0f)) < cell * 0.01f;
            const ImU32 minor = IM_COL32(255, 255, 255, static_cast<int>(22.0f * fineFade));
            const ImU32 major = IM_COL32(255, 255, 255, 46);
            const bool axisX = std::abs(x) < cell * 0.01f;
            const bool axisZ = std::abs(z) < cell * 0.01f;
            gridLine(glm::vec3(x, 0, originZ - extent), glm::vec3(x, 0, originZ + extent),
                     axisX ? WithAlpha(kAxisColors[2], 0.8f) : majorX ? major : minor, axisX ? 1.5f : 1.0f);
            gridLine(glm::vec3(originX - extent, 0, z), glm::vec3(originX + extent, 0, z),
                     axisZ ? WithAlpha(kAxisColors[0], 0.8f) : majorZ ? major : minor, axisZ ? 1.5f : 1.0f);
        }
    }

    if (m_showMarkers)
    {
        // Lights and cameras have nothing to see otherwise. Only the near ones get an icon
        // (a town has dozens of lanterns); a selected one always does.
        const glm::vec3 eye = m_camera.GetPosition();
        int budget = 160;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        m_scene->ForEachComponent<LightComponent>([&](LightComponent &light) {
            GameObject *owner = light.GetOwner();
            ImVec2 screen;
            if (!owner || budget <= 0)
            {
                return;
            }
            const glm::vec3 position = owner->GetWorldPosition();
            const float distance = glm::length(position - eye);
            const bool selected = m_document->IsSelected(owner->GetID());
            const bool sun = light.GetType() == LightType::Directional;
            // Icons shrink with distance, as Unity's do, and go when they would be specks.
            const float size = sun || selected ? 22.0f : std::clamp(16.0f * 40.0f / std::max(distance, 1.0f), 0.0f, 16.0f);
            if (size < 7.0f || !painter.Point(position, screen))
            {
                return;
            }
            --budget;
            const glm::vec3 color = glm::clamp(light.GetColor(), glm::vec3(0.0f), glm::vec3(1.0f));
            const ImU32 tint = IM_COL32(static_cast<int>(color.r * 255), static_cast<int>(color.g * 255), static_cast<int>(color.b * 255), 255);
            const Icon icon = sun ? Icon::Sun : light.GetType() == LightType::Spot ? Icon::Spot : Icon::Bulb;
            draw.AddCircleFilled(screen, size * 0.62f, IM_COL32(20, 22, 26, 140));
            DrawIcon(draw, icon, screen, size, selected ? kSelection : IconColor(icon));
            if (light.GetType() != LightType::Point && (selected || sun))
            {
                const float length = std::max(distance, 1.0f) * 0.08f;
                painter.Line(position, position + light.GetDirection() * length, tint, 2.0f);
            }
        });
        m_scene->ForEachComponent<CameraComponent>([&](CameraComponent &component) {
            GameObject *owner = component.GetOwner();
            ImVec2 screen;
            if (owner && painter.Point(owner->GetWorldPosition(), screen))
            {
                draw.AddCircleFilled(screen, 13.0f, IM_COL32(20, 22, 26, 150));
                DrawIcon(draw, Icon::Camera, screen, 20.0f, m_document->IsSelected(owner->GetID()) ? kSelection : IconColor(Icon::Camera));
                const float dx = mouse.x - screen.x;
                const float dy = mouse.y - screen.y;
                if (dx * dx + dy * dy < 400.0f)
                {
                    draw.AddText(ImVec2(screen.x + 14, screen.y - 7), Theme::Text, owner->GetName().c_str());
                }
            }
        });
    }

    if (m_scene->GetObjects().empty())
    {
        const char *hint = "Empty scene. Drag a model from Assets, or create objects with + in the Hierarchy.";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        draw.AddText(ImVec2((low.x + high.x - size.x) * 0.5f, (low.y + high.y) * 0.5f + 30.0f), Theme::TextDim, hint);
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

// A strip over the top-left of the viewport, as in Godot's 3D view: view options and the
// camera speed. And the orientation gizmo in the top-right corner.
void EditorApp::DrawViewportToolbar()
{
    if (m_stage == Stage::ProjectManager)
    {
        return;
    }
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    const ImVec2 low(base.x + m_viewportPos.x, base.y + m_viewportPos.y);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(low.x + 8.0f, low.y + 8.0f));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##ViewportToolbar", nullptr, flags))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.11f, 0.12f, 0.14f, 0.82f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.11f, 0.12f, 0.14f, 0.82f));
        if (ImGui::Button(m_document->IsPlaying() ? "Game" : "Perspective"))
        {
            ImGui::OpenPopup("##viewOptions");
        }
        ImGui::SetItemTooltip("View options");
        if (ImGui::BeginPopup("##viewOptions"))
        {
            ImGui::MenuItem("Grid", nullptr, &m_showGrid);
            ImGui::MenuItem("Light and Camera Icons", nullptr, &m_showMarkers);
            ImGui::MenuItem("Preview Sun", nullptr, &m_previewSun);
            ImGui::SetItemTooltip("Lights scenes that have no directional light of their own while editing");
            bool stats = Engine::GetInstance().GetDebugOverlay().IsVisible();
            if (ImGui::MenuItem("Engine Stats", "F1", &stats))
            {
                Engine::GetInstance().GetDebugOverlay().SetVisible(stats);
            }
            ImGui::Separator();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("Field of view", &m_camera.fieldOfViewDegrees, 20.0f, 110.0f, "%.0f deg");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##speed", &m_camera.moveSpeed, 1.0f, 200.0f, "speed %.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Fly speed (right mouse + WASD)");
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (!m_document->IsPlaying())
    {
        DrawOrientationGizmo(*ImGui::GetForegroundDrawList(ImGui::GetMainViewport()));
    }
}

// The axes as the camera sees them; click one to look along it.
void EditorApp::DrawOrientationGizmo(ImDrawList &draw)
{
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    const float radius = 34.0f;
    const ImVec2 center(base.x + m_viewportPos.x + m_viewportSize.x - radius - 16.0f, base.y + m_viewportPos.y + radius + 16.0f);
    if (m_viewportSize.x < 200.0f || m_viewportSize.y < 150.0f)
    {
        return;
    }
    const glm::mat4 view = m_camera.GetViewMatrix();
    struct AxisEnd
    {
        ImVec2 screen;
        float depth;
        int axis;
        bool positive;
    };
    std::vector<AxisEnd> ends;
    for (int axis = 0; axis < 3; ++axis)
    {
        for (int sign = 0; sign < 2; ++sign)
        {
            glm::vec3 direction(0.0f);
            direction[axis] = sign == 0 ? 1.0f : -1.0f;
            const glm::vec3 v = glm::vec3(view * glm::vec4(direction, 0.0f));
            ends.push_back({ImVec2(center.x + v.x * radius, center.y - v.y * radius), v.z, axis, sign == 0});
        }
    }
    // Far ends first so near ones draw on top.
    std::sort(ends.begin(), ends.end(), [](const AxisEnd &a, const AxisEnd &b) { return a.depth < b.depth; });

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float dx = mouse.x - center.x;
    const float dy = mouse.y - center.y;
    const bool near = dx * dx + dy * dy < (radius + 12.0f) * (radius + 12.0f) && !ImGui::GetIO().WantCaptureMouse;
    m_orientationHovered = near;
    if (near)
    {
        draw.AddCircleFilled(center, radius + 12.0f, IM_COL32(255, 255, 255, 18));
    }
    static const char *names[3] = {"X", "Y", "Z"};
    int clicked = -1;
    for (size_t i = 0; i < ends.size(); ++i)
    {
        const AxisEnd &end = ends[i];
        const ImU32 color = kAxisColors[end.axis];
        const float hx = mouse.x - end.screen.x;
        const float hy = mouse.y - end.screen.y;
        const bool hovered = near && hx * hx + hy * hy < 81.0f;
        if (end.positive)
        {
            draw.AddLine(center, end.screen, color, 2.0f);
            draw.AddCircleFilled(end.screen, 8.0f, hovered ? IM_COL32(255, 255, 255, 255) : color);
            const ImVec2 size = ImGui::CalcTextSize(names[end.axis]);
            draw.AddText(ImVec2(end.screen.x - size.x * 0.5f, end.screen.y - size.y * 0.5f), IM_COL32(20, 22, 26, 255), names[end.axis]);
        }
        else
        {
            draw.AddCircleFilled(end.screen, 6.0f, hovered ? IM_COL32(255, 255, 255, 200) : WithAlpha(color, 0.35f));
            draw.AddCircle(end.screen, 6.0f, WithAlpha(color, 0.8f), 0, 1.5f);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            clicked = static_cast<int>(i);
        }
    }
    if (clicked < 0)
    {
        return;
    }
    // Look from that end of the axis towards the pivot, keeping the distance.
    const AxisEnd &end = ends[static_cast<size_t>(clicked)];
    const glm::vec3 pivot = m_camera.GetPivot();
    const float distance = glm::length(pivot - m_camera.GetPosition());
    float yaw = m_camera.GetYaw();
    float pitch = 0.0f;
    if (end.axis == 0)
    {
        yaw = end.positive ? 90.0f : -90.0f;
    }
    else if (end.axis == 2)
    {
        yaw = end.positive ? 0.0f : 180.0f;
    }
    else
    {
        pitch = end.positive ? -89.0f : 89.0f;
    }
    m_camera.SetAngles(yaw, pitch);
    m_camera.SetPosition(pivot - m_camera.GetForward() * distance);
}

} // namespace Geni::Editor
