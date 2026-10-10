// Les modes de l'éditeur dans la vraie boucle (ADR-0036, morceau 5) : `withEditor` autour d'un
// programme minimal, un script d'input rejoué hors écran (le banc d'essai de l'App). En Édition le
// `frame` du programme n'est jamais appelé, aucun pas ne se joue et `PlayerInput` reste vide,
// touches tenues ; Alt+P joue, et les touches atteignent alors le jeu, pas ImGui ; la souris
// n'atteint le jeu que dans la scène ; Échap revient, et un clic droit tenu, qui capturait la
// souris, la rend. Il faut un device, comme `levain_app_script`.
//   levain_editor_modes [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/app/player_input.hpp"
#include "levain/editor/editor.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;

/// Ce que le programme voit de chaque image, relevé dans `ui` : après les pas, avant la route.
struct Seen
{
    int programFrames = 0;        ///< les appels de son `frame`, depuis le début
    int steps = 0;                ///< `App::stepsPlayed`
    float moverX = 0.0f;          ///< ce que la simulation a fait avancer
    float forward = 0.0f;         ///< l'axe `move_forward` de `PlayerInput`
    bool anyHeld = false;         ///< une action tenue dans `PlayerInput`
    bool imguiW = false;          ///< ImGui voit la touche W tenue
    bool leftSeen = false;        ///< le jeu tient le bouton gauche
    bool rightSeen = false;       ///< le jeu tient le bouton droit
    bool captured = false;        ///< la souris est capturée
    levain::app::ScreenRect rect; ///< `App::sceneRect`, tel que les panneaux l'ont posé
    levain::app::InputRoute route = levain::app::InputRoute::Ui; ///< celle de cette image
    bool imguiP = false; ///< ImGui voit la touche P tenue (Alt+P lui arrive)
};

/// Le programme : une caméra, un « mover » que la simulation avance, un `frame` qui compte ses
/// appels et capture la souris tant que le clic droit est tenu (le regard du sandbox). `pinRect` :
/// la scène est le côté gauche de la fenêtre, comme le ferait la Vue.
levain::app::StartFunction programOf(std::vector<Seen>& seen, int& programFrames, bool pinRect)
{
    return
        [&seen, &programFrames, pinRect](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        app.world.entity("mover")
            .set(levain::scene::Transform{})
            .set(levain::scene::Velocity{.linear = {1, 0, 0}});
        const auto forward = levain::input::axisIndex(app.bindings, "move_forward");
        const auto look = levain::input::actionIndex(app.bindings, "look_enable");
        if (!forward || !look)
        {
            return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                           "data/input.cfg : move_forward ou look_enable manque");
        }
        levain::app::FrameHooks hooks;
        hooks.frame = [&programFrames, look = *look](App& frameApp)
        {
            ++programFrames;
            frameApp.mouseCaptureWanted = levain::input::actionHeld(frameApp.input, look);
        };
        hooks.ui = [&seen, &programFrames, forward = *forward, pinRect](App& uiApp)
        {
            const auto& state = uiApp.world.get<levain::app::PlayerInput>().state;
            const auto& raw = uiApp.input.raw;
            seen.push_back(
                {.programFrames = programFrames,
                 .steps = uiApp.stepsPlayed,
                 .moverX = uiApp.world.lookup("mover").get<levain::scene::Transform>().position.x,
                 .forward = levain::input::axisValue(state, forward),
                 .anyHeld = std::ranges::any_of(state.actionsHeld, [](bool held) { return held; }),
                 .imguiW = ImGui::IsKeyDown(ImGuiKey_W),
                 .leftSeen = raw.mouseButtons.test(1),
                 .rightSeen = raw.mouseButtons.test(3),
                 .captured = uiApp.mouseCaptured,
                 .rect = uiApp.sceneRect,
                 .route = uiApp.inputRoute,
                 .imguiP = ImGui::IsKeyDown(ImGuiKey_P)});
            if (pinRect)
            {
                uiApp.sceneRect = {.x = 0.0f, .y = 0.0f, .width = 160.0f, .height = 180.0f};
            }
        };
        return hooks;
    };
}

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    int programFrames = 0;
};

