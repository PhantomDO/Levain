// Fermer l'éditeur dans la vraie boucle (ADR-0036, décision 11, morceau 13), un script d'input
// rejoué hors écran (le banc d'essai de l'App) :
//   - sous un lancement scripté (`--input-script`), la croix ferme tout de suite : la modale ne
//     s'ouvre jamais ;
//   - avec `AppSettings::testInteractive` (le crochet des tests : un humain devant sa fenêtre), la
//     croix ouvre la modale « Quitter l'éditeur ? », une seule même si la croix revient pendant
//     qu'elle est ouverte ; Annuler garde l'éditeur (et une croix de plus la rouvre), Quitter
//     l'arrête ;
//   - un signal (SIGTERM, vrai, levé par le test) arrête sans modale, même modale ouverte ;
//   - sous la modale ouverte, le jeu ne reçoit ni clic ni touche, Alt+P ne joue pas, et Échap
//     l'annule sans arrêter le jeu ;
//   - un titre hors ASCII n'atteint pas la fenêtre : ni au démarrage, ni quand la boucle le
//     réécrit chaque seconde.
// Il faut un device, comme `levain_app_script`.
//   levain_editor_close [vulkan|d3d12|d3d12-warp]

#include <csignal>
#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/mode.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/platform/window.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;

constexpr int Frames = 16;

struct Run
{
    int exitCode = 0;
    int frames = 0;               ///< les images où `ui` a tourné
    int timesOpened = 0;          ///< `QuitState::timesOpened` à la fin
    bool modalOpen = false;       ///< la modale est ouverte à la fin
    levain::app::ScreenRect quit; ///< ses boutons, tels que la dernière image les a dessinés
    levain::app::ScreenRect cancel;
    std::string title;      ///< le titre de la fenêtre à la fin de la boucle
    std::string startTitle; ///< celui du démarrage, avant la première image
    levain::editor::Mode mode = levain::editor::Mode::Edit; ///< le mode à la fin
    int gameFrames = 0;        ///< les images où le `frame` du programme a joué (le jeu joue)
    bool gameSawClick = false; ///< le jeu a vu le bouton gauche
    bool gameSawKey = false;   ///< le jeu a vu une touche tenue
};

struct Scenario
{
    std::string_view script;
    bool interactive = false; ///< `testInteractive`
    int raiseAt = -1;         ///< lève SIGTERM dans la `ui` de cette image
    std::string title = "Levain";
    double seconds = 0.0; ///< au lieu de `Frames` images : une durée
};

/// Le programme : une caméra, et une `ui` qui compte ses images et lève le signal du scénario.
levain::app::StartFunction programOf(Run& run, int raiseAt, std::string& titleSeen)
{
    return [&run, raiseAt, &titleSeen](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        run.startTitle = levain::platform::windowTitle(app.window);
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        // Épinglée : une vraie fenêtre lirait la disposition de la machine (AZERTY chez Donnovan).
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition;
        levain::app::FrameHooks hooks;
        // Ne joue qu'en jeu (`withEditor`) : ce qu'il y voit est ce que le jeu recevrait.
        hooks.frame = [&run](App& frameApp)
        {
            ++run.gameFrames;
            run.gameSawClick = run.gameSawClick || frameApp.input.raw.mouseButtons.test(1);
            run.gameSawKey = run.gameSawKey || frameApp.input.raw.keys.any();
        };
        hooks.ui = [&run, raiseAt](App&)
        {
            if (run.frames == raiseAt)
            {
                (void)std::raise(SIGTERM); // SDL le traduit en QUIT à la prochaine pompe
            }
            ++run.frames;
        };
        hooks.finish = [&titleSeen](App& endApp)
        {
            titleSeen = levain::platform::windowTitle(endApp.window);
            return true;
        };
        return hooks;
    };
}

