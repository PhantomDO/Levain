#include "panels.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>

#include <imgui.h>
// DockBuilder est une API interne d'ImGui (ADR-0032) : elle peut changer d'une version à l'autre,
// et vcpkg fige la version.
#include <imgui_internal.h>

#include "levain/app/app.hpp"
#include "levain/render/renderer.hpp"

namespace levain::app
{

namespace
{

constexpr const char* ImageWindow = "Image";
constexpr const char* PassesWindow = "Passes";
constexpr const char* SceneWindow = "Scène";

/// La disposition de départ, recréée à chaque lancement (pas d'`imgui.ini`) : Image et Passes à
/// gauche, l'une sur l'autre ; Scène à droite, sur un nœud encore vide, celui de l'inspecteur ; le
/// centre libre, la scène s'y voit. Rend les nœuds où l'éditeur ancre ses fenêtres.
DockNodes buildLayout(ImGuiID dockspace, ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);
    ImGuiID center = dockspace;
    const ImGuiID left =
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, nullptr, &center);
    const ImGuiID right =
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    ImGuiID leftBottom = 0;
    const ImGuiID leftTop =
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5f, nullptr, &leftBottom);
    ImGuiID rightBottom = 0;
    const ImGuiID rightTop =
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.4f, nullptr, &rightBottom);
    ImGui::DockBuilderDockWindow(ImageWindow, leftTop);
    ImGui::DockBuilderDockWindow(PassesWindow, leftBottom);
    ImGui::DockBuilderDockWindow(SceneWindow, rightTop);
    ImGui::DockBuilderFinish(dockspace);
    return {.left = leftTop, .inspector = rightBottom};
}

/// Les dernières images, dans l'ordre : l'historique est un anneau, `next` en est le plus ancien.
void plotHistory(const char* label, const std::array<float, HistoryLength>& values,
                 std::size_t next)
{
    const float highest = *std::ranges::max_element(values);
    ImGui::PlotLines(label, values.data(), static_cast<int>(values.size()), static_cast<int>(next),
                     nullptr, 0.0f, std::max(highest * 1.2f, 1.0f),
                     ImVec2(0.0f, 60.0f * ImGui::GetStyle().FontScaleDpi));
}

void drawImageWindow(const App& app)
{
    if (ImGui::Begin(ImageWindow))
    {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("%.0f images/s, %.2f ms", static_cast<double>(io.Framerate),
                    1000.0 / static_cast<double>(std::max(io.Framerate, 1.0f)));
        // Sous WebGPU, notre backend ne relit pas les minuteurs : « non mesuré », jamais 0.
        if (app.totalGpu.samples > 0)
        {
            ImGui::Text("GPU : %.3f ms en moyenne", render::averageOf(app.totalGpu));
        }
        else
        {
            ImGui::TextDisabled("GPU : non mesuré");
        }
        plotHistory("image (ms)", app.ui.history.frameMs, app.ui.history.next);
        if (app.totalGpu.samples > 0)
        {
            plotHistory("GPU (ms)", app.ui.history.gpuMs, app.ui.history.next);
        }
    }
    ImGui::End();
}

void drawPassesWindow(const App& app)
{
    if (ImGui::Begin(PassesWindow))
    {
        if (ImGui::BeginTable("passes", 2, ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("passe");
            ImGui::TableSetupColumn("GPU (ms)");
            ImGui::TableHeadersRow();
            const auto row = [](const char* name, const render::GpuTimeAverage& time)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name);
                ImGui::TableNextColumn();
                if (time.samples > 0)
                {
                    ImGui::Text("%.3f", render::averageOf(time));
                }
                else
                {
                    ImGui::TextDisabled("non mesuré");
                }
            };
            for (std::size_t pass = 0; pass < render::RendererPassNames.size(); ++pass)
            {
                row(render::RendererPassNames.at(pass).data(), app.renderer.passTimes.at(pass));
            }
            row("interface", app.ui.cost.gpu);
            ImGui::EndTable();
        }
        const UiCost& cost = app.ui.cost;
        ImGui::Text("interface, CPU : %.3f ms en moyenne, %.3f au pire",
                    cost.cpuSamples > 0 ? cost.cpuTotalMs / cost.cpuSamples : 0.0, cost.cpuMaxMs);
    }
    ImGui::End();
}

void drawSceneWindow(App& app)
{
    if (ImGui::Begin(SceneWindow))
    {
        const auto perFrame = [&app](std::uint64_t count)
        { return static_cast<double>(count) / std::max(app.frameCount, 1); };
        ImGui::Text("entités placées (Transform) : %d", app.world.count<scene::Transform>());
        ImGui::Text("modèles chargés : %zu", app.models.size());
        ImGui::Text("modèles, par image : %.1f dessinés, %.1f écartés",
                    perFrame(app.modelsCamera.drawn), perFrame(app.modelsCamera.culled));
        ImGui::Text("triangles des modèles, par image : %.0f",
                    perFrame(app.modelsCamera.triangles));
    }
    ImGui::End();
}

} // namespace

void drawEnginePanels(App& app)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Un identifiant qui ne dépend pas de la fenêtre en cours, au contraire de `ImGui::GetID`.
    const ImGuiID dockspace = ImHashStr("levain");
    if (ImGui::DockBuilderGetNode(dockspace) == nullptr)
    {
        app.ui.dock = buildLayout(dockspace, viewport->Size);
    }
    // Le centre laisse passer la souris et l'image : on y voit la scène, et on y vise.
    ImGui::DockSpaceOverViewport(dockspace, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
    drawImageWindow(app);
    drawPassesWindow(app);
    drawSceneWindow(app);
}

} // namespace levain::app
