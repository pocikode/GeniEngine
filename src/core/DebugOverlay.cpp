#include "core/DebugOverlay.h"
#include "core/Log.h"
#include "core/Profiler.h"

#if defined(GENI_ENABLE_IMGUI)

#include "rhi/Device.h"
#include <backends/imgui_impl_glfw.h>
#include <imgui.h>
#include <vector>

namespace Geni
{

bool DebugOverlay::Init(GLFWwindow *window, rhi::Device *device)
{
    if (m_initialized)
    {
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    // No imgui.ini by default; an application that wants saved layouts sets its own path.
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    // install_callbacks = true chains to the callbacks Engine::Init registered earlier.
    if (!device || !ImGui_ImplGlfw_InitForVulkan(window, true))
    {
        ImGui::DestroyContext();
        return false;
    }

    if (!device->InitImGui())
    {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    m_device = device;
    m_initialized = true;
    return true;
}

void DebugOverlay::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    m_device->ShutdownImGui();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_device = nullptr;
    m_initialized = false;
    m_frameStarted = false;
}

bool DebugOverlay::IsInitialized() const
{
    return m_initialized;
}

void DebugOverlay::BeginFrame()
{
    if (!m_initialized)
    {
        return;
    }

    m_device->NewImGuiFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_frameStarted = true;

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
    {
        m_visible = !m_visible;
    }
}

void DebugOverlay::Render(const FrameStats &stats, rhi::CommandList *cmd)
{
    if (!m_initialized || !m_frameStarted)
    {
        return;
    }

    GENI_PROFILE_SCOPE("DebugOverlay::Render");

    m_frameTimes[m_frameTimeCursor] = static_cast<float>(stats.frameMs);
    m_frameTimeCursor = (m_frameTimeCursor + 1) % m_frameTimes.size();

    if (m_visible)
    {
        DrawStatsWindow(stats);
    }

    ImGui::Render();
    if (cmd)
    {
        m_device->RenderImGui(*cmd);
    }
    m_frameStarted = false;
}

void DebugOverlay::DrawStatsWindow(const FrameStats &stats)
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);

    if (!ImGui::Begin("GeniEngine (F1)", &m_visible, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("%.1f fps   %.2f ms/frame", stats.fps, stats.frameMs);
    ImGui::PlotLines(
        "##frametimes", m_frameTimes.data(), static_cast<int>(m_frameTimes.size()), static_cast<int>(m_frameTimeCursor),
        nullptr, 0.0f, 33.3f, ImVec2(-1.0f, 48.0f)
    );

    ImGui::Text("Simulate %.2f ms   Render %.2f ms", stats.simulateMs, stats.renderMs);
    ImGui::Text("Fixed steps %d   alpha %.2f", stats.fixedSteps, stats.interpolationAlpha);
    if (stats.droppedSimulationSeconds > 0.0)
    {
        ImGui::TextColored(
            ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Simulation behind: dropped %.0f ms",
            stats.droppedSimulationSeconds * 1000.0
        );
    }
    ImGui::Text("Draw calls %d   Frame %llu", stats.drawCalls, static_cast<unsigned long long>(stats.frameIndex));
    ImGui::Text(
        "Objects %d submitted, %d visible, %d culled", stats.submittedObjects, stats.visibleObjects, stats.culledObjects
    );
    ImGui::Text("Instances %d   Shadow draws %d", stats.instancesDrawn, stats.shadowDraws);
    ImGui::Text("Clustered lights %d   Overflow %d", stats.clusteredLights, stats.overflowLights);

#if defined(GENI_ENABLE_TRACY)
    ImGui::TextDisabled("Tracy: enabled (on demand)");
#else
    ImGui::TextDisabled("Tracy: not compiled in");
#endif

    if (ImGui::CollapsingHeader("Log"))
    {
        static std::vector<LogMessage> messages;
        static size_t seenCount = 0;
        const size_t count = Log::GetMessageCount();
        const bool changed = count != seenCount;
        if (changed)
        {
            messages = Log::GetRecent(100);
            seenCount = count;
        }

        ImGui::BeginChild("##log", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto &message : messages)
        {
            ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
            if (message.level >= LogLevel::Error)
            {
                color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            }
            else if (message.level == LogLevel::Warn)
            {
                color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            }
            else if (message.level <= LogLevel::Debug)
            {
                color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(Log::FormatLine(message).c_str());
            ImGui::PopStyleColor();
        }
        if (changed)
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

void DebugOverlay::SetVisible(bool visible)
{
    m_visible = visible;
}

bool DebugOverlay::IsVisible() const
{
    return m_visible;
}

bool DebugOverlay::WantsMouse() const
{
    return m_initialized && ImGui::GetIO().WantCaptureMouse;
}

bool DebugOverlay::WantsKeyboard() const
{
    return m_initialized && ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace Geni

#else

namespace Geni
{

bool DebugOverlay::Init(GLFWwindow *, rhi::Device *)
{
    return false;
}

void DebugOverlay::Shutdown()
{
}

bool DebugOverlay::IsInitialized() const
{
    return false;
}

void DebugOverlay::BeginFrame()
{
}

void DebugOverlay::Render(const FrameStats &, rhi::CommandList *)
{
}

void DebugOverlay::DrawStatsWindow(const FrameStats &)
{
}

void DebugOverlay::SetVisible(bool visible)
{
    m_visible = visible;
}

bool DebugOverlay::IsVisible() const
{
    return m_visible;
}

bool DebugOverlay::WantsMouse() const
{
    return false;
}

bool DebugOverlay::WantsKeyboard() const
{
    return false;
}

} // namespace Geni

#endif
