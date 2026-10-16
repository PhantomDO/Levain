// Les menus, les barres et le temps réel de l'éditeur dans la vraie boucle (ADR-0036, décisions 9,
// 12 et 13, morceau 12), un script d'input rejoué hors écran (le banc d'essai de l'App) :
//   - « Temps réel » décoché fige le temps de la scène que lit `record` (l'eau, l'herbe et les
//     matériaux), recoché il repart sans saut ; la case de la barre d'outils et Ctrl+R font de même
//     ;
//   - Ctrl+S atteint son action (grisée : elle le dit à la console, et la barre d'état le montre) ;
//     cliqué dans le menu Fichier, Enregistrer grisé ne fait rien ;
//   - une touche d'outil, dans la Vue, atteint son action ; en vol (clic droit tenu), ni W, qui
//     vole, ni R ni Espace, que la Vue tait, ne prennent d'outil ; avec Ctrl non plus ;
//   - la barre d'outils règle la vitesse de la caméra de l'éditeur, celle du vol : le même vol va
//     plus loin, dans le rapport des vitesses ;
//   - la modale « Quitter ? » ouverte, ni les menus, ni la barre d'outils, ni les raccourcis ;
//   - Aide > Raccourcis liste chaque raccourci, « Z » pour Avancer sous une paire AZERTY.
// Il faut un device, comme `levain_app_script`.
//   levain_editor_menus [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/core/log.hpp"
#include "levain/editor/editor.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

levain::app::AppSettings settingsOf(const levain::tests::TestBackend& backend, int frames)
{
    levain::app::AppSettings settings;
    settings.steps = frames;
    settings.width = 640;
    settings.height = 360;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    return settings;
}

constexpr int RealTimeFrames = 30;
constexpr int FreezeFrom = 8;
constexpr int FreezeUntil = 20;
constexpr double FrozenTolerance = 1.0e-6;
constexpr std::chrono::milliseconds FrozenFrameSleep{50}; // 12 images gelées : 0,6 s au moins

/// « Temps réel » décoché de l'image `FreezeFrom` à `FreezeUntil` (exclue) : le temps de la scène
/// que `record` reçoit ne bouge pas, puis repart de là sans sauter du temps gelé.
int playRealTime(const levain::tests::TestBackend& backend)
{
    std::vector<double> seconds;
    int frame = 0;
    // Le temps réel passé gelé, mesuré : la borne du « sans saut » en dépend, pas d'une durée
    // fixe qu'une image lente d'une machine chargée dépasserait seule.
    std::chrono::steady_clock::time_point frozeAt;
    std::chrono::steady_clock::time_point thawedAt;
    const auto start = [&](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        levain::app::FrameHooks hooks;
        hooks.frame = [&frame, &frozeAt, &thawedAt](App& frameApp)
        {
            if (frame == FreezeFrom)
            {
                frozeAt = std::chrono::steady_clock::now();
            }
            if (frame == FreezeUntil)
            {
                thawedAt = std::chrono::steady_clock::now();
            }
            frameApp.realTime = !(frame >= FreezeFrom && frame < FreezeUntil);
            if (!frameApp.realTime)
            {
                // Du temps réel passe pendant le gel : sans lui, « sans saut » ne se distinguerait
                // pas d'un gel qui rattrape.
                std::this_thread::sleep_for(FrozenFrameSleep);
            }
        };
        hooks.record = [&seconds, &frame](App&, nvrhi::ICommandList&, double scene)
        {
            seconds.push_back(scene);
            ++frame;
        };
        return hooks;
    };
    if (levain::app::runApp(settingsOf(backend, RealTimeFrames), start) != 0 ||
        seconds.size() != RealTimeFrames)
    {
        return expect(false, "le temps réel : code 0 et une image enregistrée par image");
    }
    const auto at = [&seconds](int index) { return seconds.at(static_cast<std::size_t>(index)); };
    const double frozenSeconds = std::chrono::duration<double>(thawedAt - frozeAt).count();
    bool frozen = true;
    for (int i = FreezeFrom; i < FreezeUntil; ++i)
    {
        // À l'arrondi près : le temps gelé est une différence de doubles.
        frozen = frozen && std::abs(at(i) - at(FreezeFrom)) < FrozenTolerance;
    }
    return expect(at(FreezeFrom - 1) > at(0), "avant le gel, le temps de la scène court") +
           expect(frozen, "décoché, le temps de la scène que lit record est figé") +
           expect(at(FreezeUntil) > at(FreezeUntil - 1) && at(FreezeUntil + 5) > at(FreezeUntil),
                  "recoché, il repart") +
           // Sans saut : le temps gelé n'est pas rattrapé (l'horloge compte le temps passé gelé).
           // La moitié du temps gelé mesuré : un rattrapage le dépasserait ; le pas d'une image,
           // lui, n'en est qu'un douzième, si lente que soit la machine.
           expect(at(FreezeUntil) - at(FreezeFrom) < frozenSeconds / 2.0,
                  "recoché, il repart d'où il s'était arrêté, sans rattraper le temps gelé");
}

