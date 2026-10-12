// Les gestes de la caméra de l'éditeur dans la vraie boucle (ADR-0036, décisions 6, 8 et 13,
// morceau 11), un script d'input rejoué hors écran (le banc d'essai de l'App) sous `withEditor`,
// panneaux ouverts :
//   - clic droit tenu dans la Vue + la touche à la place du W : la caméra avance de ce que le pas
//     fixe donne, en QWERTY et en AZERTY (la touche marquée W d'un AZERTY, à la place du Z, ne fait
//     rien) ; la souris n'est capturée que pendant le vol ;
//   - le même geste commencé hors de la Vue ne fait rien, ni ne capture ;
//   - le menu du clic droit s'ouvre à un clic sans vol, pas à la fin d'un vol.
// Il faut un device, comme `levain_app_script`.
//   levain_editor_gestures [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/editor/camera.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/scene_bounds.hpp"
#include "levain/platform/input.hpp"
#include "levain/render/camera.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::editor::EditorCamera;

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 720;
constexpr float Dt = 1.0f / 60.0f;

/// Ce que l'éditeur a fait à une image, relevé à la fin de `ui`.
struct Seen
{
    EditorCamera camera;
    bool captured = false;
    bool capturedOverPanels = false;
    bool menuOpen = false;
    bool flew = false;
    levain::editor::Gesture gesture = levain::editor::Gesture::None;
    levain::app::ScreenRect image{};
    bool overrideMatchesCamera = false;
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    App* app = nullptr;
    flecs::entity_t target = 0;
    flecs::entity_t other = 0;
};

/// Une caméra du jeu en (0, 2, 10) qui regarde vers −Z, une cible placée et mise à l'échelle, et
/// une autre entité (la sélection de départ).
levain::app::StartFunction programOf(Run& run)
{
    return [&run](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera")
            .set(levain::scene::Transform{.position = {0.0f, 2.0f, 10.0f}})
            .set(levain::app::CameraLens{});
        run.target = app.world.entity("cible")
                         .set(levain::scene::Transform{.position = {60.0f, 0.0f, -30.0f},
                                                       .scale = glm::vec3{4.0f}})
                         .id();
        run.other = app.world.entity("autre")
                        .set(levain::scene::Transform{.position = {-5.0f, 0.0f, 0.0f}})
                        .id();
        run.app = &app;
        return levain::app::FrameHooks{};
    };
}

Seen seenOf(const App& app, const levain::editor::Editor& editor)
{
    Seen seen{.camera = editor.camera,
              .captured = app.mouseCaptured,
              .capturedOverPanels = app.mouseCaptureOverPanels,
              .menuOpen = editor.view.menuOpen,
              .flew = editor.gestures.flew,
              .gesture = editor.gestures.active,
              .image = editor.view.image};
    seen.overrideMatchesCamera =
        app.cameraOverride && app.cameraOverride->position == editor.camera.position;
    return seen;
}

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
    options.afterUi = [&run](const levain::editor::Editor& editor)
    { run.seen.push_back(seenOf(*run.app, editor)); };
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

bool near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-3f)
{
    return glm::length(a - b) <= epsilon;
}

constexpr std::string_view Idle = "0 mouse move 1 1\n";

/// Où sont les choses, relevé sur une exécution sans geste : le centre de la Vue et un point hors
/// de la Vue (la colonne de gauche).
struct Layout
{
    glm::vec2 center{};
    glm::vec2 outside{};
    levain::app::ScreenRect image{};
};

Layout layoutOf(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, Idle, 12);
    if (probe.exitCode != 0 || probe.seen.size() != 12)
    {
        throw std::runtime_error{"la disposition : le relevé a échoué"};
    }
    const Seen& last = probe.seen.back();
    const auto& image = last.image;
    Layout layout{.center = {image.x + (image.width / 2.0f), image.y + (image.height / 2.0f)},
                  .outside = {image.x / 2.0f, image.y + (image.height * 0.9f)},
                  .image = image};
    if (image.width < 100.0f || image.x < 40.0f)
    {
        throw std::runtime_error{"la disposition : la Vue est introuvable"};
    }
    return layout;
}

