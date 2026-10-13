// La Vue de l'éditeur dans la vraie boucle (ADR-0036, morceau 8) : `withEditor` autour d'un
// programme minimal, un script d'input rejoué hors écran (le banc d'essai de l'App). Chaque fenêtre
// est sur son nœud de la disposition et l'inspecteur est seul sur le sien, donc visible ; la Vue
// demande la taille de son contenu et la scène est rendue à cette taille ; le menu « Fenêtre »,
// cliqué à la souris scriptée, ferme la Vue (elle demande 0 x 0, aucune scène n'est rendue) puis la
// rouvre. Il faut un device, comme `levain_app_script`.
//   levain_editor_view [vulkan|d3d12|d3d12-warp]

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <functional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
// Les fenêtres et leurs nœuds retrouvés par leur nom : l'API interne d'ImGui.
#include <imgui_internal.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/editor/editor.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"
#include "levain/ui/tr.hpp"

namespace
{

using levain::app::App;
using levain::app::SceneOutput;

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 720;
/// Dans l'ordre du menu : Hiérarchie, Inspecteur, Console, Vue, Image, Passes, Scène.
constexpr std::size_t ViewEntry = 3;
constexpr std::array<const char*, 7> Titles = {"Hiérarchie", "Inspecteur", "Console", "Vue",
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
    std::array<Placed, 7> windows{};
    levain::app::DockNodes nodes{};
    bool viewOpen = false;
    bool viewShown = false;
    levain::app::SceneSize requested{};
    levain::app::ScreenRect image{};
    SceneOutput output = SceneOutput::Window;
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    levain::app::ScreenRect sceneRect{};
    levain::editor::WindowMenuLayout menu{};
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

Seen seenOf(const App& app, const levain::editor::Editor& editor)
{
    Seen seen{.nodes = app.ui.dock,
              .viewOpen = editor.view.open,
              .viewShown = editor.view.shown,
              .requested = editor.view.requested,
              .image = editor.view.image,
              .output = app.sceneView.output,
              .sceneRect = app.sceneRect,
              .menu = editor.menu};
    for (std::size_t i = 0; i < Titles.size(); ++i)
    {
        seen.windows.at(i) = placedOf(Titles.at(i));
    }
    if (app.sceneView.texture)
    {
        seen.textureWidth = app.sceneView.texture->getDesc().width;
        seen.textureHeight = app.sceneView.texture->getDesc().height;
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
        // Épinglée : une vraie fenêtre lirait la disposition de la machine (AZERTY chez Donnovan).
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition;
        run.app = &app;
        return levain::app::FrameHooks{};
    };
}

/// L'éditeur sous `withEditor`, panneaux ouverts, hors écran, une image par pas de temps.
/// `poke` tourne à la fin de `ui` de chaque image, après le relevé, avec le nombre d'images déjà
/// relevées : il pose ce que la fenêtre suivante doit voir (ancrer, replier, focaliser).
Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames,
         const std::function<void(std::size_t)>& poke = {})
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
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.afterUi = [&run, &poke](const levain::editor::Editor& editor)
    {
        run.seen.push_back(seenOf(*run.app, editor));
        if (poke)
        {
            poke(run.seen.size());
        }
    };
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

bool sameSize(const levain::app::SceneSize& size, std::uint32_t width, std::uint32_t height)
{
    return size.width == width && size.height == height;
}

/// Chaque fenêtre sur son nœud, l'inspecteur visible, la Vue à la taille de son contenu.
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
                expect(at(2).dock == nodes.bottom && at(4).dock == nodes.bottom &&
                           at(5).dock == nodes.bottom && at(6).dock == nodes.bottom,
                       "la console, Image, Passes et Scène sont en onglets du nœud du bas") +
                expect(at(ViewEntry).dock == nodes.center, "la Vue est sur le nœud du centre") +
                expect(nodes.left != nodes.center && nodes.left != nodes.inspector &&
                           nodes.left != nodes.bottom && nodes.center != nodes.inspector &&
                           nodes.center != nodes.bottom && nodes.inspector != nodes.bottom,
                       "les quatre nœuds sont distincts") +
                expect(at(1).tabVisible && at(1).windowsOnNode == 1,
                       "l'inspecteur est seul sur son nœud, donc visible (la CI le compte)") +
                expect(at(0).tabVisible && at(0).windowsOnNode == 1,
                       "la hiérarchie est seule sur son nœud") +
                expect(at(ViewEntry).tabVisible && at(ViewEntry).windowsOnNode == 1,
                       "la Vue est seule sur son nœud");
    // La Vue : sa taille demandée est celle de son contenu, l'image la remplit, la scène est rendue
    // dans une texture de cette taille, et `App::sceneRect` est l'image.
    const levain::app::SceneSize content{static_cast<std::uint32_t>(std::floor(last.image.width)),
                                         static_cast<std::uint32_t>(std::floor(last.image.height))};
    failures +=
        expect(last.viewShown && !last.requested.empty(),
               "la Vue est montrée et demande une taille") +
        expect(last.requested == content, "la taille demandée est celle du contenu de la Vue") +
        expect(last.output == SceneOutput::Texture, "la scène est dans la texture") +
        expect(sameSize(last.requested, last.textureWidth, last.textureHeight),
               "la texture de la scène a la taille demandée") +
        expect(last.sceneRect.x == last.image.x && last.sceneRect.y == last.image.y &&
                   last.sceneRect.width == last.image.width &&
                   last.sceneRect.height == last.image.height,
               "App::sceneRect est le rectangle de l'image de la Vue") +
        expect(last.image.x > 0.0f && last.image.x + last.image.width < WindowWidth &&
                   last.image.y + last.image.height < WindowHeight,
               "la Vue est entre les colonnes et au-dessus du bas");
    return failures;
}

