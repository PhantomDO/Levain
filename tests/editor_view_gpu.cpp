// La disposition de l'éditeur dans la vraie boucle (ADR-0036, morceau 8) : `withEditor` autour d'un
// programme minimal, un script d'input rejoué hors écran (le banc d'essai de l'App). Chaque fenêtre
// est sur son nœud de la disposition et l'inspecteur est seul sur le sien, donc visible. Il faut un
// device, comme `levain_app_script`.
//   levain_editor_view [vulkan|d3d12|d3d12-warp]

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <imgui.h>
// Les fenêtres et leurs nœuds retrouvés par leur nom : l'API interne d'ImGui.
#include <imgui_internal.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/editor/editor.hpp"
#include "levain/scene/components.hpp"
#include "levain/ui/tr.hpp"

namespace
{

using levain::app::App;

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 720;
constexpr std::array<const char*, 6> Titles = {"Hiérarchie", "Inspecteur", "Console",
                                               "Image",      "Passes",     "Scène"};

/// Où est une fenêtre à une image : son nœud, et si son onglet se voit.
struct Placed
{
    bool exists = false;
    ImGuiID dock = 0;
    bool tabVisible = false;
    int windowsOnNode = 0;
};

/// Ce que l'éditeur a fait à une image, relevé à la fin de `ui`.
struct Seen
{
    std::array<Placed, 6> windows{};
    levain::app::DockNodes nodes{};
};

Placed placedOf(const char* french)
{
    const ImGuiWindow* window = ImGui::FindWindowByName(levain::ui::labelOf(french).c_str());
    if (window == nullptr)
    {
        return {};
    }
    return {.exists = true,
            .dock = window->DockId,
            .tabVisible = window->DockTabIsVisible,
            .windowsOnNode = window->DockNode != nullptr ? window->DockNode->Windows.Size : 0};
}

Seen seenOf(const App& app)
{
    Seen seen{.nodes = app.ui.dock};
    for (std::size_t i = 0; i < Titles.size(); ++i)
    {
        seen.windows.at(i) = placedOf(Titles.at(i));
    }
    return seen;
}

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    App* app = nullptr;
};

levain::app::StartFunction programOf(Run& run)
{
    return [&run](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        run.app = &app;
        return levain::app::FrameHooks{};
    };
}

/// L'éditeur sous `withEditor`, panneaux ouverts, hors écran, une image par pas de temps.
Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames)
{
    auto parsed = levain::platform::parseInputScript(script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = frames;
    settings.showUiPanels = true;
    settings.width = WindowWidth;
    settings.height = WindowHeight;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    levain::editor::EditorOptions options;
    options.afterUi = [&run](const levain::editor::Editor&)
    { run.seen.push_back(seenOf(*run.app)); };
    run.exitCode =
        levain::app::runApp(settings, levain::editor::withEditor(programOf(run), options));
    return run;
}

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

/// Un script que rien ne lit : un mouvement de souris loin de tout (un script vide est refusé).
constexpr std::string_view Idle = "0 mouse move 1 1\n";

/// Chaque fenêtre sur son nœud, l'inspecteur visible.
int playLayout(const levain::tests::TestBackend& backend)
{
    constexpr int Frames = 14;
    const Run run = play(backend, Idle, Frames);
    if (run.exitCode != 0 || run.seen.size() != Frames)
    {
        return expect(false, "la disposition : code 0 et une image relevée par image");
    }
    const Seen& last = run.seen.back();
    const auto& nodes = last.nodes;
    const auto& w = last.windows;
    const auto at = [&w](std::size_t i) -> const Placed& { return w.at(i); };
    int failures = 0;
    for (std::size_t i = 0; i < w.size(); ++i)
    {
        failures += expect(at(i).exists && at(i).dock != 0,
                           std::format("« {} » existe, ancrée sur un nœud", Titles.at(i)));
    }
    failures += expect(at(0).dock == nodes.left, "la hiérarchie est sur le nœud de gauche") +
                expect(at(1).dock == nodes.inspector, "l'inspecteur est sur le nœud de droite") +
                expect(at(2).dock == nodes.bottom && at(3).dock == nodes.bottom &&
                           at(4).dock == nodes.bottom && at(5).dock == nodes.bottom,
                       "la console, Image, Passes et Scène sont en onglets du nœud du bas") +
                expect(nodes.left != nodes.center && nodes.left != nodes.inspector &&
                           nodes.left != nodes.bottom && nodes.center != nodes.inspector &&
                           nodes.center != nodes.bottom && nodes.inspector != nodes.bottom,
                       "les quatre nœuds sont distincts") +
                expect(at(1).tabVisible && at(1).windowsOnNode == 1,
                       "l'inspecteur est seul sur son nœud, donc visible (la CI le compte)") +
                expect(at(0).tabVisible && at(0).windowsOnNode == 1,
                       "la hiérarchie est seule sur son nœud");
    return failures;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const auto backend =
            levain::tests::testBackendNamed(arguments.size() == 2 ? arguments[1] : "vulkan");
        if (arguments.size() > 2 || !backend)
        {
            std::println(stderr, "usage : levain_editor_view [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = playLayout(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