// ---------------------------------------------------------------------------------------------
// L'éditeur : withEditor autour d'un programme minimal.

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 720;

/// Ce que l'éditeur a montré à une image, relevé à la fin de `ui` (`EditorOptions::afterUi`).
struct Seen
{
    levain::editor::ActionCounters actions;
    std::string status;
    std::uint64_t warnings = 0;
    std::uint64_t errors = 0;
    levain::editor::MenuBarLayout bar;
    levain::editor::ToolbarLayout toolbar;
    levain::editor::HelpState help;
    float flySpeed = 0.0f;
    glm::vec3 position{0.0f}; ///< Où est la caméra de l'éditeur.
    bool flying = false;      ///< Le vol est en cours (`inFlight`).
    bool quitOpen = false;    ///< La modale « Quitter ? » est ouverte.
    levain::app::ScreenRect image;
    levain::editor::Mode mode = levain::editor::Mode::Edit;
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    std::vector<double> sceneSeconds; ///< ce que `record` reçoit, une valeur par image
    std::vector<bool> realTime;       ///< `App::realTime` à cette image
};

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

/// Le programme sous `withEditor`, panneaux ouverts, hors écran, une image par pas de temps.
/// `azerty` : la paire de touches d'un AZERTY (la touche à la place du W tape « z »).
Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames,
         bool azerty = false)
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
    const auto program = [&run, azerty](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        // Toujours épinglée : sans elle, dans une vraie fenêtre, la disposition de la machine
        // (l'AZERTY du portable de Donnovan) change la lettre que les touches du script tapent.
        app.keyAtPosition =
            azerty ? levain::platform::KeyAtPosition{azertyAt}
                   : levain::platform::KeyAtPosition{levain::platform::qwertyKeyAtPosition};
        levain::app::FrameHooks hooks;
        hooks.record = [&run](App& recordApp, nvrhi::ICommandList&, double scene)
        {
            run.sceneSeconds.push_back(scene);
            run.realTime.push_back(recordApp.realTime);
        };
        return hooks;
    };
    levain::editor::EditorOptions options;
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.afterUi = [&run](const levain::editor::Editor& editor)
    {
        const levain::editor::StatusLine& status = editor.status;
        run.seen.push_back({.actions = editor.actions,
                            .status = status.text,
                            .warnings = status.counts.warnings,
                            .errors = status.counts.errors,
                            .bar = editor.bar,
                            .toolbar = editor.toolbar,
                            .help = editor.help,
                            .flySpeed = editor.camera.flySpeed,
                            .position = editor.camera.position,
                            .flying = levain::editor::inFlight(editor.gestures),
                            .quitOpen = editor.quit.open,
                            .image = editor.view.image,
                            .mode = editor.mode.current});
    };
    run.exitCode =
        levain::app::runApp(settings, levain::editor::withEditor(program, std::move(options)));
    return run;
}

/// Un clic à `at` : la souris y va à l'image `frame`, le bouton s'enfonce deux images plus tard.
std::string clickScript(int frame, ImVec2 at)
{
    return std::format("{} mouse move {} {}\n{} button down left\n{} button up left\n", frame, at.x,
                       at.y, frame + 2, frame + 3);
}

