// La fenêtre « Raccourcis » dans la vraie boucle (ADR-0036, décision 13, morceau 16) : `withEditor`
// autour d'un programme minimal, un script d'input rejoué hors écran, **un dossier de préférences
// jetable** (`EditorOptions::preferencesDirectory`) et `AppSettings::testInteractive` (une
// exécution scriptée n'a pas de préférences). Le clavier est épinglé (QWERTY, puis un AZERTY quand
// le scénario le demande) : un écran réel lirait celui de la machine. Un scénario par exécution,
// cinq lancements de l'application au plus (la CI donne 120 s par test) :
//   change         un accord changé par clic et touche est enregistré tout de suite, relu au
//                  redémarrage : le nouveau part, l'ancien non
//   conflict       un conflit est nommé et ne change rien ; « Annuler » l'abandonne, « Remplacer »
//                  le tranche et l'enregistre
//   capture        pendant la saisie Ctrl+S n'enregistre pas, Alt+P ne joue pas, F1 est refusée et
//                  ne ferme pas les panneaux, Échap annule
//   reset          « Réinitialiser » rend les défauts d'une action, « Tout réinitialiser »
//                  (confirmé) ceux de toutes ; le fichier n'écrit plus rien
//   menu           Édition > Préférences > Raccourcis… ouvre la fenêtre, qui liste chaque
//                  action ; le filtre répond au nom et à l'accord
//   scripted       une exécution scriptée garde les défauts et n'écrit rien
//   layout         un changement de disposition qui crée un conflit le dit à la console, sans
//                  rien rebrancher
//   play           une saisie ne survit pas à « Jouer », et Échap arrête le jeu
//   focus          la saisie s'arrête quand un champ de texte prend le clavier, ou que la fenêtre
//                  se ferme
//   leak           la touche qui finit une saisie ne part pas aussi comme raccourci, et pendant
//                  la saisie la molette sur la Vue ne déplace pas la caméra
//   azerty         sur un AZERTY une position garde son scancode et une lettre son caractère, au
//                  fichier et au vol après un redémarrage
//   resetconflict  « Réinitialiser » d'un défaut pris nomme le conflit, rien ne change sans
//                  « Remplacer »
//   held           une action tenue (le vol, Arrêter) ne lit aucun modificateur : Maj+E pour «
//   Avancer »
//                  nomme « E : Monter », et le fichier qui lui en met un est refusé, dit et gardé
//   refusedreset   une ligne refusée au chargement ne revient pas une fois son action écrite par la
//                  fenêtre (changer puis « Réinitialiser »)
//   levain_editor_shortcut_settings <vulkan|d3d12|d3d12-warp> <scénario>
//
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/assets/asset_id.hpp"
#include "levain/core/log.hpp"
#include "levain/editor/camera.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/preferences.hpp"
#include "levain/editor/shortcut_window.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"
#include "levain/ui/tr.hpp"

namespace
{

namespace fs = std::filesystem;
using levain::app::App;
using levain::editor::Action;
using levain::editor::Editor;
using levain::editor::Preferences;
using levain::editor::ShortcutRowSeen;

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 800;
/// Un script que rien ne lit : un mouvement de souris loin de tout (un script vide est refusé).
constexpr std::string_view Idle = "0 mouse move 1 1\n";

/// Un AZERTY pour ce qui change : W et Z, A et Q.
std::uint32_t azertyAt(std::uint16_t scancode)
{
    constexpr std::uint16_t ScanA = 4, ScanQ = 20, ScanW = 26, ScanZ = 29;
    switch (scancode)
    {
    case ScanA:
        return 'q';
    case ScanQ:
        return 'a';
    case ScanW:
        return 'z';
    case ScanZ:
        return 'w';
    default:
        return levain::platform::qwertyKeyAtPosition(scancode);
    }
}

/// Ce que l'éditeur a fait à une image, relevé à la fin de `ui`.
struct Seen
{
    std::string table; ///< la table, une action par ligne (`tokenOf`)
    bool panelsOpen = false;
    bool windowOpen = false;
    bool capturing = false;
    bool pending = false;
    std::string conflict;
    std::string sentence;
    std::string messageLine;
    std::string message;
    levain::editor::Mode mode = levain::editor::Mode::Edit;
    std::vector<ShortcutRowSeen> rows;
    levain::editor::MenuBarLayout bar{};
    ImVec2 replaceAt{};
    ImVec2 cancelAt{};
    ImVec2 resetAllAt{};
    ImVec2 confirmAllAt{};
    bool confirmShown = false;
    std::vector<Action> reached; ///< les actions que les raccourcis seuls ont atteintes
    ImVec2 toolbarMode{};        ///< le bouton de mode de la barre d'outils
    levain::editor::EditorCamera camera;
    levain::app::ScreenRect viewImage{};
    ImVec2 filterAt{};
    ImVec2 windowMax{}; ///< le coin bas droit de la fenêtre « Raccourcis »
};

std::string tableText(const levain::editor::ShortcutTable& table)
{
    std::string text;
    for (const levain::editor::Binding& binding : table.bindings)
    {
        text += std::format("{} = {}, {}\n", levain::editor::nameOf(binding.action),
                            levain::editor::tokenOf(binding.chords.at(0)),
                            levain::editor::tokenOf(binding.chords.at(1)));
    }
    return text;
}

Seen seenOf(const App& app, const Editor& editor)
{
    const auto& window = editor.shortcutWindow;
    return {.table = tableText(editor.shortcuts),
            .panelsOpen = app.ui.panelsOpen,
            .windowOpen = window.open,
            .capturing = window.capturing.has_value(),
            .pending = window.pending.has_value(),
            .conflict = window.conflictShown,
            .sentence = window.conflictSentence,
            .messageLine = window.messageLine,
            .message = window.message != nullptr ? window.message : "",
            .mode = editor.mode.current,
            .rows = window.rows,
            .bar = editor.bar,
            .replaceAt = window.replaceAt,
            .cancelAt = window.cancelAt,
            .resetAllAt = window.resetAllAt,
            .confirmAllAt = window.confirmAllAt,
            .confirmShown = window.confirmShown,
            .reached = editor.lastShortcuts,
            .toolbarMode = editor.toolbar.mode,
            .camera = editor.camera,
            .viewImage = editor.view.image,
            .filterAt = window.filterAt,
            .windowMax = window.windowMax};
}

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    /// Tous les messages du processus, fermeture comprise.
    std::vector<std::string> logged;
    App* app = nullptr;
    /// Les `ui` ont tourné sur cet éditeur, que le banc ouvre ou règle à la place d'un humain.
    Editor* editor = nullptr;
    /// Ce que le fichier tenait à l'image demandée (`Session::peekFileAt`).
    std::string fileAtPeek;
};