constexpr int MenuFrames = 28;

/// Le script qui ferme la Vue par le menu « Fenêtre », puis la rouvre : le titre du menu à
/// `title`, la case de la Vue à `item`.
std::string menuScript(ImVec2 title, ImVec2 item)
{
    return std::format("2 mouse move {0} {1}\n3 button down left\n4 button up left\n"
                       "7 mouse move {2} {3}\n8 button down left\n9 button up left\n"
                       "13 mouse move {0} {1}\n14 button down left\n15 button up left\n"
                       "18 mouse move {2} {3}\n19 button down left\n20 button up left\n",
                       title.x, title.y, item.x, item.y);
}

/// Le menu « Fenêtre » à la souris : la case de la Vue ferme la fenêtre, la scène n'est plus rendue
/// et la Vue demande 0 x 0 ; la même case la rouvre.
int playMenu(const levain::tests::TestBackend& backend)
{
    // Deux passages pour lire la disposition : le titre du menu, puis les cases du menu ouvert.
    const Run first = play(backend, Idle, 4);
    if (first.seen.empty())
    {
        return expect(false, "le menu : le premier passage n'a rien relevé");
    }
    const ImVec2 title = first.seen.back().menu.title;
    const Run second = play(
        backend,
        std::format("2 mouse move {} {}\n3 button down left\n4 button up left\n", title.x, title.y),
        9);
    if (second.seen.size() != 9 || !second.seen.back().menu.itemsShown)
    {
        return expect(false, "le menu : cliqué, le menu « Fenêtre » s'ouvre et montre ses cases");
    }
    const ImVec2 item = second.seen.back().menu.items.at(ViewEntry);

    const Run run = play(backend, menuScript(title, item), MenuFrames);
    if (run.exitCode != 0 || run.seen.size() != MenuFrames)
    {
        return expect(false, "le menu : code 0 et une image relevée par image");
    }
    const Seen& before = run.seen.at(6);
    const Seen& closed = run.seen.at(12);
    const Seen& reopened = run.seen.at(MenuFrames - 1);
    return expect(before.menu.itemsShown, "le menu est ouvert avant le clic sur la case") +
           expect(before.viewOpen && before.viewShown && !before.requested.empty() &&
                      before.output == SceneOutput::Texture,
                  "avant : la Vue demande sa taille, la scène est dans la texture") +
           expect(!closed.viewOpen && !closed.viewShown, "la case ferme la Vue") +
           expect(sameSize(closed.requested, 0, 0),
                  "la Vue fermée demande 0 x 0, elle ne se tait pas") +
           expect(closed.output == SceneOutput::Nothing,
                  "Vue fermée : aucune scène n'est rendue, la fenêtre n'est pas pleine de scène") +
           expect(closed.sceneRect.width == 0.0f && closed.sceneRect.height == 0.0f,
                  "Vue fermée : la souris n'est pas au jeu (rectangle vide)") +
           expect(reopened.viewOpen && reopened.viewShown && !reopened.requested.empty() &&
                      reopened.output == SceneOutput::Texture,
                  "la case rouvre la Vue : elle redemande sa taille, la scène revient") +
           expect(sameSize(reopened.requested, reopened.textureWidth, reopened.textureHeight),
                  "rouverte, la texture a la taille demandée") +
           expect(reopened.windows.at(ViewEntry).dock == reopened.nodes.center,
                  "rouverte, la Vue retrouve son nœud");
}