ImVec2 centerOf(const levain::app::ScreenRect& rect)
{
    return {rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f};
}

constexpr const char* NoInput = "0 mouse move 1 1\n"; // un script vide est refusé (règle n°7)
constexpr std::string_view SaveSaid = "Enregistrer : arrive en M7.3";

using levain::editor::Action;
using levain::editor::indexOf;

/// Ctrl+S atteint son action, qui est grisée : elle le dit à la console, la barre d'état le montre,
/// et c'est une fois, non deux.
int playSave(const levain::tests::TestBackend& backend)
{
    const Run run =
        play(backend, "2 key down Left Ctrl\n2 key down S\n3 key up S\n3 key up Left Ctrl\n", 12);
    if (run.exitCode != 0 || run.seen.size() != 12)
    {
        return expect(false, "Ctrl+S : code 0 et une image relevée par image");
    }
    const Seen& last = run.seen.back();
    return expect(run.seen.at(1).actions.declined.at(indexOf(Action::Save)) == 0,
                  "Ctrl+S : rien avant la touche") +
           expect(last.actions.declined.at(indexOf(Action::Save)) == 1,
                  "Ctrl+S atteint son action, une seule fois") +
           expect(last.actions.performed.at(indexOf(Action::Save)) == 0,
                  "Ctrl+S : l'action grisée ne fait rien") +
           expect(last.status == SaveSaid, "Ctrl+S : la barre d'état montre le dernier message");
}

int playSaveMenu(const levain::tests::TestBackend& backend)
{
    // Le menu Fichier ouvert, pour lire où est Enregistrer.
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8)
    {
        return expect(false, "Fichier : le passage qui lit la barre a relevé une image par image");
    }
    const ImVec2 file = probe.seen.back().bar.file;
    const Run opened = play(backend, clickScript(1, file), 10);
    if (opened.seen.size() != 10 || !opened.seen.back().bar.fileOpen)
    {
        return expect(false, "Fichier : un clic sur le titre ouvre le menu");
    }
    const ImVec2 save = opened.seen.back().bar.save;
    const Run run = play(backend, clickScript(1, file) + clickScript(7, save), 16);
    if (run.seen.size() != 16)
    {
        return expect(false, "Fichier : une image relevée par image");
    }
    const Seen& last = run.seen.back();
    return expect(run.seen.at(6).bar.fileOpen, "Fichier : le menu est ouvert avant le clic") +
           expect(last.actions.declined.at(indexOf(Action::Save)) == 0 &&
                      last.actions.performed.at(indexOf(Action::Save)) == 0,
                  "Fichier > Enregistrer, grisé, ne fait rien au clic") +
           expect(last.status != SaveSaid, "Fichier > Enregistrer : la console ne dit rien");
}

