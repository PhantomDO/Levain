// Les raccourcis de l'éditeur dans la vraie boucle (ADR-0036, décision 13, morceau 10), un script
// d'input rejoué hors écran (le banc d'essai de l'App) :
//   - une paire AZERTY donne Z à la place du W : l'action « Avancer » est tenue par la position,
//     son accord s'affiche « Z » et ImGui voit la touche Z, jamais le W ; un QWERTY donne W ;
//     l'événement de disposition est compté ;
//   - F1 seule ferme les panneaux, Maj+F1 les laisse ouverts, sous la route de l'interface comme
//     sous la route *jeu* ;
//   - un vol (clic droit tenu, souris capturée) : ImGui reçoit encore le relâchement, son bouton
//     n'est pas resté enfoncé, et le menu du clic droit s'ouvre ensuite ;
//   - le relâchement d'un vol est un clic pour ImGui : le comportement d'aujourd'hui, épinglé.
// Il faut un device, comme `levain_app_script`.
//   levain_editor_shortcuts [vulkan|d3d12|d3d12-warp]

#include <cstdio>
#include <exception>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/input/state.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;

/// Un AZERTY : A et Q, W et Z échangés, le reste d'un QWERTY (comme
/// tests/editor_shortcuts_test.cpp).
std::uint32_t azertyAt(std::uint16_t scancode)
{
    constexpr std::uint16_t A = 4, Q = 20, W = 26, Z = 29;
    switch (scancode)
    {
    case A:
        return 'q';
    case Q:
        return 'a';
    case W:
        return 'z';
    case Z:
        return 'w';
    default:
        return levain::platform::qwertyKeyAtPosition(scancode);
    }
}

struct Seen
{
    bool forwardHeld = false; ///< la touche de « Avancer » est tenue, par sa position
    std::string forwardName;  ///< le nom affiché de son accord
    bool imguiW = false;
    bool imguiZ = false;
    int keymapChanges = 0;
    bool panelsOpen = false;
    bool captured = false;
    bool imguiRightDown = false;
    bool menuOpen = false;
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
};