/// Comment lancer une session de l'éditeur.
struct Session
{
    std::string script{Idle};
    int frames = 12;
    std::optional<fs::path> directory;
    bool interactive = true;
    /// Un clavier AZERTY (W et Z, A et Q échangés) plutôt que le QWERTY du banc d'essai.
    bool azerty = false;
    /// Ouvre la fenêtre « Raccourcis » à cette image, comme le ferait le menu (testé à part).
    std::optional<std::size_t> openAt;
    /// Déplace la fenêtre une fois ouverte : par défaut elle recouvre le haut de la barre d'outils
    /// (la bordure d'une fenêtre ImGui prend la souris quelques pixels autour d'elle).
    std::optional<ImVec2> windowAt;
    /// Lit le fichier des préférences à cette image : il est écrit tout de suite, pas à la fin.
    std::optional<std::size_t> peekFileAt;
    /// Tourne à la fin de `ui` de chaque image, avec le nombre d'images relevées.
    std::function<void(Run&, std::size_t)> poke;
};

levain::app::StartFunction programOf(Run& run, bool azerty)
{
    return [&run, azerty](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        // Épinglé : un écran réel lirait la disposition de la machine.
        app.keyAtPosition = azerty ? azertyAt : levain::platform::qwertyKeyAtPosition;
        run.app = &app;
        return levain::app::FrameHooks{};
    };
}

fs::path fileIn(const fs::path& directory)
{
    return directory / levain::editor::PreferencesFileName;
}

