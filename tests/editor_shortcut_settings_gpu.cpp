// La fenêtre « Raccourcis » dans la vraie boucle (ADR-0036, décision 13, morceau 16) : `withEditor`
// autour d'un programme minimal, un script d'input rejoué hors écran, **un dossier de préférences
// jetable** (`EditorOptions::preferencesDirectory`) et `AppSettings::testInteractive` (une
// exécution scriptée n'a pas de préférences). Le clavier est épinglé (QWERTY) : un écran réel
// lirait celui de la machine. Un scénario par exécution, cinq lancements de l'application au plus
// (la CI donne 120 s par test) :
//   change         un accord changé par clic et touche est enregistré tout de suite, relu au
//                  redémarrage : le nouveau part, l'ancien non
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
#include <iterator>
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

/// Ce que l'éditeur a fait à une image, relevé à la fin de `ui`.
struct Seen
{
    std::string table; ///< la table, une action par ligne (`tokenOf`)
    bool panelsOpen = false;
    bool capturing = false;
    bool pending = false;
    std::vector<ShortcutRowSeen> rows;
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
            .capturing = window.capturing.has_value(),
            .pending = window.pending.has_value(),
            .rows = window.rows,
            .reached = editor.lastShortcuts};
}

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
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

/// Un scénario par nom : `levain_editor_shortcut_settings <backend> <scénario>`.
struct Scenario
{
    std::string_view name;
    int (*run)(const levain::tests::TestBackend&);
};

constexpr std::array<Scenario, 1> Scenarios{{{"change", playChange}}};

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
                         "<change>");
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