/// Une touche d'outil dans la Vue atteint son action (grisée jusqu'à M7.4, elle le dit) ; en vol
/// (clic droit tenu), aucune : W vole, R et Espace se taisent (la Vue se tait en vol) ; avec Ctrl
/// non plus ; le vol fini, W et R prennent de nouveau leur outil.
int playToolKeys(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8 || probe.seen.back().image.width <= 0.0f)
    {
        return expect(false, "les outils : le passage qui lit la Vue a relevé une image par image");
    }
    const ImVec2 view = centerOf(probe.seen.back().image);
    const std::string script = std::format("0 mouse move {} {}\n"
                                           "3 key down W\n4 key up W\n" // l'outil Déplacer
                                           "6 button down right\n"      // le vol commence
                                           "7 key down W\n8 key up W\n" // le vol : pas d'outil
                                           "8 key down R\n9 key up R\n" // en vol, la Vue se tait
                                           "9 key down Space\n10 key up Space\n" // ni outil suivant
                                           "11 button up right\n"
                                           "13 key down Left Ctrl\n13 key down W\n" // Ctrl+W
                                           "14 key up W\n14 key up Left Ctrl\n"     // pas d'outil
                                           "16 key down W\n17 key up W\n"  // l'outil, de nouveau
                                           "19 key down R\n20 key up R\n", // et R, hors du vol
                                           view.x, view.y);
    const Run run = play(backend, script, 24);
    if (run.exitCode != 0 || run.seen.size() != 24)
    {
        return expect(false, "les outils : code 0 et une image relevée par image");
    }
    const auto declined = [&run](int frame, Action action)
    { return run.seen.at(static_cast<std::size_t>(frame)).actions.declined.at(indexOf(action)); };
    const auto moves = [&declined](int frame) { return declined(frame, Action::ToolMove); };
    return expect(moves(2) == 0, "les outils : rien avant la touche") +
           expect(moves(5) == 1, "une touche d'outil dans la Vue atteint son action") +
           expect(run.seen.at(8).flying, "les outils : le clic droit tenu dans la Vue vole") +
           expect(moves(12) == 1,
                  "clic droit tenu, la touche d'outil vole : elle ne prend pas l'outil") +
           expect(declined(12, Action::ToolScale) == 0 && declined(12, Action::NextTool) == 0,
                  "en vol, R et Espace ne prennent pas d'outil : la Vue se tait") +
           expect(moves(15) == 1, "avec Ctrl, la touche W n'est pas l'outil") +
           expect(moves(18) == 2, "relâchée, la touche d'outil prend de nouveau l'outil") +
           expect(declined(23, Action::ToolScale) == 1, "hors du vol, R prend l'outil");
}

/// Une fenêtre posée sur la Vue la prive de W : l'Aide ouverte, la souris sur sa recherche (qui est
/// au-dessus de l'image de la Vue), W ne prend pas l'outil Déplacer.
int playToolKeyUnderHelp(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8)
    {
        return expect(false, "W sous l'Aide : le passage qui lit la barre a relevé une image");
    }
    const ImVec2 help = probe.seen.back().bar.help;
    const Run opened = play(backend, clickScript(1, help), 10);
    if (opened.seen.size() != 10)
    {
        return expect(false, "W sous l'Aide : une image relevée par image");
    }
    const ImVec2 entry = opened.seen.back().bar.helpShortcuts;
    const std::string open = clickScript(1, help) + clickScript(7, entry);
    const Run shown = play(backend, open, 16);
    if (shown.seen.size() != 16 || !shown.seen.back().help.open)
    {
        return expect(false, "W sous l'Aide : Aide > Raccourcis ouvre la fenêtre");
    }
    const ImVec2 search = shown.seen.back().help.searchAt;
    const auto& image = shown.seen.back().image;
    if (!(search.x >= image.x && search.x < image.x + image.width && search.y >= image.y &&
          search.y < image.y + image.height))
    {
        return expect(false, "W sous l'Aide : la recherche de l'Aide est au-dessus de la Vue");
    }
    const Run run = play(
        backend,
        open + std::format("14 mouse move {} {}\n16 key down W\n17 key up W\n", search.x, search.y),
        22);
    if (run.exitCode != 0 || run.seen.size() != 22)
    {
        return expect(false, "W sous l'Aide : code 0 et une image relevée par image");
    }
    return expect(run.seen.back().actions.declined.at(indexOf(Action::ToolMove)) == 0 &&
                      run.seen.back().actions.performed.at(indexOf(Action::ToolMove)) == 0,
                  "W sous l'Aide, posée sur la Vue, ne prend pas l'outil");
}

/// La barre d'outils règle la vitesse de la caméra de l'éditeur : un clic vers le bout du curseur.
int playSpeed(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8)
    {
        return expect(false,
                      "la vitesse : le passage qui lit la barre a relevé une image par image");
    }
    const levain::editor::ToolbarLayout toolbar = probe.seen.back().toolbar;
    const ImVec2 target{toolbar.speed.x + toolbar.speedWidth * 0.45f, toolbar.speed.y};
    const Run run = play(backend, clickScript(1, target), 10);
    if (run.exitCode != 0 || run.seen.size() != 10)
    {
        return expect(false, "la vitesse : code 0 et une image relevée par image");
    }
    return expect(run.seen.front().flySpeed == levain::editor::DefaultFlySpeed,
                  "la vitesse de la caméra part de sa valeur par défaut") +
           expect(run.seen.back().flySpeed > levain::editor::DefaultFlySpeed &&
                      run.seen.back().flySpeed <= levain::editor::MaxFlySpeed,
                  "la barre d'outils règle la vitesse de la caméra de l'éditeur, bornée");
}

