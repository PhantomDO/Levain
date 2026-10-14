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
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
// La fenêtre de la barre de mode, retrouvée par son nom : l'API interne d'ImGui.
#include <imgui_internal.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/app/player_input.hpp"
#include "levain/editor/editor.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"
#include "levain/ui/style.hpp"

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
    bool imguiP = false;   ///< ImGui voit la touche P tenue (Alt+P lui arrive)
    bool barDrawn = false; ///< la barre de mode a été dessinée à l'image d'avant
    float barX = 0.0f;     ///< son coin haut gauche, à l'image d'avant
    float barY = 0.0f;
    bool viewFocused = false; ///< la Vue a le focus à cette image (sa barre comprise)
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
        // Épinglée : une vraie fenêtre lirait la disposition de la machine (AZERTY chez Donnovan).
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition;
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
            // La barre de mode de l'image d'avant : le programme passe avant l'éditeur dans `ui`.
            const ImGuiWindow* bar = ImGui::FindWindowByName("###Mode");
            const bool barDrawn = bar != nullptr && bar->WasActive;
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
                 .imguiP = ImGui::IsKeyDown(ImGuiKey_P),
                 .barDrawn = barDrawn,
                 .barX = barDrawn ? bar->Pos.x : 0.0f,
                 .barY = barDrawn ? bar->Pos.y : 0.0f});
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
    bool editorStyled = false; ///< Le style d'ImGui, à la dernière image, est celui de l'éditeur.
    /// L'échelle (`FontScaleDpi`) et le rembourrage des champs du style d'ImGui, à chaque image,
    /// après les événements de l'image : ce que le zoom du système (`window scale`) a refait.
    std::vector<float> dpi;
    std::vector<float> framePadding;
};

/// `withEditor` règle le style d'ImGui à l'échelle de l'écran (`applyEditorStyle`) : sans cette
/// ligne, l'éditeur resterait au style par défaut, et aucun autre test ne le verrait. Le style
/// attendu est refait à l'échelle de celui d'ImGui, sans valeur en dur ; quelques champs qui
/// diffèrent du défaut.
bool hasEditorStyle(const ImGuiStyle& live)
{
    ImGuiStyle expected;
    levain::ui::applyEditorStyle(expected, live.FontScaleDpi);
    const ImVec4& liveOverline = live.Colors[ImGuiCol_TabSelectedOverline];
    const ImVec4& wanted = expected.Colors[ImGuiCol_TabSelectedOverline];
    return live.FramePadding.x == expected.FramePadding.x &&
           live.FrameRounding == expected.FrameRounding &&
           live.ColorMarkerSize == expected.ColorMarkerSize && liveOverline.z == wanted.z &&
           live.Colors[ImGuiCol_WindowBg].x == expected.Colors[ImGuiCol_WindowBg].x;
}

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
    // Panneaux ouverts, les barres (menus, outils, état) prennent leur hauteur à la Vue : dans 320
    // x 180, le trou central ne ferait plus que 20 px de haut, sans place pour la barre de mode.
    settings.width = panels ? 640 : 320;
    settings.height = panels ? 360 : 180;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    // Le programme relève dans `ui`, avant l'éditeur : ce que la Vue a posé à cette image
    // (`App::sceneRect`, la barre en calque) n'est connu qu'à la fin de `ui`, et complète la
    // dernière image relevée.
    levain::editor::EditorOptions options;
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.afterUi = [&run](const levain::editor::Editor& editor)
    {
        run.editorStyled = hasEditorStyle(ImGui::GetStyle());
        run.dpi.push_back(ImGui::GetStyle().FontScaleDpi);
        run.framePadding.push_back(ImGui::GetStyle().FramePadding.x);
        if (editor.view.shown && !run.seen.empty())
        {
            Seen& seen = run.seen.back();
            seen.rect = editor.view.image;
            seen.barDrawn = editor.view.barDrawn;
            seen.barX = editor.view.barAt.x;
            seen.barY = editor.view.barAt.y;
            seen.viewFocused = editor.view.focused;
        }
    };
    run.exitCode = levain::app::runApp(
        settings, levain::editor::withEditor(programOf(run.seen, run.programFrames, pinRect),
                                             std::move(options)));
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
    "21 button down right\n" // Édition, clic droit tenu : le vol de la caméra capture la souris...
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
    int failures =
        expect(run.exitCode == 0, "le script mène l'éditeur jusqu'à sa fin, code 0") +
        expect(run.seen.size() == ScriptFrames, "une image relevée par image du script") +
        expect(run.editorStyled, "l'éditeur a réglé le style d'ImGui (applyEditorStyle)");
    int played = 0;
    for (std::size_t i = 0; i < run.seen.size(); ++i)
    {
        const int frame = static_cast<int>(i);
        const Seen& seen = run.seen.at(i);
        played += playsAt(frame) ? 1 : 0;
        const std::string at = std::format("image {} : ", frame);
        // Panneaux fermés aussi, en Édition comme en jeu : le bandeau et le rappel de la touche.
        failures += expect(seen.barDrawn == (frame >= 1), at + "la barre de mode se dessine");
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
            // La souris n'est capturée en Édition que par le vol de la caméra (morceau 11) : un
            // clic droit appuyé dans la scène, ici aux images 21 et 22.
            failures += expect(!seen.leftSeen && !seen.rightSeen &&
                                   seen.captured == (frame == 21 || frame == 22),
                               at + "ni souris vue, ni capturée hors du vol, en Édition");
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
        failures += expect(seen.captured == (seen.rightSeen || frame == 21 || frame == 22),
                           at + "la souris est capturée tant que le jeu tient le clic droit, ou "
                                "que la caméra vole");
    }
    return failures;
}