/// La Vue cachée derrière un onglet, ou repliée, demande 0 x 0 : « rien demandé » rendrait la scène
/// à la fenêtre derrière les panneaux (`drawView`). Le contrôle : une Vue qui ne demande 0 x 0
/// que lorsqu'elle est fermée (`!asked && !view.open`) garde la taille de l'image d'avant, et
/// ce test rougit.
int playHidden(const levain::tests::TestBackend& backend)
{
    constexpr int Frames = 40;
    // Image 10 : la console s'ancre au centre, en onglet de la Vue ; 12 : elle prend l'onglet. 18 :
    // la Vue reprend le focus, donc son onglet. 24 : la Vue se détache (une fenêtre ancrée ne se
    // replie pas) ; 26 : elle se replie ; 32 : elle se déplie.
    const std::function<void(std::size_t)> poke = [](std::size_t recorded)
    {
        const std::string view = levain::ui::labelOf("Vue");
        ImGuiWindow* viewWindow = ImGui::FindWindowByName(view.c_str());
        switch (recorded)
        {
        case 10:
            ImGui::SetWindowDock(ImGui::FindWindowByName(levain::ui::labelOf("Console").c_str()),
                                 viewWindow->DockId, ImGuiCond_Always);
            break;
        case 12:
            ImGui::SetWindowFocus(levain::ui::labelOf("Console").c_str());
            break;
        case 18:
            ImGui::SetWindowFocus(view.c_str());
            break;
        case 24:
            ImGui::SetWindowDock(viewWindow, 0, ImGuiCond_Always);
            break;
        case 26:
            ImGui::SetWindowCollapsed(view.c_str(), true);
            break;
        case 32:
            ImGui::SetWindowCollapsed(view.c_str(), false);
            break;
        default:
            break;
        }
    };
    const Run run = play(backend, Idle, Frames, poke);
    if (run.exitCode != 0 || run.seen.size() != Frames)
    {
        return expect(false, "la Vue cachée : code 0 et une image relevée par image");
    }
    const auto hidden = [](const Seen& seen, std::string_view why)
    {
        return expect(!seen.viewShown, std::format("{} : la Vue n'est pas montrée", why)) +
               expect(sameSize(seen.requested, 0, 0), std::format("{} : elle demande 0 x 0", why)) +
               expect(seen.output == SceneOutput::Nothing,
                      std::format("{} : aucune scène n'est rendue", why)) +
               expect(seen.sceneRect.width == 0.0f && seen.sceneRect.height == 0.0f,
                      std::format("{} : la souris n'est pas au jeu (rectangle vide)", why));
    };
    const auto shown = [](const Seen& seen, std::string_view why)
    {
        return expect(seen.viewShown && !seen.requested.empty() &&
                          seen.output == SceneOutput::Texture,
                      std::format("{} : la Vue est montrée, elle demande sa taille", why));
    };
    return shown(run.seen.at(8), "avant l'ancrage") +
           hidden(run.seen.at(15), "derrière l'onglet de la console") +
           shown(run.seen.at(22), "focalisée de nouveau") + shown(run.seen.at(25), "détachée") +
           hidden(run.seen.at(30), "repliée") + shown(run.seen.at(38), "dépliée");
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
        const int failures = playLayout(*backend) + playMenu(*backend) + playHidden(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