constexpr int FlightFrames = 10;

/// Le vol de l'image `from` : le curseur au centre de la Vue, le clic droit, W tenu
/// `FlightFrames` images, puis tout relâché.
std::string flightScript(int from, ImVec2 view)
{
    return std::format("{} mouse move {} {}\n{} button down right\n{} key down W\n{} key up W\n"
                       "{} button up right\n",
                       from, view.x, view.y, from + 2, from + 3, from + 3 + FlightFrames,
                       from + 4 + FlightFrames);
}

/// **Une seule caméra** : la vitesse que règle la barre d'outils est celle du vol. Le même vol
/// (clic droit tenu dans la Vue, W tenu) va plus loin après un clic sur le curseur, dans le
/// rapport des vitesses : sous `--steps`, une image de vol dure le pas fixe sur toutes les
/// machines (`App::frameSeconds`).
int playSpeedFlies(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8 || probe.seen.back().image.width <= 0.0f)
    {
        return expect(false, "la vitesse en vol : le passage qui lit la barre a relevé une image");
    }
    const levain::editor::ToolbarLayout toolbar = probe.seen.back().toolbar;
    const ImVec2 target{toolbar.speed.x + toolbar.speedWidth * 0.45f, toolbar.speed.y};
    const ImVec2 view = centerOf(probe.seen.back().image);
    constexpr int From = 8;
    constexpr int Frames = From + FlightFrames + 8;
    const Run slow = play(backend, flightScript(From, view), Frames);
    const Run fast = play(backend, clickScript(1, target) + flightScript(From, view), Frames);
    if (slow.exitCode != 0 || fast.exitCode != 0 || slow.seen.size() != Frames ||
        fast.seen.size() != Frames)
    {
        return expect(false, "la vitesse en vol : code 0 et une image relevée par image");
    }
    const auto travelled = [](const Run& run)
    { return glm::length(run.seen.back().position - run.seen.at(From).position); };
    const float slowSpeed = slow.seen.at(From).flySpeed;
    const float fastSpeed = fast.seen.at(From).flySpeed;
    const float ratio = travelled(slow) > 0.0f ? travelled(fast) / travelled(slow) : 0.0f;
    return expect(slowSpeed == levain::editor::DefaultFlySpeed && fastSpeed > 2.0f * slowSpeed,
                  "la vitesse en vol : le curseur l'a montée au moins au double") +
           expect(slow.seen.at(From + 6).flying && fast.seen.at(From + 6).flying,
                  "la vitesse en vol : les deux passages volent") +
           expect(travelled(slow) > 0.0f, "la vitesse en vol : le vol de référence avance") +
           expect(std::abs(ratio - fastSpeed / slowSpeed) < 0.01f * (fastSpeed / slowSpeed),
                  "la vitesse de la barre d'outils est celle du vol : le même vol va plus loin, "
                  "dans le rapport des vitesses");
}