/// Panneaux ouverts : la barre de mode se dessine en calque de la Vue sans que la validation
/// d'ImGui ne proteste, dans l'image de la Vue et non sur les colonnes ; `sceneRect` est cette
/// image, plus étroite que la fenêtre. Alt+P joue et met la Vue en avant : la route est *jeu*, une
/// seconde Alt+P n'arrive pas à ImGui et ne ramène pas à l'Édition : seuls Échap et le bouton le
/// font.
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
           expect(second.route == levain::app::InputRoute::Game && !second.imguiP,
                  "panneaux ouverts : Jouer met la Vue en avant, la route est jeu") +
           expect(last.rect.x > 0.0f && last.rect.width > 0.0f &&
                      last.rect.x + last.rect.width < 640.0f,
                  "panneaux ouverts : la scène est l'image de la Vue, entre les colonnes") +
           expect(last.barDrawn && last.barX >= last.rect.x && last.barY >= last.rect.y,
                  "panneaux ouverts : la barre est dans l'image de la Vue, pas sur les colonnes");
}

/// Panneaux fermés, rien d'épinglé : la scène est la fenêtre entière (320 × 180), et un clic à
/// x = 250, où une disposition aurait des panneaux, atteint le jeu. Sans la remise de `sceneRect` à
/// la fenêtre à chaque image, le rectangle resterait vide et la souris n'arriverait jamais. Le
/// bandeau est dessiné sans bouton, et il ne prend jamais le focus : un clic sur lui, en jeu,
/// laisse la route *jeu* (la route *UI* laisserait encore passer W, tant qu'aucun champ n'est
/// actif : c'est la route qu'on lit).
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
        failures +=
            expect(seen.leftSeen == (i == 3 || i == 6),
                   at + "le clic, hors de tout panneau comme sur le bandeau, atteint le jeu");
        failures += expect(
            seen.route == (i < 2 ? levain::app::InputRoute::Editor : levain::app::InputRoute::Game),
            at + "le bandeau cliqué n'a pas pris le focus : la route reste jeu");
    }
    return failures;
}