std::string contentOf(const fs::path& file)
{
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

Run play(const levain::tests::TestBackend& backend, const Session& session)
{
    auto parsed = levain::platform::parseInputScript(session.script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = session.frames;
    settings.testInteractive = session.interactive;
    settings.showUiPanels = true;
    settings.width = WindowWidth;
    settings.height = WindowHeight;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    std::mutex loggedMutex; // les messages de Jolt et du chargeur partent d'autres fils
    const levain::core::LogSinkHandle sink = levain::core::addLogSink(
        [&run, &loggedMutex](const levain::core::LogRecord& record)
        {
            const std::scoped_lock lock{loggedMutex};
            run.logged.push_back(std::string{record.category} + " : " +
                                 std::string{record.message});
        });
    levain::editor::EditorOptions options;
    // Les polices de l'éditeur (morceau 17a) : celles d'assets-cache, que `data/` seul n'a pas.
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.preferencesDirectory = session.directory;
    options.afterUi = [&run, &session](const Editor& editor)
    {
        run.seen.push_back(seenOf(*run.app, editor));
        // Le banc fait ce que fait un humain avec la souris : un éditeur vivant, jamais constant.
        run.editor = const_cast<Editor*>(&editor);
        const std::size_t recorded = run.seen.size();
        if (session.openAt && recorded == *session.openAt)
        {
            run.editor->shortcutWindow.open = true;
        }
        if (session.windowAt && session.openAt && recorded == *session.openAt + 1)
        {
            ImGui::SetWindowPos(levain::ui::labelOf(levain::editor::ShortcutWindowTitle).c_str(),
                                *session.windowAt);
        }
        if (session.peekFileAt && recorded == *session.peekFileAt && session.directory)
        {
            run.fileAtPeek = contentOf(fileIn(*session.directory));
        }
        if (session.poke)
        {
            session.poke(run, recorded);
        }
    };
    run.exitCode = levain::app::runApp(
        settings, levain::editor::withEditor(programOf(run, session.azerty), options));
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

/// Un dossier jetable, vide, retiré à la fin, avec un identifiant au hasard dans le nom (deux ctest
/// en parallèle ne se partagent pas `/tmp`).
struct TempDirectory
{
    fs::path path;

    explicit TempDirectory(std::string_view name)
        : path(fs::temp_directory_path() /
               ("levain-editor-keys-" + std::string{name} + "-" +
                levain::assets::toString(levain::assets::generateAssetId())))
    {
        fs::create_directories(path);
    }

    ~TempDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
};

std::optional<Preferences> savedIn(const fs::path& directory)
{
    const auto loaded = levain::editor::loadPreferences(fileIn(directory));
    if (!loaded || !*loaded)
    {
        return std::nullopt;
    }
    return *loaded;
}

/// Le fichier (écrit aussi à la fermeture) tient au moins une ligne de `[raccourcis]`.
bool shortcutsSavedIn(const fs::path& directory)
{
    const auto saved = savedIn(directory);
    return saved && !saved->shortcuts.empty();
}

bool loggedSays(const Run& run, std::string_view needle)
{
    return std::ranges::any_of(run.logged,
                               [needle](const std::string& line) { return line.contains(needle); });
}

/// La ligne de la table pour cette action, « Annuler = Ctrl+Z, Ctrl+Y ».
std::string tableLine(const Seen& seen, Action action)
{
    const std::string prefix = std::string{levain::editor::nameOf(action)} + " =";
    std::size_t at = 0;
    while (at < seen.table.size())
    {
        const std::size_t end = seen.table.find('\n', at);
        const std::string line = seen.table.substr(at, end - at);
        if (line.starts_with(prefix))
        {
            return line;
        }
        at = end == std::string::npos ? seen.table.size() : end + 1;
    }
    return {};
}

const ShortcutRowSeen* rowOf(const Seen& seen, Action action)
{
    const auto found = std::ranges::find(seen.rows, action, &ShortcutRowSeen::action);
    return found == seen.rows.end() ? nullptr : &*found;
}

/// Les touches d'un accord à modificateur : le modificateur tenu une image avant la lettre,
/// relâché une image après (SDL pose les modificateurs tenus sur la touche qui suit).
std::string modifiedKey(int frame, std::string_view modifier, std::string_view key)
{
    return std::format("{} key down {}\n{} key down {}\n{} key up {}\n{} key up {}\n", frame,
                       modifier, frame + 1, key, frame + 2, key, frame + 3, modifier);
}

std::string ctrlKey(int frame, std::string_view key)
{
    return modifiedKey(frame, "Left Ctrl", key);
}

/// Un clic à `at` : la souris y va à l'image `frame`, le bouton s'enfonce deux images plus tard.
std::string clickScript(int frame, ImVec2 at)
{
    return std::format("{} mouse move {} {}\n{} button down left\n{} button up left\n", frame, at.x,
                       at.y, frame + 2, frame + 3);
}

/// Une touche seule.
std::string tapKey(int frame, std::string_view key)
{
    return std::format("{} key down {}\n{} key up {}\n", frame, key, frame + 1, key);
}

/// Les positions de la fenêtre ouverte : un passage sans script, pour que les suivants la cliquent.
/// **Dans son propre dossier** : une session relit la disposition de la précédente (la taille et
/// le défilement de la fenêtre « Raccourcis »), et ses lignes ne seraient plus où le sondage les a
/// vues.
Seen probeRows(const levain::tests::TestBackend& backend,
               std::optional<ImVec2> windowAt = std::nullopt)
{
    const TempDirectory scratch{"probe"};
    Session session;
    session.directory = scratch.path;
    session.frames = 10;
    session.openAt = 2;
    session.windowAt = windowAt;
    const Run run = play(backend, session);
    if (run.seen.size() != 10 ||
        run.seen.back().rows.size() != static_cast<std::size_t>(Action::Count))
    {
        throw std::runtime_error{"le sondage : la fenêtre ne liste pas toutes les actions"};
    }
    return run.seen.back();
}

ImVec2 cellOf(const Seen& probe, Action action, std::size_t slot = 0)
{
    const ShortcutRowSeen* row = rowOf(probe, action);
    if (row == nullptr)
    {
        throw std::runtime_error{"le sondage : action introuvable"};
    }
    return row->chordAt.at(slot);
}

/// **Changer un accord, redémarrer** : Enregistrer prend Ctrl+K par un clic et une touche ; le
/// fichier est écrit tout de suite ; à la session suivante Ctrl+K enregistre et Ctrl+S non.
int playChange(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"change"};
    const Seen probe = probeRows(backend);
    Session first;
    first.directory = directory.path;
    first.frames = 26;
    first.openAt = 2;
    first.peekFileAt = 22;
    first.script = clickScript(4, cellOf(probe, Action::Save)) + ctrlKey(12, "K");
    const Run one = play(backend, first);
    if (one.exitCode != 0 || one.seen.size() != 26)
    {
        return expect(false, "la première session : code 0 et une image par image");
    }
    int failures = expect(!one.seen[8].pending && one.seen[8].capturing,
                          "le clic sur la cellule démarre la saisie") +
                   expect(!one.seen.back().capturing && !one.seen.back().pending,
                          "sans conflit, l'accord est posé") +
                   expect(tableLine(one.seen.back(), Action::Save) == "Enregistrer = Ctrl+K, ",
                          "Enregistrer a pris Ctrl+K") +
                   expect(one.fileAtPeek.contains("Enregistrer = Ctrl+K"),
                          "le fichier est écrit tout de suite, pas à la fermeture");
    const auto saved = savedIn(directory.path);
    failures += expect(saved && saved->shortcuts.size() == 1 &&
                           saved->shortcuts.front().action == "Enregistrer" &&
                           saved->shortcuts.front().chords == std::vector<std::string>{"Ctrl+K"},
                       "le fichier tient la seule action changée");
    // La session suivante : Ctrl+S d'abord (ne doit rien faire), puis Ctrl+K.
    Session second;
    second.directory = directory.path;
    second.frames = 24;
    second.script = ctrlKey(4, "S") + ctrlKey(12, "K");
    const Run two = play(backend, second);
    if (two.exitCode != 0 || two.seen.size() != 24)
    {
        return failures + expect(false, "la seconde session : code 0 et une image par image");
    }
    std::size_t savesBeforeK = 0;
    std::size_t savesAfterK = 0;
    for (std::size_t i = 0; i < two.seen.size(); ++i)
    {
        const std::size_t saves =
            static_cast<std::size_t>(std::ranges::count(two.seen[i].reached, Action::Save));
        (i < 12 ? savesBeforeK : savesAfterK) += saves;
    }
    return failures +
           expect(tableLine(two.seen.front(), Action::Save) == "Enregistrer = Ctrl+K, ",
                  "la table relue a Ctrl+K dès la première image") +
           expect(savesBeforeK == 0, "l'ancien accord (Ctrl+S) ne fait plus rien") +
           expect(savesAfterK == 1, "le nouvel accord (Ctrl+K) enregistre, une fois");
}

/// **Un conflit** : Annuler sur Ctrl+S est nommé, rien ne change ; « Annuler » l'abandonne ;
/// la seconde fois « Remplacer » donne Ctrl+S à Annuler et l'ôte à Enregistrer, fichier compris.
int playConflict(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"conflict"};
    const Seen rows = probeRows(backend);
    const TempDirectory scratch{"probe-conflict"};
    Session probeConflict;
    probeConflict.directory = scratch.path;
    probeConflict.frames = 22;
    probeConflict.openAt = 2;
    probeConflict.script = clickScript(4, cellOf(rows, Action::Undo)) + ctrlKey(10, "S");
    const Run probed = play(backend, probeConflict);
    if (probed.seen.size() != 22 || !probed.seen.back().pending)
    {
        return expect(false, "le sondage : le conflit n'est pas apparu");
    }
    const ImVec2 cancel = probed.seen.back().cancelAt;
    const ImVec2 replace = probed.seen.back().replaceAt;

    Session real;
    real.directory = directory.path;
    real.frames = 80;
    real.openAt = 2;
    real.script = clickScript(4, cellOf(rows, Action::Undo)) + ctrlKey(10, "S") +
                  clickScript(20, cancel) + clickScript(28, cellOf(rows, Action::Undo)) +
                  ctrlKey(34, "S") + clickScript(44, replace) + ctrlKey(56, "S");
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 80)
    {
        return expect(false, "le conflit : code 0 et une image par image");
    }
    const Seen& waiting = run.seen[18];
    const Seen& cancelled = run.seen[26];
    const Seen& waitingAgain = run.seen[42];
    const Seen& replaced = run.seen[54];
    const std::string defaults = run.seen.front().table;
    int failures =
        expect(waiting.pending && waiting.conflict == "Ctrl+S : Enregistrer",
               "le conflit est nommé « Ctrl+S : Enregistrer »") +
        expect(waiting.sentence ==
                   "Annuler voudrait Ctrl+S, déjà pris, rien ne change : Ctrl+S : Enregistrer",
               "la phrase nomme l'action qui voudrait l'accord, pas seulement celle qui le tient") +
        expect(waiting.table == defaults, "rien ne change tant qu'on n'a pas remplacé") +
        expect(!cancelled.pending && cancelled.table == defaults,
               "« Annuler » abandonne la candidate, la table est inchangée") +
        expect(waitingAgain.pending && waitingAgain.table == defaults,
               "le second conflit attend aussi") +
        expect(!replaced.pending && tableLine(replaced, Action::Undo) == "Annuler = Ctrl+S, " &&
                   tableLine(replaced, Action::Save) == "Enregistrer = , ",
               "« Remplacer » donne Ctrl+S à Annuler et l'ôte à Enregistrer") +
        expect(loggedSays(run, "Ctrl+S : Enregistrer retiré"),
               "la console dit ce qui a été retiré");
    // Après « Remplacer » : Ctrl+S est Annuler, plus Enregistrer.
    const auto after = std::span{run.seen}.subspan(54);
    const bool undid = std::ranges::any_of(
        after, [](const Seen& seen) { return std::ranges::contains(seen.reached, Action::Undo); });
    const bool saved = std::ranges::any_of(
        after, [](const Seen& seen) { return std::ranges::contains(seen.reached, Action::Save); });
    failures += expect(undid, "après « Remplacer », Ctrl+S atteint Annuler") +
                expect(!saved, "après « Remplacer », Ctrl+S n'atteint plus Enregistrer");
    const auto file = savedIn(directory.path);
    return failures + expect(file && file->shortcuts.size() == 2 &&
                                 file->shortcuts.at(0).action == "Enregistrer" &&
                                 file->shortcuts.at(0).chords.empty() &&
                                 file->shortcuts.at(1).action == "Annuler" &&
                                 file->shortcuts.at(1).chords == std::vector<std::string>{"Ctrl+S"},
                             "le fichier tient les deux actions changées");
}

