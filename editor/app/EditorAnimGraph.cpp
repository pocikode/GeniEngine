// The Animation Graph panel (docs/design/0028): states as boxes, transitions as arrows,
// the selection edited on the right. Edits go to AnimGraphDocument (the file's JSON) and
// are checked by the runtime's parser before they can be saved.

#include "EditorApp.h"
#include "EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Geni::Editor
{

namespace
{

constexpr float kNodeWidth = 150.0f;
constexpr float kNodeHeight = 46.0f;

ImVec2 ToIm(const glm::vec2 &v)
{
    return ImVec2(v.x, v.y);
}

// Where the segment from the centre of a box towards `towards` leaves the box.
glm::vec2 BoxEdge(const glm::vec2 &centre, const glm::vec2 &towards)
{
    const glm::vec2 d = towards - centre;
    if (std::abs(d.x) < 1e-3f && std::abs(d.y) < 1e-3f)
    {
        return centre;
    }
    const float sx = std::abs(d.x) > 1e-3f ? (kNodeWidth * 0.5f) / std::abs(d.x) : 1e9f;
    const float sy = std::abs(d.y) > 1e-3f ? (kNodeHeight * 0.5f) / std::abs(d.y) : 1e9f;
    return centre + d * std::min(sx, sy);
}

void Arrow(ImDrawList &draw, const glm::vec2 &a, const glm::vec2 &b, ImU32 colour, float thickness)
{
    draw.AddLine(ToIm(a), ToIm(b), colour, thickness);
    const glm::vec2 d = glm::normalize(b - a);
    const glm::vec2 n(-d.y, d.x);
    draw.AddTriangleFilled(ToIm(b), ToIm(b - d * 10.0f + n * 5.0f), ToIm(b - d * 10.0f - n * 5.0f), colour);
}

float DistanceToSegment(const glm::vec2 &p, const glm::vec2 &a, const glm::vec2 &b)
{
    const glm::vec2 ab = b - a;
    const float t = glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
    return glm::length(p - (a + ab * t));
}

} // namespace

bool EditorApp::OpenAnimGraph(const std::string &path)
{
    std::ifstream in(m_project.GetConfig().assets / path);
    std::stringstream text;
    text << in.rdbuf();
    const auto document = nlohmann::ordered_json::parse(text.str(), nullptr, false);
    AnimGraphDocument graph;
    std::string error;
    if (!in || document.is_discarded() || !graph.Load(document, &error))
    {
        m_document->SetStatus("Could not open " + path + (error.empty() ? "" : ": " + error));
        return false;
    }
    m_graph = std::move(graph);
    m_graphPath = path;
    m_graphSaved = m_graph.GetJson();
    m_graphHistory.clear();
    m_graphState.clear();
    m_graphTransition = -1;
    m_graphPan = glm::vec2(0.0f);
    m_showAnimGraph = true;
    return true;
}

bool EditorApp::SaveAnimGraph()
{
    const std::string error = m_graph.Validate();
    if (!error.empty())
    {
        m_document->SetStatus("Not saved: " + error);
        return false;
    }
    std::ofstream out(m_project.GetConfig().assets / m_graphPath);
    out << m_graph.GetJson().dump(2) << '\n';
    if (!out)
    {
        m_document->SetStatus("Could not save " + m_graphPath);
        return false;
    }
    m_graphSaved = m_graph.GetJson();
    m_document->SetStatus("Saved " + m_graphPath);
    return true;
}

void EditorApp::RememberAnimGraph()
{
    m_graphHistory.push_back(m_graph.GetJson());
    if (m_graphHistory.size() > 200)
    {
        m_graphHistory.erase(m_graphHistory.begin());
    }
}

void EditorApp::DrawAnimGraphPanel()
{
    if (!m_showAnimGraph || m_graphPath.empty())
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(900.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Graph", &m_showAnimGraph))
    {
        ImGui::End();
        return;
    }
    const bool dirty = m_graph.GetJson() != m_graphSaved;
    const std::string problem = m_graph.Validate();

    // Toolbar.
    ImGui::TextUnformatted(m_graphPath.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty || !problem.empty());
    if (ImGui::Button("Save"))
    {
        SaveAnimGraph();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Revert"))
    {
        const std::string path = m_graphPath;
        OpenAnimGraph(path);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_graphHistory.empty());
    const bool undo = ImGui::Button("Undo") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                                                ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false));
    ImGui::EndDisabled();
    if (undo && !m_graphHistory.empty())
    {
        m_graph.Load(m_graphHistory.back());
        m_graphHistory.pop_back();
        m_graphTransition = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add State"))
    {
        RememberAnimGraph();
        std::string name = "state";
        for (int n = 2; m_graph.GetState(name); ++n)
        {
            name = "state" + std::to_string(n);
        }
        m_graph.AddState(name, "Idle");
        m_graph.SetPosition(name, glm::vec2(80.0f, 60.0f) - m_graphPan);
        m_graphState = name;
        m_graphTransition = -1;
    }
    if (!problem.empty())
    {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", problem.c_str());
    }
    else if (dirty)
    {
        ImGui::TextDisabled("Unsaved changes");
    }

    // The canvas on the left, the selection on the right.
    const float sideWidth = 280.0f;
    const ImVec2 canvasSize(
        std::max(ImGui::GetContentRegionAvail().x - sideWidth - 8.0f, 100.0f),
        std::max(ImGui::GetContentRegionAvail().y, 100.0f)
    );
    ImGui::BeginChild("##canvas", canvasSize, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImDrawList &draw = *ImGui::GetWindowDrawList();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const glm::vec2 origin = glm::vec2(corner.x, corner.y) + m_graphPan;
    draw.AddRectFilled(corner, ImVec2(corner.x + canvasSize.x, corner.y + canvasSize.y), Theme::PanelDark);
    ImGui::InvisibleButton(
        "##background", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle
    );
    const bool canvasHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
    {
        m_graphPan += glm::vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
    }
    const glm::vec2 mouse(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);

    const std::vector<std::string> states = m_graph.GetStates();
    auto centreOf = [&](const std::string &state) {
        return origin + m_graph.GetPosition(state) + glm::vec2(kNodeWidth, kNodeHeight) * 0.5f;
    };
    const glm::vec2 anyCentre = origin + glm::vec2(60.0f, 30.0f);
    bool anyUsed = false;

    // States.
    const std::string initial = m_graph.GetInitial();
    std::string hoveredState;
    for (const std::string &state : states)
    {
        const glm::vec2 low = origin + m_graph.GetPosition(state);
        const glm::vec2 high = low + glm::vec2(kNodeWidth, kNodeHeight);
        const bool hovered =
            canvasHovered && mouse.x >= low.x && mouse.y >= low.y && mouse.x <= high.x && mouse.y <= high.y;
        if (hovered)
        {
            hoveredState = state;
        }
        const bool selected = state == m_graphState;
        draw.AddRectFilled(ToIm(low), ToIm(high), selected ? Theme::Accent : IM_COL32(48, 52, 62, 255), 8.0f);
        draw.AddRect(
            ToIm(low), ToIm(high), state == initial ? Theme::Warning : IM_COL32(90, 96, 110, 255), 8.0f, 0,
            state == initial ? 2.0f : 1.0f
        );
        draw.AddText(ToIm(low + glm::vec2(10, 6)), Theme::Text, state.c_str());
        const auto *json = m_graph.GetState(state);
        std::string detail = json && json->contains("blend")
                                 ? "blend: " + json->at("blend").value("parameter", std::string())
                             : json ? json->value("clip", std::string())
                                    : std::string();
        draw.AddText(ToIm(low + glm::vec2(10, 24)), Theme::TextDim, detail.c_str());
    }

    // Transitions over the boxes, so their heads show. Two arrows between the same pair are offset.
    const std::vector<AnimTransitionView> transitions = m_graph.GetTransitions();
    int hoveredTransition = -1;
    for (size_t i = 0; i < transitions.size(); ++i)
    {
        const AnimTransitionView &t = transitions[i];
        anyUsed = anyUsed || t.from == "*";
        const glm::vec2 a = t.from == "*" ? anyCentre : centreOf(t.from);
        const glm::vec2 b = centreOf(t.to);
        const glm::vec2 d = glm::length(b - a) > 1e-3f ? glm::normalize(b - a) : glm::vec2(1, 0);
        const glm::vec2 side = glm::vec2(-d.y, d.x) * 7.0f;
        const glm::vec2 from = BoxEdge(a, b) + side;
        const glm::vec2 to = BoxEdge(b, a) + side;
        const bool selected = static_cast<int>(i) == m_graphTransition;
        if (canvasHovered && DistanceToSegment(mouse, from, to) < 6.0f)
        {
            hoveredTransition = static_cast<int>(i);
        }
        Arrow(
            draw, from, to,
            selected ? Theme::Warning : (hoveredTransition == static_cast<int>(i) ? Theme::Text : Theme::TextDim),
            selected ? 2.5f : 1.5f
        );
    }
    if (anyUsed)
    {
        draw.AddRectFilled(
            ToIm(anyCentre - glm::vec2(40, 16)), ToIm(anyCentre + glm::vec2(40, 16)), Theme::AccentDim, 8.0f
        );
        draw.AddText(ToIm(anyCentre - glm::vec2(12, 7)), Theme::Text, "Any");
    }

    // Clicks: a state (select, start dragging) or a transition (select); empty space clears.
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (!hoveredState.empty())
        {
            m_graphState = hoveredState;
            m_graphTransition = -1;
            m_graphDragging = hoveredState;
            RememberAnimGraph();
            std::snprintf(m_graphName, sizeof(m_graphName), "%s", hoveredState.c_str());
        }
        else if (hoveredTransition >= 0)
        {
            m_graphTransition = hoveredTransition;
            m_graphState.clear();
            std::snprintf(
                m_graphConditions, sizeof(m_graphConditions), "%s",
                transitions[static_cast<size_t>(hoveredTransition)].conditions.c_str()
            );
        }
        else
        {
            m_graphState.clear();
            m_graphTransition = -1;
        }
    }
    if (!m_graphDragging.empty())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_graph.SetPosition(m_graphDragging, m_graph.GetPosition(m_graphDragging) + glm::vec2(delta.x, delta.y));
        }
        else
        {
            // A click without movement leaves no undo step.
            if (!m_graphHistory.empty() && m_graphHistory.back() == m_graph.GetJson())
            {
                m_graphHistory.pop_back();
            }
            m_graphDragging.clear();
        }
    }
    ImGui::EndChild();

    // The selection.
    ImGui::SameLine();
    ImGui::BeginChild("##selection", ImVec2(sideWidth, canvasSize.y));
    if (!m_graphState.empty() && m_graph.GetState(m_graphState))
    {
        const auto state = *m_graph.GetState(m_graphState);
        ImGui::TextDisabled("State");
        if (ImGui::InputText("Name", m_graphName, sizeof(m_graphName), ImGuiInputTextFlags_EnterReturnsTrue) &&
            m_graphName != m_graphState)
        {
            RememberAnimGraph();
            if (m_graph.RenameState(m_graphState, m_graphName))
            {
                m_graphState = m_graphName;
            }
            else
            {
                m_graphHistory.pop_back();
            }
        }
        if (state.contains("blend"))
        {
            ImGui::TextWrapped(
                "Blend on '%s': %s", state["blend"].value("parameter", std::string()).c_str(),
                state["blend"].value("clips", nlohmann::ordered_json::array()).dump().c_str()
            );
        }
        char clip[64];
        std::snprintf(clip, sizeof(clip), "%s", state.value("clip", std::string()).c_str());
        if (ImGui::InputText("Clip", clip, sizeof(clip), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            RememberAnimGraph();
            m_graph.SetStateClip(m_graphState, clip);
        }
        bool loop = state.value("loop", true);
        if (ImGui::Checkbox("Loop", &loop))
        {
            RememberAnimGraph();
            m_graph.SetStateLoop(m_graphState, loop);
        }
        float speed = state.value("speed", 1.0f);
        if (ImGui::DragFloat("Speed", &speed, 0.01f, 0.0f, 10.0f))
        {
            m_graph.SetStateSpeed(m_graphState, speed);
        }
        if (ImGui::IsItemActivated())
        {
            RememberAnimGraph();
        }
        ImGui::BeginDisabled(m_graphState == m_graph.GetInitial());
        if (ImGui::Button("Make Initial"))
        {
            RememberAnimGraph();
            m_graph.SetInitial(m_graphState);
        }
        ImGui::EndDisabled();
        if (ImGui::BeginCombo("##to", "Add transition to..."))
        {
            for (const std::string &other : states)
            {
                if (other != m_graphState && ImGui::Selectable(other.c_str()))
                {
                    RememberAnimGraph();
                    m_graphTransition = m_graph.AddTransition(m_graphState, other);
                    m_graphConditions[0] = '\0';
                    m_graphState.clear();
                }
            }
            ImGui::EndCombo();
        }
        if (!m_graphState.empty() && ImGui::Button("Delete State"))
        {
            RememberAnimGraph();
            if (!m_graph.RemoveState(m_graphState))
            {
                m_graphHistory.pop_back();
            }
            m_graphState.clear();
        }
    }
    else if (m_graphTransition >= 0 && m_graphTransition < static_cast<int>(m_graph.GetTransitions().size()))
    {
        AnimTransitionView view = m_graph.GetTransitions()[static_cast<size_t>(m_graphTransition)];
        ImGui::TextDisabled("Transition");
        ImGui::Text("%s -> %s", view.from == "*" ? "Any" : view.from.c_str(), view.to.c_str());
        bool changed = false;
        bool started = false;
        changed |= ImGui::DragFloat("Duration", &view.duration, 0.01f, 0.0f, 5.0f, "%.2f s");
        started |= ImGui::IsItemActivated();
        bool exit = view.exitTime >= 0.0f;
        if (ImGui::Checkbox("Exit time", &exit))
        {
            view.exitTime = exit ? 0.9f : -1.0f;
            changed = started = true;
        }
        if (exit)
        {
            changed |= ImGui::SliderFloat("##exit", &view.exitTime, 0.0f, 1.0f, "%.2f");
            started |= ImGui::IsItemActivated();
        }
        ImGui::TextDisabled("Conditions, e.g. grounded == true; jump");
        if (ImGui::InputText(
                "##conditions", m_graphConditions, sizeof(m_graphConditions), ImGuiInputTextFlags_EnterReturnsTrue
            ))
        {
            view.conditions = m_graphConditions;
            changed = started = true;
        }
        if (started)
        {
            RememberAnimGraph();
        }
        if (changed)
        {
            std::string error;
            if (!m_graph.SetTransition(m_graphTransition, view, &error))
            {
                m_document->SetStatus(error);
            }
        }
        if (ImGui::Button("Delete Transition"))
        {
            RememberAnimGraph();
            m_graph.RemoveTransition(m_graphTransition);
            m_graphTransition = -1;
        }
    }
    else
    {
        ImGui::TextDisabled(
            "Click a state or an arrow.\nDrag states to arrange them;\ndrag with the right or middle\nbutton to pan."
        );
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace Geni::Editor