/// Le bouton de la barre, cliqué à la souris scriptée : Édition, jeu, Édition. Les coordonnées
/// viennent de la disposition (le coin du trou, la marge de la fenêtre, le bouton) : un premier
/// passage sans script lit le trou.
int playButton(const levain::tests::TestBackend& backend)
{
    // (un script vide est refusé, bruyamment : un mouvement de souris loin de tout, pour rien.)
    const Run probe = play(backend, "0 mouse move 1 1\n", 3, true, false);
    if (probe.seen.empty())
    {
        return expect(false, "le passage qui lit le trou n'a relevé aucune image");
    }
    const levain::app::ScreenRect hole = probe.seen.back().rect;
    // La barre est à 8 px du coin du trou, sa marge est de 8 px, le bouton fait environ 19 px.
    const float x = hole.x + 8.0f + 8.0f + 20.0f;
    const float y = hole.y + 8.0f + 8.0f + 9.0f;
    const std::string script = std::format("0 mouse move {} {}\n2 button down left\n"
                                           "3 button up left\n6 button down left\n"
                                           "7 button up left\n",
                                           x, y);
    const Run run = play(backend, script, 12, true, false);
    if (run.seen.size() != 12)
    {
        return expect(false, "le bouton : une image relevée par image");
    }
    // Le clic de l'image 3 joue (images 4 à 7), celui de l'image 7 revient à l'Édition (8 à 11).
    return expect(run.exitCode == 0, "le bouton : code 0") +
           expect(run.seen.at(3).steps == 0, "le bouton : rien ne joue avant son clic") +
           expect(run.seen.at(7).steps == 4 && run.seen.at(7).programFrames == 4,
                  "le bouton « Jouer » joue à l'image suivante") +
           expect(run.seen.at(11).steps == 4 && run.seen.at(11).programFrames == 4,
                  "le bouton « Revenir à l'édition » arrête le jeu");
}

/// Les points de la Vue, lus par un premier passage sans script : un point de l'image hors de la
/// barre, et un point de la marge de la barre de mode (dans son enfant, à côté du bouton).
struct ViewSpots
{
    float sceneX = 0.0f;
    float sceneY = 0.0f;
    float barX = 0.0f;
    float barY = 0.0f;
};

std::optional<ViewSpots> viewSpots(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, "0 mouse move 1 1\n", 3, true, false);
    if (probe.seen.empty() || probe.seen.back().rect.width <= 0.0f)
    {
        return std::nullopt;
    }
    const levain::app::ScreenRect hole = probe.seen.back().rect;
    // La barre est à 8 px du coin, sa marge de 8 px : le point est dans l'enfant, hors du bouton.
    // Hors de la barre, qui en jeu (avec son bandeau) couvre la Vue de 320 px : son coin bas droit.
    return ViewSpots{.sceneX = hole.x + hole.width - 4.0f,
                     .sceneY = hole.y + hole.height - 3.0f,
                     .barX = hole.x + 8.0f + 3.0f,
                     .barY = hole.y + 8.0f + 3.0f};
}