/// Le programme sous `withEditor`, dans la vraie boucle, hors écran, une image par pas de temps
/// (`--steps` : ce que fait la simulation ne dépend pas de la machine).
Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames,
         bool panels, bool pinRect)
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
        settings, levain::editor::withEditor(programOf(run.seen, run.programFrames, pinRect), {}));
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

/// Les images où le jeu joue (le mode « Jouer (sans retour) » à l'avance des pas et du `frame`), 30
/// au total. Alt+P à l'image 4, depuis `ui` : le jeu joue dès la 5. Échap à l'image 17, depuis
/// `frame` : l'Édition dès la 17. Alt+P à l'image 22 : le jeu dès la 23.
bool playsAt(int frame)
{
    return (frame >= 5 && frame <= 16) || frame >= 23;
}

constexpr int ScriptFrames = 30;
constexpr std::string_view Script =
    "0 mouse move 40 90\n"
    "1 key down W\n" // Édition : tenue, le jeu n'en voit rien
    "3 key up W\n"
    "4 key down Left Alt\n"
    "4 key down P\n" // Alt+P : le jeu dès l'image 5
    "5 key up P\n"
    "5 key up Left Alt\n"
    "6 key down W\n" // en jeu : au jeu, pas à ImGui
    "8 key up W\n"
    "9 button down left\n" // dans la scène (x < 160)
    "10 button up left\n"
    "11 mouse move 250 90\n" // hors de la scène
    "12 button down left\n"
    "13 button up left\n"
    "14 mouse move 40 90\n"
    "15 button down right\n" // le jeu capture la souris
    "17 key down Escape\n"   // clic droit tenu : l'Édition, la souris rendue
    "18 key up Escape\n"
    "20 button up right\n"
    "21 button down right\n" // Édition, clic droit tenu...
    "22 key down Left Alt\n"
    "22 key down P\n" // ...et le jeu dès l'image 23
    "23 key up P\n"
    "23 key up Left Alt\n"
    "26 button up right\n"
    "27 button down right\n" // un nouvel appui, en jeu : capturée
    "29 button up right\n";

int playModes(const levain::tests::TestBackend& backend)
{
    const Run run = play(backend, Script, ScriptFrames, false, true);
    int failures = expect(run.exitCode == 0, "le script mène l'éditeur jusqu'à sa fin, code 0") +
                   expect(run.seen.size() == ScriptFrames, "une image relevée par image du script");
    int played = 0;
    for (std::size_t i = 0; i < run.seen.size(); ++i)
    {
        const int frame = static_cast<int>(i);
        const Seen& seen = run.seen.at(i);
        played += playsAt(frame) ? 1 : 0;
        const std::string at = std::format("image {} : ", frame);
        // Édition : le frame du programme n'est jamais appelé, aucun pas, rien dans PlayerInput. Le
        // jeu : un pas et un frame par image, la simulation avance de ce qu'elle a joué.
        failures += expect(seen.programFrames == played,
                           at + "le frame du programme tourne en jeu seulement");
        failures += expect(seen.steps == played, at + "un pas par image de jeu, aucun en Édition");
        failures += expect(std::abs(seen.moverX - static_cast<float>(played) / 60.0f) < 1e-4f,
                           at + "la simulation n'avance qu'en jeu");
        if (!playsAt(frame))
        {
            failures += expect(seen.forward == 0.0f && !seen.anyHeld,
                               at + "PlayerInput reste vide en Édition");
            failures += expect(!seen.leftSeen && !seen.rightSeen && !seen.captured,
                               at + "ni souris vue ni souris capturée en Édition");
        }
        // Les touches : ImGui seul les voit en Édition (W, images 1 et 2) ; en jeu le jeu seul (6
        // et 7).
        failures += expect(seen.imguiW == (frame == 1 || frame == 2),
                           at + "ImGui voit la touche en Édition, pas en jeu");
        failures += expect(seen.forward == ((frame == 6 || frame == 7) ? 1.0f : 0.0f),
                           at + "la touche atteint PlayerInput en jeu seulement");
        // La souris : dans la scène, au jeu ; dehors, non ; le clic droit tenu capture, et le
        // retour à l'Édition, comme le jeu qui reprend, ne le capture pas sans un nouvel appui.
        failures += expect(seen.leftSeen == (frame == 9),
                           at + "le clic gauche n'atteint le jeu que dans la scène");
        failures +=
            expect(seen.rightSeen == (frame == 15 || frame == 16 || frame == 27 || frame == 28),
                   at + "le jeu tient le clic droit de son appui à son relâchement, pas avant");
        failures += expect(seen.captured == seen.rightSeen,
                           at + "la souris est capturée tant que le jeu tient le clic droit");
    }
    return failures;
}

