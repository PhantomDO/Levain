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
//   levain_editor_shortcut_settings <vulkan|d3d12|d3d12-warp> <scénario>
//
#include <algorithm>
#include <array>
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
#include "levain/editor/editor.hpp"
#include "levain/editor/preferences.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"

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
            .message = window.message != nullptr ? window.message : "",
            .mode = editor.mode.current,
            .rows = window.rows,
            .bar = editor.bar,
            .replaceAt = window.replaceAt,
            .cancelAt = window.cancelAt,
            .resetAllAt = window.resetAllAt,
            .confirmAllAt = window.confirmAllAt,
            .confirmShown = window.confirmShown,
            .reached = editor.lastShortcuts};
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
    /// Ouvre la fenêtre « Raccourcis » à cette image, comme le ferait le menu (testé à part).
    std::optional<std::size_t> openAt;
    /// Lit le fichier des préférences à cette image : il est écrit tout de suite, pas à la fin.
    std::optional<std::size_t> peekFileAt;
    /// Tourne à la fin de `ui` de chaque image, avec le nombre d'images relevées.
    std::function<void(Run&, std::size_t)> poke;
};

levain::app::StartFunction programOf(Run& run)
{
    return [&run](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition; // épinglé
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
        if (session.peekFileAt && recorded == *session.peekFileAt && session.directory)
        {
            run.fileAtPeek = contentOf(fileIn(*session.directory));
        }
        if (session.poke)
        {
            session.poke(run, recorded);
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

/// Les touches d'un accord à Ctrl : Ctrl tenu une image avant la lettre, relâché une image après
/// (SDL pose les modificateurs tenus sur la touche qui suit).
std::string ctrlKey(int frame, std::string_view key)
{
    return std::format("{} key down Left Ctrl\n{} key down {}\n{} key up {}\n{} key up Left Ctrl\n",
                       frame, frame + 1, key, frame + 2, key, frame + 3);
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
Seen probeRows(const levain::tests::TestBackend& backend)
{
    const TempDirectory scratch{"probe"};
    Session session;
    session.directory = scratch.path;
    session.frames = 10;
    session.openAt = 2;
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
    real.script = clickScript(4, undo) + ctrlKey(10, "S") + clickScript(16, undo) +
                  "22 key down Left Alt\n23 key down P\n24 key up P\n25 key up Left Alt\n" +
                  clickScript(28, undo) + tapKey(34, "F1") + tapKey(40, "Escape");
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
    return expect(run.seen[8].capturing, "le clic démarre la saisie") +
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
    real.frames = 70;
    real.openAt = 2;
    real.script = clickScript(4, save) + ctrlKey(10, "K") + clickScript(16, resetSave) +
                  clickScript(22, save) + ctrlKey(28, "K") + clickScript(34, undo) +
                  ctrlKey(40, "J") + clickScript(46, rows.resetAllAt) + clickScript(52, confirm);
    const Run run = play(backend, real);
    if (run.exitCode != 0 || run.seen.size() != 70)
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
           expect(run.seen[50].confirmShown && run.seen[50].table != defaults,
                  "« Tout réinitialiser » demande confirmation et ne change rien avant") +
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

/// Un scénario par nom : `levain_editor_shortcut_settings <backend> <scénario>`.
struct Scenario
{
    std::string_view name;
    int (*run)(const levain::tests::TestBackend&);
};

constexpr std::array<Scenario, 7> Scenarios{{{"change", playChange},
                                             {"conflict", playConflict},
                                             {"capture", playCapture},
                                             {"reset", playReset},
                                             {"menu", playMenu},
                                             {"scripted", playScripted},
                                             {"layout", playLayout}}};

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
                         "<change|conflict|capture|reset|menu|scripted|layout>");
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