/// La barre de mode de la Vue, panneaux ouverts, de bout en bout : (1) en jeu, un clic sur la barre
/// ne va qu'à elle, jamais au jeu (la barre est dans le rectangle de la scène : sans la route *UI*
/// sur son survol, le jeu choisirait ce qu'il y a dessous) ; (2) le clic qui suit Alt+P atteint le
/// jeu, sans image perdue à attendre le focus de la Vue ; (3) cliquer la barre laisse le focus à la
/// Vue (`ImGuiFocusedFlags_ChildWindows`), sans quoi le clic suivant ne ferait que le reprendre.
int playBar(const levain::tests::TestBackend& backend)
{
    const auto spots = viewSpots(backend);
    if (!spots)
    {
        return expect(false, "la barre : le passage qui lit la Vue n'a pas trouvé son image");
    }
    constexpr std::string_view AltP =
        "2 key down Left Alt\n2 key down P\n3 key up P\n3 key up Left Alt\n";
    // (1) Jouer à l'image 2, puis un clic sur le bouton « Revenir à l'édition » (image 7) :
    // l'Édition, et le jeu n'a vu aucun bouton.
    const float buttonX = spots->barX + 17.0f;
    const float buttonY = spots->barY + 6.0f;
    const Run onBar = play(backend,
                           std::format("{}5 mouse move {} {}\n7 button down left\n"
                                       "8 button up left\n",
                                       AltP, buttonX, buttonY),
                           14, true, false);
    int failures = expect(onBar.exitCode == 0 && onBar.seen.size() == 14,
                          "la barre : une image relevée par image, code 0");
    if (onBar.seen.size() != 14)
    {
        return failures;
    }
    const bool anyLeft = std::ranges::any_of(onBar.seen, [](const Seen& s) { return s.leftSeen; });
    failures += expect(onBar.seen.at(6).steps > 0, "la barre : le jeu joue avant le clic") +
                expect(!anyLeft, "la barre : le clic sur la barre en jeu n'atteint pas le jeu") +
                expect(onBar.seen.at(13).steps == onBar.seen.at(12).steps &&
                           onBar.seen.at(12).steps == onBar.seen.at(9).steps,
                       "la barre : le bouton « Revenir à l'édition » arrête le jeu");
    // (2) La Vue n'a plus le focus (un clic sur la hiérarchie, image 1) ; Alt+P à l'image 5, puis
    // le clic de l'image 6 : le jeu le voit à l'image même, sans attendre que la Vue ait pris le
    // focus.
    const Run after = play(backend,
                           std::format("0 mouse move 20 90\n1 button down left\n2 button up left\n"
                                       "3 mouse move {0} {1}\n"
                                       "5 key down Left Alt\n5 key down P\n6 key up P\n"
                                       "6 key up Left Alt\n6 button down left\n7 button up left\n",
                                       spots->sceneX, spots->sceneY),
                           12, true, false);
    failures += expect(after.seen.size() == 12, "Alt+P puis clic : une image relevée par image");
    if (after.seen.size() == 12)
    {
        failures += expect(!after.seen.at(4).viewFocused,
                           "Alt+P puis clic : la hiérarchie a pris le focus à la Vue") +
                    expect(after.seen.at(6).route == levain::app::InputRoute::Game,
                           "Alt+P puis clic : la route est jeu dès l'image qui suit Alt+P") +
                    expect(after.seen.at(6).leftSeen,
                           "le clic qui suit Alt+P atteint le jeu, sans image perdue");
    }
    // (3) En Édition : un clic dans la Vue lui donne le focus, un clic sur la marge de la barre le
    // lui laisse.
    const Run focus =
        play(backend,
             std::format("0 mouse move {0} {1}\n1 button down left\n2 button up left\n"
                         "4 mouse move {2} {3}\n5 button down left\n"
                         "6 button up left\n",
                         spots->sceneX, spots->sceneY, spots->barX, spots->barY),
             10, true, false);
    failures += expect(focus.seen.size() == 10, "le focus : une image relevée par image");
    if (focus.seen.size() == 10)
    {
        failures +=
            expect(focus.seen.at(4).viewFocused, "le focus : un clic dans la Vue la focalise") +
            expect(focus.seen.at(8).viewFocused && focus.seen.at(9).viewFocused,
                   "le focus : un clic sur la barre laisse le focus à la Vue");
    }
    return failures;
}

/// Le zoom du système change en route (`window scale 2`, image 3) : `App` refait le style de
/// l'éditeur à la nouvelle échelle, avant l'image où il l'a reçu. Sans la fonction de style de
/// `setUiStyle` (ou sans son rappel par la boucle), l'éditeur resterait à l'échelle de l'ouverture
/// : ses panneaux trop petits sur l'écran qu'on vient de changer, et rien ne le dirait.
int playRescale(const levain::tests::TestBackend& backend)
{
    constexpr int Frames = 8;
    constexpr int ChangeAt = 3;
    const Run run = play(backend, "3 window scale 2\n", Frames, false, true);
    if (run.exitCode != 0 || run.dpi.size() != Frames)
    {
        return expect(false, "le zoom : code 0 et une échelle relevée par image");
    }
    // Ce que l'éditeur donne à 2 : le style refait à neuf, sans valeur écrite en dur.
    ImGuiStyle doubled;
    levain::ui::applyEditorStyle(doubled, 2.0f);
    const float opening = run.dpi.front();
    int failures = expect(opening > 0.0f && opening != 2.0f,
                          "le zoom : l'ouverture n'est pas déjà à l'échelle 2");
    for (int frame = 0; frame < Frames; ++frame)
    {
        const std::size_t at = static_cast<std::size_t>(frame);
        const std::string where = std::format("le zoom, image {} : ", frame);
        const bool rescaled = frame >= ChangeAt;
        failures += expect(run.dpi.at(at) == (rescaled ? 2.0f : opening),
                           where + "l'échelle est celle de l'ouverture, puis celle du zoom");
        failures += expect((run.framePadding.at(at) == doubled.FramePadding.x) == rescaled,
                           where + "les tailles suivent l'échelle");
    }
    return failures + expect(run.editorStyled, "le zoom : le style refait est celui de l'éditeur");
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
        const int failures = playModes(*backend) + playPanels(*backend) +
                             playWholeWindow(*backend) + playButton(*backend) + playBar(*backend) +
                             playRescale(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