/// **Pendant la saisie rien ne part** : Ctrl+S est la candidate et n'enregistre pas, Alt+P ne
/// joue pas, F1 est refusée sans fermer les panneaux, Échap annule sans rien changer.
int playCapture(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"capture"};
    const Seen rows = probeRows(backend);
    const ImVec2 undo = cellOf(rows, Action::Undo);
    Session real;
    real.directory = directory.path;
    real.frames = 60;
    real.openAt = 2;
    // Fin du scénario : un clic sur « Simuler » puis sur « Rendre la souris », que rien ne lit
    // encore : leurs cases sont grisées, aucune saisie ne démarre.
    real.script = clickScript(4, undo) + ctrlKey(10, "S") + clickScript(16, undo) +
                  "22 key down Left Alt\n23 key down P\n24 key up P\n25 key up Left Alt\n" +
                  clickScript(28, undo) + tapKey(34, "F1") + tapKey(40, "Escape") +
                  clickScript(46, cellOf(rows, Action::Simulate)) +
                  clickScript(52, cellOf(rows, Action::GiveMouseBack));
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 60)
    {
        return expect(false, "la saisie : code 0 et une image par image");
    }
    const std::string defaults = run.seen.front().table;
    const bool anySave =
        std::ranges::any_of(run.seen, [](const Seen& seen)
                            { return std::ranges::contains(seen.reached, Action::Save); });
    const bool anyPlay = std::ranges::any_of(run.seen, [](const Seen& seen)
                                             { return seen.mode != levain::editor::Mode::Edit; });
    const bool panelsAlwaysOpen =
        std::ranges::all_of(run.seen, [](const Seen& seen) { return seen.panelsOpen; });
    // Le nom d'une ligne est à la hauteur de ses boutons (AlignTextToFramePadding), pas au-dessus.
    const bool aligned =
        std::ranges::all_of(rows.rows, [](const ShortcutRowSeen& row)
                            { return std::abs(row.nameAt.y - row.chordAt.at(0).y) < 1.0f; });
    return expect(aligned, "le nom de chaque ligne est à la hauteur de ses boutons") +
           expect(run.seen[8].capturing && run.seen[8].messageLine.contains("Échap"),
                  "pendant la saisie la ligne de message dit ce que fait Échap") +
           expect(!run.seen[44].capturing && run.seen[44].messageLine.empty(),
                  "la saisie finie, la ligne de message est vide") +
           expect(run.seen[14].pending && run.seen[14].conflict == "Ctrl+S : Enregistrer",
                  "Ctrl+S pendant la saisie est la candidate, en conflit avec Enregistrer") +
           expect(!anySave, "Ctrl+S n'a pas enregistré pendant la saisie") +
           expect(run.seen[26].pending && !anyPlay,
                  "Alt+P pendant la saisie est la candidate, et ne joue pas") +
           expect(run.seen[38].capturing && std::string{run.seen[38].message}.contains("F1"),
                  "F1 est refusée (un message le dit) et la saisie continue") +
           expect(panelsAlwaysOpen, "F1 pendant la saisie ne ferme pas les panneaux") +
           expect(!run.seen[44].capturing && run.seen[44].table == defaults,
                  "Échap annule la saisie sans rien changer") +
           expect(std::ranges::none_of(std::span{run.seen}.subspan(46),
                                       [](const Seen& seen) { return seen.capturing; }),
                  "« Simuler » et « Rendre la souris » ne démarrent aucune saisie (grisées)") +
           expect(run.seen.back().table == defaults, "aucun accord n'a changé");
}

/// **Réinitialiser** : un accord changé revient par « Réinitialiser » ; deux autres changés par
/// « Tout réinitialiser », qu'une confirmation précède ; le fichier ne tient plus aucune action.
int playReset(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"reset"};
    const Seen rows = probeRows(backend);
    const ImVec2 save = cellOf(rows, Action::Save);
    const ImVec2 undo = cellOf(rows, Action::Undo);
    const TempDirectory scratch{"probe-all"};
    Session probeAll;
    probeAll.directory = scratch.path;
    probeAll.frames = 12;
    probeAll.openAt = 2;
    probeAll.script = clickScript(5, rows.resetAllAt);
    const Run probed = play(backend, probeAll);
    if (probed.seen.size() != 12 || !probed.seen.back().confirmShown)
    {
        return expect(false,
                      "le sondage : la confirmation de « Tout réinitialiser » n'apparaît pas");
    }
    const ImVec2 confirm = probed.seen.back().confirmAllAt;
    const ImVec2 resetSave = rowOf(rows, Action::Save)->resetAt;

    Session real;
    real.directory = directory.path;
    real.frames = 80;
    real.openAt = 2;
    // Une saisie est en cours (Enregistrer) quand « Tout réinitialiser » est cliqué : Ctrl+L tapé
    // sous la confirmation ne doit pas devenir l'accord d'Enregistrer.
    real.script = clickScript(4, save) + ctrlKey(10, "K") + clickScript(16, resetSave) +
                  clickScript(22, save) + ctrlKey(28, "K") + clickScript(34, undo) +
                  ctrlKey(40, "J") + clickScript(44, save) + clickScript(50, rows.resetAllAt) +
                  ctrlKey(56, "L") + clickScript(62, confirm);
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 80)
    {
        return expect(false, "réinitialiser : code 0 et une image par image");
    }
    const std::string defaults = run.seen.front().table;
    const auto file = savedIn(directory.path);
    return expect(tableLine(run.seen[14], Action::Save) == "Enregistrer = Ctrl+K, ",
                  "Enregistrer a pris Ctrl+K") +
           expect(run.seen[21].table == defaults, "« Réinitialiser » rend ses défauts à l'action") +
           expect(tableLine(run.seen[44], Action::Save) == "Enregistrer = Ctrl+K, " &&
                      tableLine(run.seen[44], Action::Undo) == "Annuler = Ctrl+J, ",
                  "deux actions sont changées") +
           expect(run.seen[48].capturing, "une saisie est en cours avant « Tout réinitialiser »") +
           expect(run.seen[60].confirmShown && run.seen[60].table != defaults,
                  "« Tout réinitialiser » demande confirmation et ne change rien avant") +
           expect(
               !run.seen[60].capturing && !run.seen[60].pending &&
                   tableLine(run.seen[60], Action::Save) == "Enregistrer = Ctrl+K, ",
               "la saisie est abandonnée sous la confirmation : Ctrl+L n'est pas devenu l'accord") +
           expect(run.seen.back().table == defaults,
                  "confirmé, toutes les actions rendent leurs défauts") +
           expect(file && file->shortcuts.empty(), "le fichier ne tient plus aucune action");
}

