// The Timeline panel (docs/design/0026): scrub, preview, key and edit the transform
// tracks of the selection's timeline. Every edit goes through EditorDocument::SetProperty
// on the component's `tracks`, so it undoes like any other change.

#include "EditorApp.h"
#include "EditorTheme.h"

#include "../core/TimelineEditing.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Geni::Editor
{

GameObject *EditorApp::FindTimelineOwner(int &componentIndex)
{
    for (GameObject *object = m_document->GetPrimary(); object; object = object->GetParent())
    {
        const auto &components = object->GetComponents();
        for (size_t i = 0; i < components.size(); ++i)
        {
            if (components[i]->GetTypeID() == TimelineComponent::TypeID())
            {
                componentIndex = static_cast<int>(i);
                return object;
            }
        }
    }
    componentIndex = -1;
    return nullptr;
}

bool EditorApp::CommitTimeline(GameObject &owner, int componentIndex, const TimelineData &data)
{
    return m_document->SetProperty(owner, componentIndex, "tracks", nlohmann::json(TimelineToJson(data).dump()));
}

void EditorApp::DrawTimelinePanel()
{
    if (!m_showTimeline)
    {
        return;
    }
    if (!ImGui::Begin("Timeline", &m_showTimeline))
    {
        ImGui::End();
        return;
    }
    int componentIndex = -1;
    GameObject *owner = FindTimelineOwner(componentIndex);
    if (!owner)
    {
        m_timelinePreview = false;
        ImGui::TextDisabled("Select an object with a TimelineComponent (Add Component > TimelineComponent).");
        ImGui::End();
        return;
    }
    auto *timeline =
        static_cast<TimelineComponent *>(owner->GetComponents()[static_cast<size_t>(componentIndex)].get());
    const TimelineData &data = timeline->GetData();
    const float length = std::max(timeline->GetLength(), 5.0f);

    // Preview: advance the playhead with the frame time; no sounds, cuts or events.
    if (m_timelinePreview)
    {
        float time = timeline->GetTime() + ImGui::GetIO().DeltaTime * timeline->GetSpeed();
        if (time >= timeline->GetLength())
        {
            time = timeline->GetLoop() && timeline->GetLength() > 0.0f ? std::fmod(time, timeline->GetLength())
                                                                       : timeline->GetLength();
            m_timelinePreview = timeline->GetLoop();
        }
        timeline->SetTime(time);
    }

    // Toolbar.
    if (ImGui::Button(m_timelinePreview ? "Pause" : "Play"))
    {
        m_timelinePreview = !m_timelinePreview;
        if (m_timelinePreview && timeline->GetTime() >= timeline->GetLength())
        {
            timeline->SetTime(0.0f);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("|<"))
    {
        timeline->SetTime(0.0f);
    }
    ImGui::SameLine();
    float time = timeline->GetTime();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::DragFloat("##time", &time, 0.01f, 0.0f, length, "%.2f s"))
    {
        timeline->SetTime(time);
    }
    GameObject *selected = m_document->GetPrimary();
    ImGui::SameLine();
    ImGui::BeginDisabled(!selected || selected == owner);
    if (ImGui::Button("Key selection"))
    {
        TimelineData edited = data;
        TimelineKey key;
        key.time = timeline->GetTime();
        key.position = selected->GetPosition();
        key.rotation = selected->GetRotation();
        key.scale = selected->GetScale();
        SetTimelineKey(edited, selected->GetName(), key);
        CommitTimeline(*owner, componentIndex, edited);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s  ·  %.2f s", owner->GetName().c_str(), timeline->GetLength());

    // Ruler and tracks.
    const float labelWidth = 150.0f;
    const float rowHeight = 22.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x - labelWidth, 50.0f);
    const float rows = static_cast<float>(data.transforms.size()) + 1.0f;
    ImDrawList &draw = *ImGui::GetWindowDrawList();
    auto xOf = [&](float t) { return origin.x + labelWidth + t / length * width; };
    auto timeOf = [&](float x) { return std::clamp((x - origin.x - labelWidth) / width * length, 0.0f, length); };

    // The ruler: drag to scrub.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + labelWidth, origin.y));
    ImGui::InvisibleButton("##ruler", ImVec2(width, rowHeight));
    if (ImGui::IsItemActive())
    {
        timeline->SetTime(timeOf(ImGui::GetIO().MousePos.x));
        m_timelinePreview = false;
    }
    draw.AddRectFilled(
        ImVec2(origin.x + labelWidth, origin.y), ImVec2(origin.x + labelWidth + width, origin.y + rowHeight),
        Theme::PanelDark
    );
    for (int second = 0; second <= static_cast<int>(length); ++second)
    {
        const float x = xOf(static_cast<float>(second));
        draw.AddLine(ImVec2(x, origin.y + rowHeight * 0.55f), ImVec2(x, origin.y + rowHeight), Theme::TextDim);
        char label[16];
        std::snprintf(label, sizeof(label), "%d", second);
        draw.AddText(ImVec2(x + 3.0f, origin.y + 2.0f), Theme::TextDim, label);
    }

    TimelineData edited = data;
    bool changed = false;
    for (size_t t = 0; t < data.transforms.size(); ++t)
    {
        const TimelineTrack &track = data.transforms[t];
        const float y = origin.y + rowHeight * static_cast<float>(t + 1);
        draw.AddText(ImVec2(origin.x + 4.0f, y + 3.0f), Theme::Text, track.target.c_str());
        draw.AddLine(
            ImVec2(origin.x + labelWidth, y + rowHeight * 0.5f),
            ImVec2(origin.x + labelWidth + width, y + rowHeight * 0.5f), Theme::AccentDim
        );
        for (size_t k = 0; k < track.keys.size(); ++k)
        {
            const float x = xOf(track.keys[k].time);
            const ImVec2 centre(x, y + rowHeight * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(x - 6.0f, y + 3.0f));
            ImGui::PushID(static_cast<int>(t * 10000 + k));
            ImGui::InvisibleButton("##key", ImVec2(12.0f, rowHeight - 6.0f));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                timeline->SetTime(track.keys[k].time);
                m_timelineDragTrack = static_cast<int>(t);
                m_timelineDragKey = static_cast<int>(k);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                RemoveTimelineKey(edited, static_cast<int>(t), static_cast<int>(k));
                changed = true;
            }
            ImGui::PopID();
            const float r = hovered ? 6.0f : 5.0f;
            draw.AddQuadFilled(
                ImVec2(centre.x, centre.y - r), ImVec2(centre.x + r, centre.y), ImVec2(centre.x, centre.y + r),
                ImVec2(centre.x - r, centre.y), hovered ? Theme::Warning : Theme::Accent
            );
        }
    }
    // Dragging a key: preview its new time while held, commit once when released.
    if (m_timelineDragKey >= 0)
    {
        const float dragTime = timeOf(ImGui::GetIO().MousePos.x);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            draw.AddLine(
                ImVec2(xOf(dragTime), origin.y), ImVec2(xOf(dragTime), origin.y + rowHeight * rows), Theme::Warning
            );
        }
        else
        {
            const auto &keys = data.transforms[static_cast<size_t>(m_timelineDragTrack)].keys;
            if (m_timelineDragKey < static_cast<int>(keys.size()) &&
                std::abs(keys[static_cast<size_t>(m_timelineDragKey)].time - dragTime) > TimelineKeySnap)
            {
                MoveTimelineKey(edited, m_timelineDragTrack, m_timelineDragKey, dragTime);
                timeline->SetTime(dragTime);
                changed = true;
            }
            m_timelineDragTrack = -1;
            m_timelineDragKey = -1;
        }
    }
    // The playhead.
    const float playhead = xOf(std::min(timeline->GetTime(), length));
    draw.AddLine(ImVec2(playhead, origin.y), ImVec2(playhead, origin.y + rowHeight * rows), Theme::Error, 2.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowHeight * rows + 6.0f));
    ImGui::TextDisabled("Click a key to jump to it, drag to move it, right-click to delete it.");
    if (changed)
    {
        CommitTimeline(*owner, componentIndex, edited);
    }
    ImGui::End();
}

} // namespace Geni::Editor