/// Panneaux ouverts : `sceneRect` est le trou du centre de la disposition, plus étroit que la
/// fenêtre. Alt+P joue ; une seconde Alt+P, que le panneau qui a le focus (la route UI) laisse
/// arriver à ImGui, ne ramène pas à l'Édition : seule Échap le fait.
int playPanels(const levain::tests::TestBackend& backend)
{
    const Run run = play(backend,
                         "2 key down Left Alt\n2 key down P\n3 key up P\n3 key up Left Alt\n"
                         "5 key down Left Alt\n5 key down P\n6 key up P\n6 key up Left Alt\n",
                         8, true, false);
    if (run.exitCode != 0 || run.seen.size() != 8)
    {
        return expect(false, "panneaux ouverts : code 0 et une image relevée par image");
    }
    const Seen& last = run.seen.back();
    const Seen& second = run.seen.at(5);
    return expect(last.steps == 5 && last.programFrames == 5,
                  "panneaux ouverts : le jeu joue de l'image 3 à la 7, Alt+P ne le rend pas") +
           expect(second.route == levain::app::InputRoute::Ui && second.imguiP,
                  "panneaux ouverts : un panneau a le focus, la seconde Alt+P arrive à ImGui") +
           expect(last.rect.x > 0.0f && last.rect.width > 0.0f &&
                      last.rect.x + last.rect.width < 320.0f,
                  "panneaux ouverts : la scène est le trou du centre, entre les colonnes");
}

/// Panneaux fermés, rien d'épinglé : la scène est la fenêtre entière (320 × 180), et un clic à
/// x = 250, où une disposition aurait des panneaux, atteint le jeu. Sans la remise de `sceneRect` à
/// la fenêtre à chaque image, le rectangle resterait vide et la souris n'arriverait jamais. Un
/// clic dans le coin, en jeu, ne prend pas le focus et laisse la route *jeu* (la route *UI*
/// laisserait encore passer W, tant qu'aucun champ n'est actif : c'est la route qu'on lit).
int playWholeWindow(const levain::tests::TestBackend& backend)
{
    const Run run = play(backend,
                         "0 mouse move 250 90\n1 key down Left Alt\n1 key down P\n2 key up P\n"
                         "2 key up Left Alt\n3 button down left\n4 button up left\n"
                         "5 mouse move 12 12\n6 button down left\n7 button up left\n",
                         12, false, false);
    int failures = expect(run.exitCode == 0 && run.seen.size() == 12,
                          "panneaux fermés : une image relevée par image, code 0");
    for (std::size_t i = 0; i < run.seen.size(); ++i)
    {
        const Seen& seen = run.seen.at(i);
        const std::string at = std::format("panneaux fermés, image {} : ", i);
        failures += expect(seen.rect.x == 0.0f && seen.rect.y == 0.0f &&
                               seen.rect.width == 320.0f && seen.rect.height == 180.0f,
                           at + "la scène est la fenêtre entière");
        failures += expect(seen.leftSeen == (i == 3 || i == 6),
                           at + "le clic, hors de tout panneau, atteint le jeu");
        failures += expect(
            seen.route == (i < 2 ? levain::app::InputRoute::Editor : levain::app::InputRoute::Game),
            at + "le clic n'a pas pris le focus : la route reste jeu");
    }
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
            std::println(stderr, "usage : levain_editor_modes [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = playModes(*backend) + playPanels(*backend) + playWholeWindow(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