/// **Par le menu** : Édition > Préférences > Raccourcis… ouvre la fenêtre, qui liste chaque
/// action ; le filtre répond à l'accord puis au nom.
int playMenu(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"menu"};
    Session discover;
    discover.directory = directory.path;
    discover.frames = 6;
    const Run first = play(backend, discover);
    if (first.seen.empty())
    {
        return expect(false, "le sondage n'a rien relevé");
    }
    const ImVec2 edit = first.seen.back().bar.edit;
    discover.frames = 10;
    discover.script = clickScript(1, edit);
    const Run second = play(backend, discover);
    if (second.seen.empty() || !second.seen.back().bar.editOpen)
    {
        return expect(false, "le sondage : le menu Édition ne s'ouvre pas");
    }
    const ImVec2 preferences = second.seen.back().bar.preferences;
    discover.frames = 16;
    discover.script = clickScript(1, edit) + clickScript(6, preferences);
    const Run third = play(backend, discover);
    if (third.seen.empty() || !third.seen.back().bar.preferencesOpen)
    {
        return expect(false, "le sondage : le sous-menu Préférences ne s'ouvre pas");
    }
    const ImVec2 entry = third.seen.back().bar.shortcutsEntry;

    Session real;
    real.directory = directory.path;
    real.frames = 40;
    real.script = clickScript(1, edit) + clickScript(6, preferences) + clickScript(11, entry);
    real.poke = [](Run& run, std::size_t recorded)
    {
        auto& filter = run.editor->shortcutWindow.filter;
        const std::string_view text = recorded == 24 ? "ctrl+maj+z" : recorded == 32 ? "enreg" : "";
        if (!text.empty())
        {
            std::ranges::fill(filter, '\0');
            std::ranges::copy(text, filter.begin());
        }
    };
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 40)
    {
        return expect(false, "le menu : code 0 et une image par image");
    }
    return expect(!run.seen[8].windowOpen, "la fenêtre est fermée avant le clic") +
           expect(run.seen[22].windowOpen &&
                      run.seen[22].rows.size() == static_cast<std::size_t>(Action::Count),
                  "le menu ouvre la fenêtre, qui liste chaque action de la table") +
           expect(run.seen[30].rows.size() == 1 && run.seen[30].rows.front().action == Action::Redo,
                  "le filtre répond à un accord (Ctrl+Maj+Z : Rétablir)") +
           expect(run.seen.back().rows.size() == 1 &&
                      run.seen.back().rows.front().action == Action::Save,
                  "le filtre répond à un nom d'action (« enreg » : Enregistrer)");
}

/// **Une exécution scriptée** n'a pas de préférences : les défauts, et le fichier d'à côté n'est
/// ni lu ni écrit.
int playScripted(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"scripted"};
    Preferences preferences;
    preferences.shortcuts = {{.action = "Enregistrer", .chords = {"Ctrl+K"}}};
    if (!levain::editor::savePreferences(fileIn(directory.path), preferences))
    {
        return expect(false, "le fichier d'essai s'écrit");
    }
    const std::string before = contentOf(fileIn(directory.path));
    Session session;
    session.directory = directory.path;
    session.interactive = false;
    session.frames = 10;
    session.openAt = 2;
    const Run run = play(backend, session);
    if (run.exitCode != 0 || run.seen.size() != 10)
    {
        return expect(false, "scriptée : code 0 et une image par image");
    }
    return expect(tableLine(run.seen.back(), Action::Save) == "Enregistrer = Ctrl+S, ",
                  "une exécution scriptée garde les défauts") +
           expect(contentOf(fileIn(directory.path)) == before, "le fichier n'est pas réécrit");
}

/// **Un changement de disposition qui fait naître un conflit** : « Annuler = Z » (une lettre) ne
/// touche rien sur un QWERTY ; sur un AZERTY la lettre Z est la position du W (Avancer). La console
/// le dit, aucune action n'est rebranchée ; une ligne d'une autre branche survit à
/// l'enregistrement.
int playLayout(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"layout"};
    Preferences preferences;
    preferences.shortcuts = {{.action = "Annuler", .chords = {"Z"}},
                             {.action = "Action d'une autre branche", .chords = {"Ctrl+Q"}}};
    if (!levain::editor::savePreferences(fileIn(directory.path), preferences))
    {
        return expect(false, "le fichier d'essai s'écrit");
    }
    Session session;
    session.directory = directory.path;
    session.frames = 14;
    session.script = "8 keymap changed\n";
    session.poke = [](Run& run, std::size_t recorded)
    {
        if (recorded == 6)
        {
            run.app->keyAtPosition = azertyAt;
        }
    };
    const Run run = play(backend, session);
    if (run.exitCode != 0 || run.seen.size() != 14)
    {
        return expect(false, "la disposition : code 0 et une image par image");
    }
    const auto file = savedIn(directory.path);
    return expect(!loggedSays(run, "raccourcis : conflit (préférences)"),
                  "sur un QWERTY, « Annuler = Z » ne fait aucun conflit") +
           expect(loggedSays(run, "raccourcis : conflit (disposition du clavier)"),
                  "le changement de disposition fait naître un conflit, dit à la console") +
           expect(loggedSays(run, "action inconnue « Action d'une autre branche »"),
                  "la ligne d'une action inconnue est dite") +
           expect(tableLine(run.seen.back(), Action::Undo) == "Annuler = Z, " &&
                      tableLine(run.seen.back(), Action::MoveForward) == "Avancer = pos:26, ",
                  "aucun accord n'est rebranché en silence") +
           expect(file && file->shortcuts.size() == 2 &&
                      file->shortcuts.at(0).action == "Annuler" &&
                      file->shortcuts.at(1).action == "Action d'une autre branche",
                  "l'enregistrement garde l'action inconnue");
}

/// Où cliquer ou rester sur la Vue sans toucher la fenêtre « Raccourcis », qui couvre son centre :
/// le bord droit de l'image. Une faute bruyante si ce bord est encore sous la fenêtre (règle n°7).
ImVec2 viewSpotOf(const Seen& probe)
{
    const ImVec2 spot{probe.viewImage.x + probe.viewImage.width - 40.0f,
                      probe.viewImage.y + probe.viewImage.height * 0.5f};
    // La fenêtre se cale sur la police et l'espace libre (`shortcutWindowFit`) : son bord est
    // relevé.
    if (probe.viewImage.width <= 0.0f || spot.x <= probe.windowMax.x)
    {
        throw std::runtime_error{
            "le sondage : le bord de la Vue est sous la fenêtre « Raccourcis »"};
    }
    return spot;
}