std::string at(glm::vec2 point)
{
    return std::format("{} {}", point.x, point.y);
}

/// Les images où l'état du vol (la capture) se lit : de l'appui, 8, à l'image du relâchement.
constexpr int PressFrame = 8;
constexpr int ReleaseFrame = 16;

/// Le vol : clic droit tenu dans la Vue (ou hors d'elle), la touche de vol des images 9 à 14. Le
/// pas fixe donne `speed * Dt` par image tenue ; six images tenues avancent de 0,8.
std::string flightScript(glm::vec2 mouse, std::string_view keyDown, std::string_view keyUp)
{
    return std::format("4 mouse move {}\n8 button down right\n9 {}\n15 {}\n16 button up right\n",
                       at(mouse), keyDown, keyUp);
}

int checkCaptureOnlyDuringFlight(const Run& run, int firstCaptured, int lastCaptured,
                                 std::string_view what)
{
    int failures = 0;
    for (std::size_t i = 0; i < run.seen.size(); ++i)
    {
        const int frame = static_cast<int>(i);
        const bool wanted = frame >= firstCaptured && frame <= lastCaptured;
        failures +=
            expect(run.seen.at(i).captured == wanted && run.seen.at(i).capturedOverPanels == wanted,
                   std::format("{} : image {}, la souris est capturée {}", what, frame,
                               wanted ? "pendant le vol" : "seulement pendant le vol"));
    }
    return failures;
}

/// En QWERTY et en AZERTY : clic droit tenu dans la Vue, la touche à la place du W avance.
int playFlight(const levain::tests::TestBackend& backend, const Layout& layout)
{
    int failures = 0;

    struct Case
    {
        const char* name;
        const char* down;
        const char* up;
        bool moves;
    };

    // La paire AZERTY : la touche à la place du W tape « z » ; la touche marquée W y est à la
    // place du Z. Par position, la première avance et la seconde non ; sur un QWERTY, W avance et Z
    // non. Une lecture par lettre inverserait l'AZERTY.
    constexpr std::array<Case, 4> Cases = {
        {{"QWERTY, W", "key down W", "key up W", true},
         {"AZERTY, Z à la place du W", "key down W as z", "key up W as z", true},
         {"AZERTY, W à la place du Z", "key down Z as w", "key up Z as w", false},
         {"QWERTY, Z", "key down Z", "key up Z", false}}};
    for (const Case& c : Cases)
    {
        const Run run = play(backend, flightScript(layout.center, c.down, c.up), 22);
        if (run.exitCode != 0 || run.seen.size() != 22)
        {
            failures += expect(false, std::format("{} : code 0 et une image par image", c.name));
            continue;
        }
        const EditorCamera start = run.seen.front().camera;
        const EditorCamera end = run.seen.back().camera;
        const glm::vec3 forward = levain::editor::viewDirectionOf(start);
        const glm::vec3 expected = start.position + (forward * (start.flySpeed * Dt * 6.0f));
        failures +=
            expect(c.moves ? near(end.position, expected) : near(end.position, start.position),
                   std::format("{} : la caméra {} de 6 images à la vitesse de vol", c.name,
                               c.moves ? "avance" : "n'avance pas"));
        failures +=
            expect(end.yawDegrees == start.yawDegrees && end.pitchDegrees == start.pitchDegrees,
                   std::format("{} : sans mouvement de souris, le regard est le même", c.name));
        failures += checkCaptureOnlyDuringFlight(run, PressFrame, ReleaseFrame - 1, c.name);
        failures += expect(run.seen.at(PressFrame).gesture == levain::editor::Gesture::Fly &&
                               run.seen.at(ReleaseFrame).gesture == levain::editor::Gesture::None,
                           std::format("{} : le vol dure de l'appui au relâchement", c.name));
        // Le menu du clic droit : pas à la fin d'un vol que la touche a mené ; une touche qui ne
        // vole pas laisse un simple clic, dont le menu s'ouvre.
        failures += expect(
            std::ranges::any_of(run.seen, [](const Seen& s) { return s.menuOpen; }) == !c.moves,
            std::format("{} : le menu {} à la fin du clic droit", c.name,
                        c.moves ? "ne s'ouvre pas, un vol l'a précédé,"
                                : "s'ouvre, rien n'a volé,"));
        failures +=
            expect(near(run.seen.at(8).camera.position, start.position) &&
                       near(run.seen.at(15).camera.position, c.moves ? expected : start.position),
                   std::format("{} : la caméra ne bouge qu'entre l'appui et le relâchement "
                               "de la touche",
                               c.name));
        // La caméra de l'App est celle de l'éditeur, chaque image.
        failures += expect(
            std::ranges::all_of(run.seen, [](const Seen& s) { return s.overrideMatchesCamera; }),
            std::format("{} : App::cameraOverride suit la caméra de l'éditeur", c.name));
    }
    return failures;
}