Run play(const levain::tests::TestBackend& backend, const Scenario& scenario)
{
    auto parsed = levain::platform::parseInputScript(scenario.script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    if (scenario.seconds > 0.0)
    {
        settings.loopSeconds = scenario.seconds;
    }
    // Sans `--steps` : le script mène la boucle jusqu'à sa dernière image (15). Une boucle arrêtée
    // avant elle sort en erreur (règle n°7, app.cpp) : c'est le code 1 des scénarios qui ferment.
    settings.testInteractive = scenario.interactive;
    settings.title = scenario.title;
    settings.width = 640;
    settings.height = 360;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    levain::editor::EditorOptions options;
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.afterUi = [&run](const levain::editor::Editor& editor)
    {
        run.timesOpened = editor.quit.timesOpened;
        run.modalOpen = editor.quit.open;
        run.mode = editor.mode.current;
        run.quit = editor.quit.quitButton;
        run.cancel = editor.quit.cancelButton;
    };
    run.exitCode = levain::app::runApp(
        settings, levain::editor::withEditor(programOf(run, scenario.raiseAt, run.title),
                                             std::move(options)));
    return run;
}

/// Un script de la fenêtre interactive, sans autre réglage.
Run playScript(const levain::tests::TestBackend& backend, const std::string& script,
               bool interactive)
{
    return play(backend, {.script = script, .interactive = interactive});
}

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

/// Le milieu d'un bouton, en pixels de l'image : où la souris scriptée clique.
std::string centerOf(const levain::app::ScreenRect& rect)
{
    return std::format("{} {}", rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f);
}

/// Une croix à l'image 3 et une autre à la 5, modale ouverte, puis un clic sur le bouton à l'image
/// 7 (appui) et 8 (relâchement). Annuler : les `Frames` images se jouent, et la modale est fermée.
/// Quitter : la boucle s'arrête après le clic.
int closeModal(const levain::tests::TestBackend& backend)
{
    const std::string_view twoCloses = "3 window close\n5 window close\n";
    // Un premier passage lit les boutons, que la disposition place au centre de la fenêtre.
    const Run probe = play(backend, {.script = "3 window close\n5 window close\n"
                                               "15 mouse move 1 1\n",
                                     .interactive = true});
    int failures = expect(probe.exitCode == 0 && probe.frames == Frames,
                          "le passage qui lit les boutons joue ses images, modale ouverte");
    failures += expect(probe.timesOpened == 1 && probe.modalOpen,
                       "deux croix, modale ouverte entre elles : une seule modale, jamais empilée");
    failures += expect(probe.quit.width > 0.0f && probe.cancel.width > 0.0f &&
                           probe.quit.x < probe.cancel.x,
                       "la modale dessine Quitter puis Annuler");
    const auto click = [&](const levain::app::ScreenRect& button)
    {
        return std::format("{}7 mouse move {}\n7 button down left\n8 button up left\n", twoCloses,
                           centerOf(button));
    };
    // Annuler : l'éditeur reste, la modale est fermée.
    const Run cancel =
        play(backend, {.script = click(probe.cancel) + "15 mouse move 1 1\n", .interactive = true});
    failures += expect(cancel.exitCode == 0 && cancel.frames == Frames,
                       "Annuler : l'éditeur continue jusqu'à la fin de ses images");
    failures +=
        expect(cancel.timesOpened == 1 && !cancel.modalOpen,
               "Annuler : une modale ouverte, puis fermée, sans qu'une seconde croix la rouvre");
    // Une croix de plus, la modale fermée, en rouvre une.
    const Run again =
        play(backend, {.script = click(probe.cancel) + "10 window close\n15 mouse move 1 1\n",
                       .interactive = true});
    failures += expect(again.timesOpened == 2 && again.modalOpen && again.frames == Frames,
                       "après Annuler, une croix rouvre la modale");
    // Quitter : la boucle s'arrête à l'image du clic, bien avant la fin.
    const Run quit =
        play(backend, {.script = click(probe.quit) + "15 mouse move 1 1\n", .interactive = true});
    failures += expect(quit.exitCode == 1 && quit.frames > 8 && quit.frames < Frames,
                       "Quitter arrête la boucle à l'image du clic");
    return failures;
}

/// Sans le crochet des tests, `--input-script` rend la run scriptée : la croix ferme tout de
/// suite, la modale ne s'ouvre pas. C'est la première image qui suit la croix qui est la dernière.
int closeScripted(const levain::tests::TestBackend& backend)
{
    const Run run = play(backend, {.script = "3 window close\n15 mouse move 1 1\n"});
    return expect(run.exitCode == 1,
                  "scripté : la boucle s'arrête avant la fin du script, code 1 (règle n°7)") +
           expect(run.frames == 4, "scripté : la croix de l'image 3 arrête la boucle aussitôt") +
           expect(run.timesOpened == 0 && !run.modalOpen, "scripté : la modale ne s'ouvre jamais");
}

/// SIGTERM, levé dans la `ui` de l'image 5 : la pompe de l'image 6 le reçoit, et la boucle s'arrête
/// après elle, sans modale. Une fois la modale ouverte (croix à l'image 3), le signal l'emporte
/// aussi : on quitte sans demander.
int signalQuits(const levain::tests::TestBackend& backend)
{
    const Run bare =
        play(backend, {.script = "15 mouse move 1 1\n", .interactive = true, .raiseAt = 5});
    const Run withModal =
        play(backend,
             {.script = "3 window close\n15 mouse move 1 1\n", .interactive = true, .raiseAt = 8});
    return expect(bare.exitCode == 1 && bare.frames == 7,
                  "SIGTERM arrête la boucle à l'image suivante") +
           expect(bare.timesOpened == 0, "SIGTERM n'ouvre aucune modale") +
           expect(withModal.exitCode == 1 && withModal.frames == 10 && withModal.timesOpened == 1,
                  "SIGTERM modale ouverte : on quitte sans attendre de réponse");
}

/// Alt+P à l'image 1 : le jeu joue dès la 2. La croix à la 3 ouvre la modale ; un clic gauche et la
/// touche W aux images 4 et 5, loin des boutons, n'atteignent pas le jeu (la route de l'UI, dès
/// l'image qui suit l'ouverture). Le témoin, sans croix : le jeu les voit, le banc les atteint.
int nothingBehindModal(const levain::tests::TestBackend& backend)
{
    const std::string_view startPlay =
        "1 key down Left Alt\n1 key down P\n2 key up P\n2 key up Left Alt\n"
        "3 mouse move 5 5\n";
    const std::string_view poke = "4 button down left\n4 key down W\n5 button up left\n5 key up W\n"
                                  "15 mouse move 1 1\n";
    const Run control = playScript(backend, std::string{startPlay} + std::string{poke}, true);
    const Run behind =
        playScript(backend, std::string{startPlay} + "3 window close\n" + std::string{poke}, true);
    return expect(control.gameFrames > 0 && control.gameSawClick && control.gameSawKey,
                  "témoin : sans modale, le jeu voit le clic et la touche") +
           expect(behind.gameFrames > 0 && behind.mode == levain::editor::Mode::Play,
                  "modale : le jeu joue bien jusque-là") +
           expect(behind.modalOpen && behind.timesOpened == 1, "modale : toujours ouverte") +
           expect(!behind.gameSawClick, "modale : le clic derrière elle n'atteint pas le jeu") +
           expect(!behind.gameSawKey, "modale : la touche derrière elle n'atteint pas le jeu");
}

/// Alt+P, modale ouverte, ne joue pas (le témoin sans croix, si). Le raccourci est global : ImGui
/// le laisserait passer.
int chordSilentUnderModal(const levain::tests::TestBackend& backend)
{
    const std::string_view chord = "5 key down Left Alt\n5 key down P\n6 key up P\n"
                                   "6 key up Left Alt\n15 mouse move 1 1\n";
    const Run control = playScript(backend, std::string{chord}, true);
    const Run modal = playScript(backend, "3 window close\n" + std::string{chord}, true);
    return expect(control.mode == levain::editor::Mode::Play, "témoin : sans modale, Alt+P joue") +
           expect(modal.mode == levain::editor::Mode::Edit && modal.modalOpen &&
                      modal.gameFrames == 0,
                  "modale : Alt+P ne joue pas");
}

/// Échap annule la modale. En Édition il n'a rien d'autre à faire ; en jeu, il n'arrête pas le jeu
/// (l'UI a le clavier), et le suivant, modale fermée, l'arrête.
int escapeCancels(const levain::tests::TestBackend& backend)
{
    const std::string_view escape = "6 key down Escape\n7 key up Escape\n";
    const Run edit =
        playScript(backend, "3 window close\n" + std::string{escape} + "15 mouse move 1 1\n", true);
    const std::string_view playing = "1 key down Left Alt\n1 key down P\n2 key up P\n"
                                     "2 key up Left Alt\n3 window close\n";
    const Run inPlay = playScript(
        backend, std::string{playing} + std::string{escape} + "15 mouse move 1 1\n", true);
    const Run then = playScript(backend,
                                std::string{playing} + std::string{escape} +
                                    "10 key down Escape\n11 key up Escape\n15 mouse move 1 1\n",
                                true);
    return expect(edit.exitCode == 0 && edit.frames == Frames && edit.timesOpened == 1 &&
                      !edit.modalOpen,
                  "Échap ferme la modale, en Édition") +
           expect(inPlay.timesOpened == 1 && !inPlay.modalOpen &&
                      inPlay.mode == levain::editor::Mode::Play,
                  "Échap ferme la modale en jeu, sans arrêter le jeu") +
           expect(then.mode == levain::editor::Mode::Edit,
                  "témoin : l'Échap suivant, modale fermée, arrête le jeu");
}

/// Un titre hors ASCII (un nom de scène, plus tard) n'atteint pas la fenêtre : la boucle réécrit le
/// titre toutes les secondes, et `setWindowTitle` n'accepte que l'ASCII.
int titleStaysAscii(const levain::tests::TestBackend& backend)
{
    const Run run = play(backend, {.script = "0 mouse move 1 1\n",
                                   .title = "Caf\xC3\xA9 \xE2\x80\x94 \xC3\xA9"
                                            "diteur",
                                   .seconds = 1.4});
    bool ascii = true;
    for (const char c : run.title)
    {
        ascii = ascii && static_cast<unsigned char>(c) < 0x80;
    }
    return expect(run.exitCode == 0, "titre : code 0") +
           expect(run.startTitle == "Caf? ? ?diteur",
                  std::format("titre : ASCII dès la création de la fenêtre (« {} »)",
                              run.startTitle)) +
           expect(run.title.starts_with("Caf? ? ?diteur - "),
                  std::format(
                      "titre : réécrit par la boucle, caractères hors ASCII remplacés (« {} »)",
                      run.title)) +
           expect(ascii, "titre : de l'ASCII");
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
            std::println(stderr, "usage : levain_editor_close [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = closeScripted(*backend) + closeModal(*backend) +
                             signalQuits(*backend) + nothingBehindModal(*backend) +
                             chordSilentUnderModal(*backend) + escapeCancels(*backend) +
                             titleStaysAscii(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