float distanceBetween(const levain::editor::EditorCamera& a, const levain::editor::EditorCamera& b)
{
    const float dx = a.position.x - b.position.x, dy = a.position.y - b.position.y,
                dz = a.position.z - b.position.z;
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

/// **Une saisie ne survit pas à « Jouer »** : hors de l'Édition les touches de l'éditeur n'arrivent
/// plus à la saisie, qui ne pourrait plus finir, et F1 est refusée pendant elle. Le bouton de la
/// barre d'outils joue, la saisie s'arrête, et Échap (que la saisie gardait) arrête le jeu.
int playPlayMode(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"play"};
    constexpr ImVec2 Below{40.0f, 120.0f}; // sous la barre d'outils, qu'il faut cliquer
    const Seen rows = probeRows(backend, Below);
    Session real;
    real.directory = directory.path;
    real.frames = 50;
    real.openAt = 2;
    real.windowAt = Below;
    real.script = clickScript(4, cellOf(rows, Action::Undo)) + clickScript(12, rows.toolbarMode) +
                  tapKey(30, "Escape");
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 50)
    {
        return expect(false, "jouer : code 0 et une image par image");
    }
    const std::string defaults = run.seen.front().table;
    const bool playedAtAll = std::ranges::any_of(
        run.seen, [](const Seen& seen) { return seen.mode != levain::editor::Mode::Edit; });
    return expect(run.seen[9].capturing && run.seen[9].mode == levain::editor::Mode::Edit,
                  "le clic démarre la saisie, en Édition") +
           expect(playedAtAll, "le bouton de la barre d'outils joue") +
           expect(run.seen[26].mode == levain::editor::Mode::PlayWithoutReturn &&
                      !run.seen[26].capturing && !run.seen[26].pending,
                  "en jeu la saisie est abandonnée, rien n'attend") +
           expect(run.seen[26].panelsOpen && run.seen.back().panelsOpen,
                  "les panneaux restent ouverts") +
           expect(run.seen.back().mode == levain::editor::Mode::Edit,
                  "Échap arrête le jeu, que la saisie ne retient plus") +
           expect(run.seen.back().table == defaults && !run.seen.back().capturing,
                  "aucun accord n'a changé, aucune saisie ne reste");
}

/// **La saisie s'arrête quand le clavier est pris ailleurs** : le filtre de la fenêtre devient le
/// champ actif, « s » y est tapé et n'est pas l'accord ; une fenêtre fermée en pleine saisie ne
/// coupe plus les raccourcis (Ctrl+S enregistre).
int playFocus(const levain::tests::TestBackend& backend)
{
    const Seen rows = probeRows(backend);
    const TempDirectory typing{"focus-text"};
    Session text;
    text.directory = typing.path;
    text.frames = 32;
    text.openAt = 2;
    text.script = clickScript(4, cellOf(rows, Action::Undo)) + clickScript(12, rows.filterAt) +
                  tapKey(20, "S");
    const Run typed = play(backend, text);
    if (typed.exitCode != 0 || typed.seen.size() != 32)
    {
        return expect(false, "le champ : code 0 et une image par image");
    }
    const std::string defaults = typed.seen.front().table;
    int failures = expect(typed.seen[9].capturing, "le clic démarre la saisie") +
                   expect(!typed.seen[18].capturing && !typed.seen[18].pending,
                          "le champ de texte actif arrête la saisie") +
                   expect(!typed.seen.back().pending && typed.seen.back().table == defaults,
                          "« s » tapé dans le champ n'est pas devenu l'accord");

    const TempDirectory closing{"focus-close"};
    Session close;
    close.directory = closing.path;
    close.frames = 40;
    close.openAt = 2;
    close.script = clickScript(4, cellOf(rows, Action::Undo)) + ctrlKey(24, "S");
    close.poke = [](Run& run, std::size_t recorded)
    {
        if (recorded == 14)
        {
            run.editor->shortcutWindow.open = false; // la croix de la fenêtre, en pleine saisie
        }
    };
    const Run closed = play(backend, close);
    if (closed.exitCode != 0 || closed.seen.size() != 40)
    {
        return failures + expect(false, "la fermeture : code 0 et une image par image");
    }
    const bool saved =
        std::ranges::any_of(std::span{closed.seen}.subspan(22), [](const Seen& seen)
                            { return std::ranges::contains(seen.reached, Action::Save); });
    return failures + expect(closed.seen[9].capturing, "le clic démarre la saisie") +
           expect(!closed.seen[20].capturing && !closed.seen[20].windowOpen,
                  "fermer la fenêtre abandonne la saisie") +
           expect(saved,
                  "après la fermeture, Ctrl+S enregistre : les raccourcis ne sont plus coupés");
}

/// **La touche qui finit la saisie ne part pas comme raccourci**, et la saisie coupe les gestes :
/// la molette sur la Vue ne déplace pas la caméra pendant la saisie (elle le fait après), et Espace
/// capturée sur la Vue (où elle est « outil suivant ») est l'accord sans changer d'outil.
int playLeak(const levain::tests::TestBackend& backend)
{
    const Seen rows = probeRows(backend);
    const ImVec2 spot = viewSpotOf(rows);
    const TempDirectory directory{"leak"};
    Session real;
    real.directory = directory.path;
    real.frames = 40;
    real.openAt = 2;
    real.script = clickScript(4, cellOf(rows, Action::MoveUp, 1)) +
                  std::format("10 mouse move {} {}\n12 wheel 3\n", spot.x, spot.y) +
                  tapKey(18, "Space") + "26 wheel 3\n";
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 40)
    {
        return expect(false, "la fuite : code 0 et une image par image");
    }
    const bool anyToolKey =
        std::ranges::any_of(run.seen,
                            [](const Seen& seen)
                            {
                                return std::ranges::contains(seen.reached, Action::NextTool) ||
                                       std::ranges::contains(seen.reached, Action::ToolMove);
                            });
    return expect(run.seen[9].capturing, "le clic démarre la saisie") +
           expect(distanceBetween(run.seen[16].camera, run.seen[9].camera) < 1e-4f,
                  "pendant la saisie la molette sur la Vue ne déplace pas la caméra") +
           expect(!run.seen[24].capturing && !run.seen[24].pending &&
                      tableLine(run.seen[24], Action::MoveUp) == "Monter = pos:8, pos:44",
                  "Espace, par sa position, est l'accord de « Monter »") +
           expect(!anyToolKey, "la touche de l'accord n'a pas pris l'outil suivant sur la Vue") +
           expect(distanceBetween(run.seen.back().camera, run.seen[24].camera) > 1e-3f,
                  "la molette sur la Vue déplace la caméra une fois la saisie finie");
}

