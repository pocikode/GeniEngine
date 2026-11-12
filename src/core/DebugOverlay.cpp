#include "core/DebugOverlay.h"

#include "core/Log.h"
#include "core/Profiler.h"
#include <algorithm>
#include <cstdio>

#if defined(GENI_ENABLE_IMGUI)

#include "rhi/Device.h"
#include <backends/imgui_impl_glfw.h>
#include <imgui.h>
#include <vector>

namespace Geni
{

bool DebugOverlay::Init(GLFWwindow *window, rhi::Device *device, bool docking, const std::string &iniFile)
{
    if (m_initialized)
    {
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    // No imgui.ini by default; an application that wants saved layouts sets its own path.
    m_iniFile = iniFile;
    io.IniFilename = m_iniFile.empty() ? nullptr : m_iniFile.c_str();
    if (docking)
    {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }
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
    // ImGui draws into the backbuffer, so its pixel scale is backbuffer size over window
    // size. GLFW's framebuffer size is not always that: a hidden window on a Retina
    // display is created with a 1:1 backbuffer (Engine::Init) but can still report a
    // 2x framebuffer, which drew the whole UI at double size and clipped it.
    ImGuiIO &io = ImGui::GetIO();
    uint32_t width = 0;
    uint32_t height = 0;
    m_device->GetBackbufferSize(width, height);
    if (width > 0 && height > 0 && io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
    {
        io.DisplayFramebufferScale =
            ImVec2(static_cast<float>(width) / io.DisplaySize.x, static_cast<float>(height) / io.DisplaySize.y);
    }
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

// Memory against the per-platform budget and the world streamer's state
// (docs/design/0007).
void DebugOverlay::DrawStreamingSection(const FrameStats &stats)
{
    constexpr double kMiB = 1024.0 * 1024.0;
    if (!ImGui::CollapsingHeader("Memory and streaming", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    const double budget = stats.gpuBudgetBytes;
    const float fraction = budget > 0.0 ? static_cast<float>(stats.residentGpuBytes / budget) : 0.0f;
    char label[96];
    std::snprintf(label, sizeof(label), "%.1f / %.0f MiB assets", stats.residentGpuBytes / kMiB, budget / kMiB);
    ImGui::PushStyleColor(
        ImGuiCol_PlotHistogram, fraction > 1.0f ? ImVec4(0.9f, 0.3f, 0.2f, 1.0f) : ImVec4(0.3f, 0.6f, 0.9f, 1.0f)
    );
    ImGui::ProgressBar(std::min(fraction, 1.0f), ImVec2(-1.0f, 0.0f), label);
    ImGui::PopStyleColor();
    ImGui::Text("GPU allocated %.1f MiB", stats.gpuAllocatedBytes / kMiB);
    if (!stats.worldOpen)
    {
        ImGui::TextDisabled("No world open");
        return;
    }
    const float nodes = stats.maxLoadedNodes > 0 ? static_cast<float>(stats.loadedNodes) / stats.maxLoadedNodes : 0.0f;
    std::snprintf(label, sizeof(label), "%d / %d nodes", stats.loadedNodes, stats.maxLoadedNodes);
    ImGui::ProgressBar(std::min(nodes, 1.0f), ImVec2(-1.0f, 0.0f), label);
    ImGui::Text(
        "Cells %d loaded   %d loading   %d unloading   %d HLOD", stats.cellsLoaded, stats.cellsLoading,
        stats.cellsUnloading, stats.hlodCells
    );
    ImGui::Text(
        "Radius %.0f m full, %.0f m HLOD   stream %.2f ms", stats.loadRadius, stats.hlodRadius, stats.streamingMs
    );
    ImGui::Text("Camera (%.0f, %.0f, %.0f)", stats.cameraWorld[0], stats.cameraWorld[1], stats.cameraWorld[2]);
    ImGui::Text("Origin (%.0f, %.0f, %.0f)", stats.worldOrigin[0], stats.worldOrigin[1], stats.worldOrigin[2]);
    if (stats.streamingBudgetLimited)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Budget limited: nearer cells first");
    }
    if (stats.lateStreamingFrames > 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Late frames: %d", stats.lateStreamingFrames);
    }
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
    ImGui::Text(
        "Assets %d loaded   %d pending   %.1f KiB decoded", stats.assetsLoaded, stats.assetsPending,
        stats.assetBytes / 1024.0
    );
    ImGui::Text(
        "LOD instances %d / %d / %d / %d", stats.lodInstances[0], stats.lodInstances[1], stats.lodInstances[2],
        stats.lodInstances[3]
    );
    DrawStreamingSection(stats);

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

bool DebugOverlay::Init(GLFWwindow *, rhi::Device *, bool, const std::string &)
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

void DebugOverlay::DrawStreamingSection(const FrameStats &)
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