/// Le programme de test : une caméra et un point d'accroche qui note ce que chaque image a vu.
/// `layout` : la disposition du clavier de l'App. `flight` : le clic droit tenu capture la souris
/// (le vol de l'éditeur, morceau 11) ; `menuFrom` : une fenêtre pleine, au menu du clic droit, à
/// dessiner à partir de cette image (0 : jamais) ; `route` : la route d'input de l'App.
levain::app::StartFunction programOf(std::vector<Seen>& seen,
                                     levain::platform::KeyAtPosition layout, bool flight,
                                     int menuFrom, levain::app::InputRoute route)
{
    return [&seen, layout = std::move(layout), flight, menuFrom,
            route](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.inputRoute = route;
        // Sous la route jeu, la souris va au jeu dans `sceneRect` : la fenêtre entière, pour que le
        // clic droit tenu arrive à `frame` (une fenêtre d'ImGui dessinée dessus la prendrait
        // sinon).
        app.sceneRect = {.x = 0.0f, .y = 0.0f, .width = 320.0f, .height = 180.0f};
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        app.keyAtPosition = layout;
        levain::app::FrameHooks hooks;
        hooks.frame = [flight](App& frameApp)
        {
            // Le bouton droit est le bouton 3 de SDL.
            frameApp.mouseCaptureWanted = flight && frameApp.input.raw.mouseButtons.test(3);
        };
        hooks.ui = [&seen, menuFrom, shortcuts = levain::editor::defaultShortcuts()](App& uiApp)
        {
            const auto* forward =
                levain::editor::bindingOf(shortcuts, levain::editor::Action::MoveForward);
            bool menuOpen = false;
            if (menuFrom > 0 && static_cast<int>(seen.size()) >= menuFrom)
            {
                const ImGuiIO& io = ImGui::GetIO();
                ImGui::SetNextWindowPos({0.0f, 0.0f});
                ImGui::SetNextWindowSize(io.DisplaySize);
                constexpr auto Flags = static_cast<ImGuiWindowFlags>(
                    static_cast<unsigned>(ImGuiWindowFlags_NoDecoration) |
                    static_cast<unsigned>(ImGuiWindowFlags_NoMove) |
                    static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings) |
                    static_cast<unsigned>(ImGuiWindowFlags_NoBackground));
                ImGui::Begin("###fond", nullptr, Flags);
                if (ImGui::BeginPopupContextWindow("menu"))
                {
                    ImGui::TextUnformatted("menu");
                    menuOpen = true;
                    ImGui::EndPopup();
                }
                ImGui::End();
            }
            seen.push_back(
                {.forwardHeld = levain::editor::positionHeld(*forward, uiApp.input.raw.keys),
                 .forwardName =
                     levain::editor::displayNameOf(forward->chords[0], uiApp.keyAtPosition),
                 .imguiW = ImGui::IsKeyDown(ImGuiKey_W),
                 .imguiZ = ImGui::IsKeyDown(ImGuiKey_Z),
                 .keymapChanges = uiApp.keymapChanges,
                 .panelsOpen = uiApp.ui.panelsOpen,
                 .captured = uiApp.mouseCaptured,
                 .imguiRightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right),
                 .menuOpen = menuOpen});
        };
        return hooks;
    };
}

Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames,
         bool panels, levain::platform::KeyAtPosition layout, bool flight, int menuFrom,
         levain::app::InputRoute route = levain::app::InputRoute::Ui)
{
    auto parsed = levain::platform::parseInputScript(script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = frames;
    settings.showUiPanels = panels;
    settings.width = 320;
    settings.height = 180;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    run.exitCode = levain::app::runApp(
        settings, programOf(run.seen, std::move(layout), flight, menuFrom, route));
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

/// Une paire AZERTY, puis un QWERTY : « Avancer » est tenue par la position, et s'affiche Z.
int playLayouts(const levain::tests::TestBackend& backend)
{
    const Run azerty = play(backend, "0 keymap changed\n1 key down W as z\n3 key up W as z\n", 5,
                            false, azertyAt, false, 0);
    const Run qwerty = play(backend, "1 key down W\n3 key up W\n", 5, false,
                            levain::platform::qwertyKeyAtPosition, false, 0);
    if (azerty.seen.size() != 5 || qwerty.seen.size() != 5)
    {
        return expect(false, "les dispositions : une image relevée par image");
    }
    return expect(azerty.exitCode == 0 && qwerty.exitCode == 0, "les dispositions : code 0") +
           expect(azerty.seen[0].forwardName == "Z" && azerty.seen[4].forwardName == "Z",
                  "AZERTY : l'accord de « Avancer » s'affiche Z, à la place du W") +
           expect(qwerty.seen[0].forwardName == "W", "QWERTY : le même accord s'affiche W") +
           expect(azerty.seen[1].forwardHeld && azerty.seen[2].forwardHeld &&
                      !azerty.seen[3].forwardHeld && !azerty.seen[0].forwardHeld,
                  "AZERTY : « Avancer » est tenue par la position, images 1 et 2") +
           expect(azerty.seen[1].imguiZ && !azerty.seen[1].imguiW,
                  "AZERTY : ImGui voit la touche Z, jamais le W") +
           expect(qwerty.seen[1].imguiW && !qwerty.seen[1].imguiZ, "QWERTY : ImGui voit le W") +
           expect(azerty.seen[0].keymapChanges == 1 && qwerty.seen[4].keymapChanges == 0,
                  "l'événement de disposition est compté, et lui seul");
}

/// F1 seule ferme les panneaux ouverts ; Maj+F1 les laisse ouverts, sous la route de l'interface
/// comme sous la route *jeu* (où l'appui sur Maj n'arrive pas à ImGui, mais le Maj tenu de l'appui
/// sur F1, si).
int playPanelsKey(const levain::tests::TestBackend& backend, levain::app::InputRoute route,
                  std::string_view routeName)
{
    const Run run = play(backend,
                         "1 key down Left Shift\n1 key down F1\n2 key up F1\n2 key up Left Shift\n"
                         "4 key down F1\n5 key up F1\n",
                         7, true, levain::platform::qwertyKeyAtPosition, false, 0, route);
    const std::string name{routeName};
    if (run.seen.size() != 7)
    {
        return expect(false, "F1 (" + name + ") : une image relevée par image");
    }
    return expect(run.exitCode == 0, "F1 (" + name + ") : code 0") +
           expect(run.seen[0].panelsOpen && run.seen[1].panelsOpen && run.seen[3].panelsOpen,
                  "Maj+F1 laisse les panneaux ouverts (" + name + ")") +
           expect(!run.seen[4].panelsOpen && !run.seen[6].panelsOpen,
                  "F1 seule les ferme (" + name + ")");
}

/// Un vol à la souris capturée, puis un clic droit : ImGui a reçu le relâchement du vol, et le menu
/// s'ouvre. Le second clic est loin du premier : un bouton resté enfoncé pour ImGui n'aurait pas de
/// nouvel appui au second clic (il est déjà enfoncé pour lui), donc pas de clic complet dont le
/// relâchement ouvre le menu.
int playFlight(const levain::tests::TestBackend& backend)
{
    const Run run = play(backend,
                         "0 mouse move 20 20\n1 button down right\n3 button up right\n"
                         "5 mouse move 200 100\n6 button down right\n7 button up right\n",
                         9, false, levain::platform::qwertyKeyAtPosition, true, 4);
    if (run.seen.size() != 9)
    {
        return expect(false, "le vol : une image relevée par image");
    }
    return expect(run.exitCode == 0, "le vol : code 0") +
           expect(!run.seen[0].captured && run.seen[1].captured && run.seen[2].captured &&
                      !run.seen[3].captured,
                  "le clic droit tenu capture la souris, images 1 et 2 (la boucle capture après "
                  "`frame`)") +
           expect(run.seen[1].imguiRightDown && run.seen[2].imguiRightDown,
                  "ImGui a reçu l'appui qui a pris la capture") +
           expect(!run.seen[3].imguiRightDown,
                  "le relâchement pendant la capture atteint ImGui : son bouton est relâché") +
           expect(!run.seen[6].menuOpen && run.seen[7].menuOpen,
                  "le menu du clic droit s'ouvre au relâchement qui suit le vol");
}

/// **Le comportement d'aujourd'hui, épinglé** : le relâchement d'un vol atteint ImGui, mais pas les
/// mouvements d'avant (`mouseTakenByCapture`) : appui et relâchement au même point, c'est un clic,
/// et le menu du clic droit de la Vue s'ouvre à la fin de chaque vol. Ce n'est pas voulu (Unreal
/// n'ouvre le menu que pour un clic droit sans glissé) : TODO morceau 11, la Vue ne l'ouvrira que
/// si aucun vol n'a eu lieu depuis l'appui, et ce test changera alors de sens. La fenêtre au menu
/// est dessinée dès l'image 1, avant l'appui.
int playFlightReleaseIsAClick(const levain::tests::TestBackend& backend)
{
    const Run run = play(backend,
                         "0 mouse move 20 20\n2 button down right\n3 mouse move 100 60\n"
                         "5 button up right\n",
                         8, false, levain::platform::qwertyKeyAtPosition, true, 1,
                         levain::app::InputRoute::Game);
    if (run.seen.size() != 8)
    {
        return expect(false, "le vol, clic : une image relevée par image");
    }
    return expect(run.exitCode == 0, "le vol, clic : code 0") +
           expect(run.seen[2].captured && run.seen[4].captured && !run.seen[5].captured,
                  "le vol, clic : la souris est capturée de l'appui au relâchement") +
           expect(!run.seen[4].menuOpen,
                  "le vol, clic : le menu n'est pas ouvert tant que le bouton est tenu") +
           expect(run.seen[5].menuOpen,
                  "le vol, clic : AUJOURD'HUI le menu s'ouvre au relâchement du vol (TODO 11)");
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
            std::println(stderr, "usage : levain_editor_shortcuts [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = playLayouts(*backend) +
                             playPanelsKey(*backend, levain::app::InputRoute::Ui, "route UI") +
                             playPanelsKey(*backend, levain::app::InputRoute::Game, "route jeu") +
                             playFlight(*backend) + playFlightReleaseIsAClick(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