/// **Sur un AZERTY** : une action de position garde le scancode de la touche (la touche à la place
/// du Z, qui tape « w »), une action de lettre le caractère ; le fichier les écrit ainsi ; après le
/// redémarrage le clic droit tenu et cette touche physique déplacent la caméra, et sans le
/// fichier non.
int playAzerty(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"azerty"};
    const Seen rows = probeRows(backend);
    Session first;
    first.directory = directory.path;
    first.azerty = true;
    first.frames = 40;
    first.openAt = 2;
    first.script = clickScript(4, cellOf(rows, Action::MoveForward, 1)) + tapKey(10, "Z as w") +
                   clickScript(16, cellOf(rows, Action::Undo, 1)) + ctrlKey(22, "Z as w");
    const Run one = play(backend, first);
    if (one.exitCode != 0 || one.seen.size() != 40)
    {
        return expect(false, "la première session : code 0 et une image par image");
    }
    const auto saved = savedIn(directory.path);
    const ShortcutRowSeen* forward = rowOf(one.seen.back(), Action::MoveForward);
    int failures =
        expect(tableLine(one.seen.back(), Action::MoveForward) == "Avancer = pos:26, pos:29",
               "la touche à la place du Z est liée par son scancode (position 29)") +
        expect(forward != nullptr && forward->chords == "Z / W",
               "elle s'affiche « W », la lettre qu'elle tape sur un AZERTY") +
        expect(tableLine(one.seen.back(), Action::Undo) == "Annuler = Ctrl+Z, Ctrl+W",
               "une action de lettre garde le caractère tapé : Ctrl+W") +
        expect(saved && saved->shortcuts.size() == 2 &&
                   saved->shortcuts.at(0).chords == std::vector<std::string>{"pos:26", "pos:29"},
               "le fichier écrit la position par son numéro");

    const ImVec2 center{rows.viewImage.x + (rows.viewImage.width * 0.5f),
                        rows.viewImage.y + (rows.viewImage.height * 0.5f)};
    const std::string fly =
        std::format("4 mouse move {} {}\n8 button down right\n9 key down Z as w\n15 key up Z as w\n"
                    "16 button up right\n",
                    center.x, center.y);
    Session second;
    second.directory = directory.path;
    second.azerty = true;
    second.frames = 22;
    second.script = fly;
    const Run two = play(backend, second);
    const TempDirectory fresh{"azerty-fresh"};
    Session control;
    control.directory = fresh.path;
    control.azerty = true;
    control.frames = 22;
    control.script = fly;
    const Run none = play(backend, control);
    if (two.seen.size() != 22 || none.seen.size() != 22)
    {
        return failures + expect(false, "le vol : une image par image");
    }
    return failures +
           expect(distanceBetween(two.seen.back().camera, two.seen.front().camera) > 0.5f,
                  "après le redémarrage, clic droit et la touche liée déplacent la caméra") +
           expect(distanceBetween(none.seen.back().camera, none.seen.front().camera) < 1e-4f,
                  "sans le fichier, cette touche ne déplace rien");
}

/// **« Réinitialiser » d'un défaut pris** : Annuler a pris Ctrl+S à Enregistrer ; réinitialiser
/// Enregistrer nomme le conflit et ne change rien ; « Annuler » l'abandonne, puis « Remplacer »
/// rend Ctrl+S à Enregistrer et l'ôte à Annuler, fichier compris.
int playResetConflict(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"reset-conflict"};
    const Seen rows = probeRows(backend);
    const ImVec2 undo = cellOf(rows, Action::Undo);
    const ImVec2 resetSave = rowOf(rows, Action::Save)->resetAt;
    const TempDirectory scratch{"probe-reset-conflict"};
    Session probe;
    probe.directory = scratch.path;
    probe.frames = 22;
    probe.openAt = 2;
    probe.script = clickScript(4, undo) + ctrlKey(10, "S");
    const Run probed = play(backend, probe);
    if (probed.seen.size() != 22 || !probed.seen.back().pending)
    {
        return expect(false, "le sondage : le conflit n'est pas apparu");
    }
    const ImVec2 replace = probed.seen.back().replaceAt;
    const ImVec2 cancel = probed.seen.back().cancelAt;

    Session real;
    real.directory = directory.path;
    real.frames = 90;
    real.openAt = 2;
    // Annuler prend Ctrl+S (Remplacer), puis Enregistrer demande ses défauts : annulé, puis
    // remplacé.
    real.script = clickScript(4, undo) + ctrlKey(10, "S") + clickScript(20, replace) +
                  clickScript(28, resetSave) + clickScript(38, cancel) +
                  clickScript(46, resetSave) + clickScript(56, replace);
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 90)
    {
        return expect(false, "réinitialiser un défaut pris : code 0 et une image par image");
    }
    const Seen& taken = run.seen[26];
    const Seen& waiting = run.seen[36];
    const Seen& cancelled = run.seen[44];
    const Seen& done = run.seen.back();
    const auto file = savedIn(directory.path);
    return expect(tableLine(taken, Action::Undo) == "Annuler = Ctrl+S, " &&
                      tableLine(taken, Action::Save) == "Enregistrer = , ",
                  "Annuler a pris Ctrl+S à Enregistrer") +
           expect(waiting.pending && waiting.conflict == "Ctrl+S : Annuler",
                  "« Réinitialiser » nomme le conflit « Ctrl+S : Annuler »") +
           expect(waiting.sentence ==
                      "Enregistrer voudrait ses accords par défaut, déjà pris, rien "
                      "ne change : Ctrl+S : Annuler",
                  "la phrase nomme l'action qui reprend ses défauts") +
           expect(waiting.table == taken.table, "rien ne change tant qu'on n'a pas remplacé") +
           expect(!cancelled.pending && cancelled.table == taken.table,
                  "« Annuler » abandonne : la table est inchangée") +
           expect(!done.pending && tableLine(done, Action::Save) == "Enregistrer = Ctrl+S, " &&
                      tableLine(done, Action::Undo) == "Annuler = , ",
                  "« Remplacer » rend Ctrl+S à Enregistrer et l'ôte à Annuler") +
           expect(loggedSays(run, "Ctrl+S : Annuler retiré"),
                  "la console dit ce qui a été retiré") +
           expect(file && file->shortcuts.size() == 1 &&
                      file->shortcuts.front().action == "Annuler" &&
                      file->shortcuts.front().chords.empty(),
                  "le fichier ne tient plus que Annuler, sans accord");
}