/// Le même geste commencé hors de la Vue (dans la hiérarchie) ne fait rien et ne capture pas.
int playOutside(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run run = play(backend, flightScript(layout.outside, "key down W", "key up W"), 22);
    const EditorCamera start = run.seen.front().camera;
    return expect(run.exitCode == 0 && run.seen.size() == 22, "hors de la Vue : code 0") +
           expect(near(run.seen.back().camera.position, start.position) &&
                      run.seen.back().camera.yawDegrees == start.yawDegrees,
                  "hors de la Vue : la caméra ne bouge pas") +
           checkCaptureOnlyDuringFlight(run, 1000, 1000, "hors de la Vue (jamais capturée)") +
           expect(std::ranges::none_of(run.seen, [](const Seen& s) { return s.menuOpen; }),
                  "hors de la Vue : aucun menu de la Vue");
}

/// Un clic droit pressé hors de la Vue (dans la hiérarchie) et relâché dans la Vue n'ouvre pas son
/// menu : il n'est pas parti d'elle, ImGui y voit pourtant un relâchement.
int playPressedOutside(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run run = play(backend,
                         std::format("4 mouse move {}\n8 button down right\n10 mouse move {}\n"
                                     "12 button up right\n",
                                     at(layout.outside), at(layout.center)),
                         16);
    return expect(run.exitCode == 0 && run.seen.size() == 16, "pressé hors de la Vue : code 0") +
           expect(std::ranges::none_of(run.seen, [](const Seen& s) { return s.menuOpen; }),
                  "un clic droit pressé hors de la Vue, relâché dans la Vue, n'ouvre pas le menu") +
           checkCaptureOnlyDuringFlight(run, 1000, 1000, "pressé hors de la Vue");
}

/// Le menu du clic droit : il s'ouvre à un clic sans vol, pas à la fin d'un vol (même relâchement,
/// vu d'ImGui comme un clic), et la souris n'est capturée que le temps du clic.
int playMenu(const levain::tests::TestBackend& backend, const Layout& layout)
{
    const Run click = play(
        backend,
        std::format("4 mouse move {}\n8 button down right\n9 button up right\n", at(layout.center)),
        14);
    const Run moved = play(backend,
                           std::format("4 mouse move {}\n8 button down right\n9 mouse motion 12 5\n"
                                       "10 button up right\n",
                                       at(layout.center)),
                           14);
    int failures = expect(click.exitCode == 0 && moved.exitCode == 0, "le menu : code 0");
    failures += expect(std::ranges::any_of(click.seen, [](const Seen& s) { return s.menuOpen; }),
                       "un clic droit sans vol ouvre le menu de la Vue");
    failures +=
        expect(std::ranges::none_of(moved.seen, [](const Seen& s) { return s.menuOpen; }),
               "la fin d'un vol, relâchement vu comme un clic par ImGui, n'ouvre pas le menu");
    failures += expect(moved.seen.at(9).camera.yawDegrees != moved.seen.at(8).camera.yawDegrees,
                       "le mouvement relatif du vol tourne le regard");
    failures += checkCaptureOnlyDuringFlight(click, PressFrame, PressFrame, "le clic sans vol");
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
            std::println(stderr, "usage : levain_editor_gestures [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const Layout layout = layoutOf(*backend);
        const int failures = playFlight(*backend, layout) + playOutside(*backend, layout) +
                             playPressedOutside(*backend, layout) + playMenu(*backend, layout);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
