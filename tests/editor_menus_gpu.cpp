// Les menus, les barres et le temps réel de l'éditeur dans la vraie boucle (ADR-0036, décisions 9,
// 12 et 13, morceau 12), un script d'input rejoué hors écran (le banc d'essai de l'App) :
//   - « Temps réel » décoché fige le temps de la scène que lit `record` (l'eau, l'herbe et les
//     matériaux), recoché il repart sans saut ; jouer le force, revenir à l'Édition rend le choix ;
//   - cliqué dans le menu Fichier, Enregistrer grisé ne fait rien ;
//   - la barre d'outils règle la vitesse de la caméra de l'éditeur, bornée ;
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

constexpr const char* NoInput = "0 mouse move 1 1\n"; // un script vide est refusé (règle n°7)
constexpr std::string_view SaveSaid = "Enregistrer : arrive en M7.3";

using levain::editor::Action;
using levain::editor::indexOf;

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

/// « Temps réel » ne se règle qu'en Édition : décoché, Alt+P (jouer) le force à vrai et le temps de
/// la scène court ; Échap, de retour en Édition, rend le choix décoché.
int playRealTimeInPlay(const levain::tests::TestBackend& backend)
{
    const Run probe = play(backend, NoInput, 8);
    if (probe.seen.size() != 8 || probe.seen.back().image.width <= 0.0f)
    {
        return expect(false, "temps réel en jeu : le passage qui lit la barre a relevé une image");
    }
    const ImVec2 box = probe.seen.back().toolbar.realTime;
    const std::string script =
        clickScript(1, box) + // décoché à l'image 3
        "8 key down Left Alt\n8 key down P\n9 key up P\n9 key up Left Alt\n" + // on joue
        "17 key down Escape\n18 key up Escape\n";                              // retour en Édition
    const Run run = play(backend, script, 26);
    if (run.exitCode != 0 || run.sceneSeconds.size() != 26 || run.realTime.size() != 26 ||
        run.seen.size() != 26)
    {
        return expect(false, "temps réel en jeu : code 0 et une image enregistrée par image");
    }
    const auto at = [&run](int frame)
    { return run.sceneSeconds.at(static_cast<std::size_t>(frame)); };
    return expect(!run.realTime.at(6), "temps réel en jeu : décoché en Édition") +
           expect(run.seen.at(12).mode == levain::editor::Mode::PlayWithoutReturn,
                  "temps réel en jeu : Alt+P a joué") +
           expect(run.realTime.at(12) && run.realTime.at(16), "en jeu, le temps réel est forcé") +
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
    return playSaveMenu(backend) + playSpeed(backend) + playRealTimeInPlay(backend) +
           playHelp(backend);
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