/// **Une action tenue ne lit aucun modificateur** (le vol, Arrêter) : Maj+E saisi pour « Avancer »
/// est la touche E, que « Monter » tient : le conflit est nommé « E : Monter », la fenêtre dit que
/// les modificateurs sont ignorés, et rien ne change avant « Remplacer ». Un fichier écrit à la
/// main avec « Avancer = Shift+pos:8 » est refusé, dit à la console, et l'enregistrement qu'un
/// autre changement fait garde la ligne.
int playHeld(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"held"};
    const Seen rows = probeRows(backend);
    Session real;
    real.directory = directory.path;
    real.frames = 30;
    real.openAt = 2;
    real.script =
        clickScript(4, cellOf(rows, Action::MoveForward, 1)) + modifiedKey(10, "Left Shift", "E");
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 30)
    {
        return expect(false, "l'action tenue : code 0 et une image par image");
    }
    const Seen& waiting = run.seen.back();
    int failures =
        expect(run.seen[9].capturing, "le clic démarre la saisie") +
        expect(
            waiting.pending && waiting.conflict == "E : Monter",
            "Maj+E pour « Avancer » nomme le conflit « E : Monter » (Maj ne se lit pas en vol)") +
        expect(waiting.message.contains("modificateur"),
               "la fenêtre dit que cette action ignore les modificateurs") +
        expect(waiting.table == run.seen.front().table, "rien ne change avant « Remplacer »") +
        expect(!shortcutsSavedIn(directory.path), "aucune action n'est écrite");

    // Le fichier écrit à la main : refusé, dit, gardé. Annuler change (Ctrl+K), ce qui enregistre.
    const TempDirectory written{"held-file"};
    Preferences preferences;
    preferences.shortcuts = {{.action = "Avancer", .chords = {"Shift+pos:8"}}};
    if (!levain::editor::savePreferences(fileIn(written.path), preferences))
    {
        return failures + expect(false, "le fichier d'essai s'écrit");
    }
    Session second;
    second.directory = written.path;
    second.frames = 30;
    second.openAt = 2;
    second.script = clickScript(4, cellOf(rows, Action::Undo)) + ctrlKey(10, "K");
    const Run reread = play(backend, second);
    if (reread.exitCode != 0 || reread.seen.size() != 30)
    {
        return failures + expect(false, "le fichier à la main : code 0 et une image par image");
    }
    const auto file = savedIn(written.path);
    const levain::editor::ShortcutLine* kept = nullptr;
    if (file)
    {
        const auto found =
            std::ranges::find(file->shortcuts, "Avancer", &levain::editor::ShortcutLine::action);
        kept = found == file->shortcuts.end() ? nullptr : &*found;
    }
    return failures +
           expect(loggedSays(reread, "Avancer : cette action se tient comme une touche"),
                  "la ligne à modificateur est dite à la console") +
           expect(tableLine(reread.seen.front(), Action::MoveForward) == "Avancer = pos:26, ",
                  "les défauts d'« Avancer » restent") +
           expect(tableLine(reread.seen.back(), Action::Undo) == "Annuler = Ctrl+K, ",
                  "l'autre changement est fait") +
           expect(kept != nullptr && kept->chords == std::vector<std::string>{"Shift+pos:8"},
                  "l'enregistrement garde la ligne refusée telle qu'écrite");
}

/// **Une ligne refusée ne revient pas** : « Avancer = Z » (une lettre pour le vol) est refusée au
/// chargement et gardée ; dès que la fenêtre écrit « Avancer » (une saisie), la ligne est oubliée,
/// et « Réinitialiser » ne la rend pas : le fichier n'a plus d'« Avancer », le redémarrage ne
/// journalise plus l'erreur. La ligne d'une action inconnue, elle, reste.
int playRefusedReset(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"refused-reset"};
    const Seen rows = probeRows(backend);
    Preferences preferences;
    preferences.shortcuts = {{.action = "Avancer", .chords = {"Z"}},
                             {.action = "Action d'une autre branche", .chords = {"Ctrl+Q"}}};
    if (!levain::editor::savePreferences(fileIn(directory.path), preferences))
    {
        return expect(false, "le fichier d'essai s'écrit");
    }
    Session first;
    first.directory = directory.path;
    first.frames = 40;
    first.openAt = 2;
    first.peekFileAt = 20;
    first.script = clickScript(4, cellOf(rows, Action::MoveForward)) + tapKey(10, "Z") +
                   clickScript(24, rowOf(rows, Action::MoveForward)->resetAt);
    const Run one = play(backend, first);
    if (one.exitCode != 0 || one.seen.size() != 40)
    {
        return expect(false, "la première session : code 0 et une image par image");
    }
    const auto after = savedIn(directory.path);
    const auto hasLine = [](const std::optional<Preferences>& file, std::string_view action)
    {
        return file && std::ranges::any_of(file->shortcuts, [action](const auto& line)
                                           { return line.action == action; });
    };
    int failures = expect(loggedSays(one, "Avancer : un accord n'est pas du genre de l'action"),
                          "« Avancer = Z » est refusée au chargement") +
                   expect(tableLine(one.seen.front(), Action::MoveForward) == "Avancer = pos:26, ",
                          "les défauts d'« Avancer » restent") +
                   expect(tableLine(one.seen[16], Action::MoveForward) == "Avancer = pos:29, ",
                          "la saisie change « Avancer » (la touche à la place du Z)") +
                   expect(one.fileAtPeek.contains("Avancer = pos:29") &&
                              !one.fileAtPeek.contains("Avancer = Z"),
                          "le fichier écrit la saisie, plus la ligne refusée") +
                   expect(tableLine(one.seen.back(), Action::MoveForward) == "Avancer = pos:26, ",
                          "« Réinitialiser » rend les défauts") +
                   expect(!hasLine(after, "Avancer"),
                          "« Avancer = Z » ne revient pas à l'écriture après « Réinitialiser »") +
                   expect(hasLine(after, "Action d'une autre branche"),
                          "la ligne d'une action inconnue est gardée");
    // Le redémarrage : plus d'erreur à journaliser pour « Avancer ».
    Session second;
    second.directory = directory.path;
    second.frames = 10;
    const Run two = play(backend, second);
    return failures + expect(two.exitCode == 0 && two.seen.size() == 10, "la seconde session") +
           expect(!loggedSays(two, "Avancer : un accord n'est pas du genre de l'action"),
                  "le redémarrage ne journalise plus le refus");
}

/// Un scénario par nom : `levain_editor_shortcut_settings <backend> <scénario>`.
struct Scenario
{
    std::string_view name;
    int (*run)(const levain::tests::TestBackend&);
};

constexpr std::array<Scenario, 14> Scenarios{{{"change", playChange},
                                              {"conflict", playConflict},
                                              {"capture", playCapture},
                                              {"reset", playReset},
                                              {"menu", playMenu},
                                              {"scripted", playScripted},
                                              {"layout", playLayout},
                                              {"play", playPlayMode},
                                              {"focus", playFocus},
                                              {"leak", playLeak},
                                              {"azerty", playAzerty},
                                              {"resetconflict", playResetConflict},
                                              {"held", playHeld},
                                              {"refusedreset", playRefusedReset}}};

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const auto backend =
            arguments.size() == 3 ? levain::tests::testBackendNamed(arguments[1]) : std::nullopt;
        const auto scenario =
            arguments.size() == 3
                ? std::ranges::find(Scenarios, std::string_view{arguments[2]}, &Scenario::name)
                : Scenarios.end();
        if (!backend || scenario == Scenarios.end())
        {
            std::println(stderr,
                         "usage : levain_editor_shortcut_settings <vulkan|d3d12|d3d12-warp> "
                         "<change|conflict|capture|reset|menu|scripted|layout|play|focus|leak|"
                         "azerty|resetconflict|held|refusedreset>");
            return 2;
        }
        return scenario->run(*backend) == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