/// La modale « Quitter ? » (Fichier > Quitter l'éditeur) ouverte : la case « Temps réel » de la
/// barre d'outils, son curseur de vitesse, le menu Fichier, Ctrl+S et la touche d'outil dans la Vue
/// ne font rien. La modale bloque les clics des fenêtres derrière elle, et les raccourcis de
/// l'éditeur se taisent sous elle (`globalChordsAllowed`) : ImGui laisse passer la route globale.
int playQuitModalBlocks(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8 || probe.seen.back().image.width <= 0.0f)
    {
        return expect(false, "sous la modale : le passage qui lit la barre a relevé une image");
    }
    const Seen& layout = probe.seen.back();
    const Run opened = play(backend, clickScript(1, layout.bar.file), 10);
    if (opened.seen.size() != 10 || !opened.seen.back().bar.fileOpen)
    {
        return expect(false, "sous la modale : un clic sur Fichier ouvre le menu");
    }
    const ImVec2 view = centerOf(layout.image);
    const ImVec2 speed{layout.toolbar.speed.x + layout.toolbar.speedWidth * 0.45f,
                       layout.toolbar.speed.y};
    const std::string script =
        clickScript(1, layout.bar.file) + clickScript(7, opened.seen.back().bar.quit) +
        clickScript(12, layout.toolbar.realTime) + clickScript(16, speed) +
        clickScript(20, layout.bar.file) +
        "25 key down Left Ctrl\n25 key down S\n26 key up S\n26 key up Left Ctrl\n" +
        std::format("28 mouse move {} {}\n30 key down W\n31 key up W\n", view.x, view.y);
    const Run run = play(backend, script, 34);
    if (run.exitCode != 0 || run.seen.size() != 34 || run.realTime.size() != 34)
    {
        return expect(false, "sous la modale : code 0 et une image relevée par image");
    }
    const Seen& last = run.seen.back();
    bool fileOpenUnder = false;
    for (std::size_t frame = 11; frame < run.seen.size(); ++frame)
    {
        fileOpenUnder = fileOpenUnder || run.seen.at(frame).bar.fileOpen;
    }
    return expect(run.seen.at(10).quitOpen && last.quitOpen,
                  "Fichier > Quitter l'éditeur ouvre la modale, qui reste ouverte") +
           expect(run.realTime.back(), "sous la modale, la case « Temps réel » ne décoche rien") +
           expect(last.flySpeed == levain::editor::DefaultFlySpeed,
                  "sous la modale, le curseur de vitesse ne bouge pas") +
           expect(!fileOpenUnder, "sous la modale, le menu Fichier ne s'ouvre pas") +
           expect(last.actions.declined.at(indexOf(Action::Save)) == 0 &&
                      last.actions.declined.at(indexOf(Action::ToolMove)) == 0,
                  "sous la modale, ni Ctrl+S ni la touche d'outil n'atteignent leur action");
}

/// « Temps réel » : la case de la barre d'outils, puis Ctrl+R dans la Vue, figent le temps de la
/// scène que lit `record`, et le rendent.
int playRealTimeToggle(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8 || probe.seen.back().image.width <= 0.0f)
    {
        return expect(false,
                      "temps réel : le passage qui lit la barre a relevé une image par image");
    }
    const ImVec2 box = probe.seen.back().toolbar.realTime;
    const ImVec2 view = centerOf(probe.seen.back().image);
    // La case décoche à l'image 3 (figé dès la 4), Ctrl+R dans la Vue recoche à l'image 10.
    const std::string script =
        clickScript(1, box) + std::format("8 mouse move {} {}\n"
                                          "10 key down Left Ctrl\n10 key down R\n"
                                          "11 key up R\n11 key up Left Ctrl\n",
                                          view.x, view.y);
    const Run run = play(backend, script, 20);
    if (run.exitCode != 0 || run.sceneSeconds.size() != 20 || run.realTime.size() != 20)
    {
        return expect(false, "temps réel : code 0 et une image enregistrée par image");
    }
    const auto at = [&run](int frame)
    { return run.sceneSeconds.at(static_cast<std::size_t>(frame)); };
    bool frozen = true;
    for (int i = 5; i < 10; ++i)
    {
        frozen = frozen && std::abs(at(i) - at(5)) < 1.0e-6;
    }
    return expect(run.realTime.at(0) && !run.realTime.at(6),
                  "temps réel : la case de la barre d'outils le décoche") +
           expect(at(3) > at(0), "temps réel : avant la case, le temps de la scène court") +
           expect(frozen, "temps réel décoché en Édition : le temps de la scène est figé") +
           expect(run.realTime.at(15), "temps réel : Ctrl+R dans la Vue le recoche") +
           expect(at(19) > at(12), "temps réel : recoché, le temps de la scène repart");
}

/// « Temps réel » ne se règle qu'en Édition : décoché, Alt+P (jouer) le force à vrai et le temps de
/// la scène court ; Ctrl+R en jeu ne le bascule pas ; Échap, de retour en Édition, rend le choix
/// décoché.
int playRealTimeInPlay(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8 || probe.seen.back().image.width <= 0.0f)
    {
        return expect(false, "temps réel en jeu : le passage qui lit la barre a relevé une image");
    }
    const ImVec2 box = probe.seen.back().toolbar.realTime;
    const ImVec2 view = centerOf(probe.seen.back().image);
    const std::string script =
        clickScript(1, box) + // décoché à l'image 3
        "8 key down Left Alt\n8 key down P\n9 key up P\n9 key up Left Alt\n" + // on joue
        std::format("11 mouse move {} {}\n"
                    "13 key down Left Ctrl\n13 key down R\n14 key up R\n14 key up Left Ctrl\n",
                    view.x, view.y) +
        "17 key down Escape\n18 key up Escape\n"; // retour en Édition
    const Run run = play(backend, script, 26);
    if (run.exitCode != 0 || run.sceneSeconds.size() != 26 || run.realTime.size() != 26 ||
        run.seen.size() != 26)
    {
        return expect(false, "temps réel en jeu : code 0 et une image enregistrée par image");
    }
    const auto at = [&run](int frame)
    { return run.sceneSeconds.at(static_cast<std::size_t>(frame)); };
    return expect(!run.realTime.at(6), "temps réel en jeu : décoché en Édition") +
           expect(run.seen.at(12).mode == levain::editor::Mode::Play,
                  "temps réel en jeu : Alt+P a joué") +
           expect(run.realTime.at(12) && run.realTime.at(16),
                  "en jeu, le temps réel est forcé, et Ctrl+R ne le bascule pas") +
           expect(at(16) > at(11), "en jeu, le temps de la scène court malgré le choix décoché") +
           expect(run.seen.at(24).mode == levain::editor::Mode::Edit && !run.realTime.at(24),
                  "de retour en Édition, le choix décoché est rendu");
}

/// Aide > Raccourcis liste chaque raccourci de la table, avec la touche de la disposition d'un
/// AZERTY.
int playHelp(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8, true);
    if (probe.seen.size() != 8)
    {
        return expect(false, "l'Aide : le passage qui lit la barre a relevé une image par image");
    }
    const ImVec2 help = probe.seen.back().bar.help;
    const Run opened = play(backend, clickScript(1, help), 10, true);
    if (opened.seen.size() != 10 || !opened.seen.back().bar.helpOpen)
    {
        return expect(false, "l'Aide : un clic sur le titre ouvre le menu");
    }
    const ImVec2 entry = opened.seen.back().bar.helpShortcuts;
    const Run run = play(backend, clickScript(1, help) + clickScript(7, entry), 18, true);
    if (run.exitCode != 0 || run.seen.size() != 18)
    {
        return expect(false, "l'Aide : code 0 et une image relevée par image");
    }
    const levain::editor::HelpState& state = run.seen.back().help;
    const auto forward =
        std::ranges::find(state.shown, Action::MoveForward, &levain::editor::HelpRow::action);
    return expect(!run.seen.at(6).help.open, "l'Aide : fermée avant le clic") +
           expect(state.open && state.total == static_cast<std::size_t>(Action::Count) &&
                      state.shown.size() == state.total,
                  "Aide > Raccourcis ouvre la fenêtre, qui liste chaque action de la table") +
           expect(forward != state.shown.end() && forward->chords == "Z",
                  "l'Aide montre « Z » pour Avancer sous une paire AZERTY");
}

int playEditorMenus(const levain::tests::TestBackend& backend)
{
    return playSave(backend) + playSaveMenu(backend) + playToolKeys(backend) +
           playToolKeyUnderHelp(backend) + playSpeed(backend) + playSpeedFlies(backend) +
           playQuitModalBlocks(backend) + playRealTimeToggle(backend) +
           playRealTimeInPlay(backend) + playHelp(backend);
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
            std::println(stderr, "usage : levain_editor_menus [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = playRealTime(*backend) + playEditorMenus(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
