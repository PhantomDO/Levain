// La disposition et les préférences de l'éditeur dans la vraie boucle (ADR-0036, décision 10,
// morceau 15) : `withEditor` autour d'un programme minimal, un script d'input rejoué hors écran (le
// banc d'essai de l'App), **un dossier de préférences jetable**
// (`EditorOptions::preferencesDirectory`, jamais celui de qui lance le test) et
// `AppSettings::testInteractive` (une exécution scriptée n'a pas de préférences). **Un scénario par
// exécution** (chacun amorce son propre état, quatre ou cinq lancements de l'application au plus :
// la CI donne 120 s par test, et asan ou WARP vont deux à trois fois plus lentement) :
//   central     la Vue fermée à la session d'avant : son nœud (le central) est retrouvé quand même
// Il faut un device, comme `levain_app_script`.
//   levain_editor_preferences <vulkan|d3d12|d3d12-warp> <scénario>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
// Les fenêtres et leurs nœuds retrouvés par leur nom : l'API interne d'ImGui.
#include <imgui_internal.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/assets/asset_id.hpp"
#include "levain/core/log.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/console.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/editor/inspector.hpp"
#include "levain/editor/preferences.hpp"
#include "levain/editor/view.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"
#include "levain/ui/tr.hpp"

namespace
{

namespace fs = std::filesystem;
using levain::app::App;
using levain::editor::Preferences;

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 720;
/// Dans l'ordre du menu : Hiérarchie, Inspecteur, Console, Vue, Image, Passes, Scène.
constexpr std::size_t ViewEntry = 3;
constexpr std::array<const char*, 7> Titles = {levain::editor::HierarchyWindow,
                                               levain::editor::InspectorWindow,
                                               levain::editor::ConsoleWindow,
                                               levain::editor::ViewWindow,
                                               "Image",
                                               "Passes",
                                               "Scène"};
/// Un script que rien ne lit : un mouvement de souris loin de tout (un script vide est refusé).
constexpr std::string_view Idle = "0 mouse move 1 1\n";

/// Où est une fenêtre à une image : son nœud, et si son onglet se voit.
struct Placed
{
    bool exists = false;
    ImGuiID dock = 0;
    bool tabVisible = false;
    int windowsOnNode = 0;
};

Placed placedOf(const char* french)
{
    const ImGuiWindow* window = ImGui::FindWindowByName(levain::ui::labelOf(french).c_str());
    if (window == nullptr)
    {
        return {};
    }
    return {.exists = true,
            .dock = window->DockId,
            .tabVisible = window->DockTabIsVisible,
            .windowsOnNode = window->DockNode != nullptr ? window->DockNode->Windows.Size : 0};
}

/// Ce que l'éditeur a fait à une image, relevé à la fin de `ui`.
struct Seen
{
    std::array<Placed, 7> windows{};
    levain::app::DockNodes nodes{};
    bool viewOpen = true;
    /// Le nœud du centre est une feuille que la disposition a marquée « centrale ».
    bool centerIsCentral = false;
};

Seen seenOf(const App& app, const levain::editor::Editor& editor)
{
    Seen seen{.nodes = app.ui.dock, .viewOpen = editor.view.open, .centerIsCentral = false};
    for (std::size_t i = 0; i < Titles.size(); ++i)
    {
        seen.windows.at(i) = placedOf(Titles.at(i));
    }
    const ImGuiDockNode* center = ImGui::DockBuilderGetNode(app.ui.dock.center);
    seen.centerIsCentral = center != nullptr && center->IsLeafNode() && center->IsCentralNode();
    return seen;
}

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    /// Les messages de la console à la dernière image, « catégorie : texte ».
    std::vector<std::string> console;
    /// Tous les messages du processus, **fermeture comprise** : la console n'est relevée qu'à
    /// chaque image, et l'enregistrement de la fermeture vient après la dernière.
    std::vector<std::string> logged;
    App* app = nullptr;
};

/// Comment lancer une session de l'éditeur.
struct Session
{
    std::string_view script = Idle;
    int frames = 12;
    /// Le dossier de préférences injecté. Rien : l'éditeur lirait celui de l'utilisateur, si
    /// `interactive` le permettait ; aucun passage ne combine les deux.
    std::optional<fs::path> directory;
    /// `AppSettings::testInteractive` : un humain devant sa fenêtre (les préférences valent).
    bool interactive = false;
};

/// Une session scriptée (`--steps`, `--input-script`) : pas de préférences, dossier injecté ou non.
Session scripted(int frames, std::string_view script = Idle,
                 std::optional<fs::path> directory = std::nullopt)
{
    Session session;
    session.script = script;
    session.frames = frames;
    session.directory = std::move(directory);
    return session;
}

/// Un humain devant sa fenêtre (`testInteractive`), avec un dossier de préférences jetable.
Session interactive(const fs::path& directory, int frames, std::string_view script = Idle)
{
    Session session = scripted(frames, script, directory);
    session.interactive = true;
    return session;
}

levain::app::StartFunction programOf(Run& run)
{
    return [&run](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        // Épinglée : une vraie fenêtre lirait la disposition de la machine (AZERTY chez Donnovan).
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition;
        run.app = &app;
        return levain::app::FrameHooks{};
    };
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
    options.preferencesDirectory = session.directory;
    options.afterUi = [&run](const levain::editor::Editor& editor)
    {
        run.seen.push_back(seenOf(*run.app, editor));
        run.console.clear();
        for (const auto& entry : levain::core::snapshotOf(*editor.log->ring).entries)
        {
            run.console.push_back(entry.category + " : " + entry.text);
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

/// Un dossier jetable, vide, retiré à la fin. Un identifiant au hasard dans le nom : deux ctest en
/// parallèle (le même test sous Vulkan et sous WARP) ou deux worktrees ne se partagent pas
/// `/tmp` (même idée que `app_loop_gpu.cpp`).
struct TempDirectory
{
    fs::path path;

    explicit TempDirectory(std::string_view name)
        : path(fs::temp_directory_path() /
               ("levain-editor-prefs-" + std::string{name} + "-" +
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

fs::path fileIn(const fs::path& directory)
{
    return directory / levain::editor::PreferencesFileName;
}

/// Les préférences écrites dans ce dossier, ou rien (absentes ou refusées).
std::optional<Preferences> savedIn(const fs::path& directory)
{
    const auto loaded = levain::editor::loadPreferences(fileIn(directory));
    if (!loaded || !*loaded)
    {
        return std::nullopt;
    }
    return *loaded;
}

bool consoleSays(const Run& run, std::string_view needle)
{
    for (const std::string& line : run.console)
    {
        if (line.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/// Un enregistrement qui réussit ne passe pas par l'échec du renommage : sans ce contrôle, une
/// écriture qui échoue dès que le fichier existe laisserait les scénarios verts (la session
/// suivante relirait simplement le fichier de la précédente).
int expectSaveRenamed(const Run& run, std::string_view when)
{
    const bool failed = std::ranges::any_of(
        run.logged, [](const std::string& line)
        { return line.contains("renommage échoué") || line.contains("écriture échouée"); });
    return expect(!failed, std::format("{} : l'enregistrement n'a pas échoué", when));
}

/// **La Vue fermée à la session d'avant** (deux lancements) : aucun réglage de sa fenêtre dans la
/// disposition (elle n'a jamais été dessinée), son nœud est pourtant retrouvé : le central.
int playViewClosed(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"central"};
    Preferences seed;
    seed.windows.view = false;
    if (!levain::editor::savePreferences(fileIn(directory.path), seed))
    {
        return expect(false, "la Vue fermée : le fichier d'amorce s'écrit");
    }
    const Run first = play(backend, interactive(directory.path, 12));
    const auto saved = savedIn(directory.path);
    if (first.exitCode != 0 || first.seen.size() != 12 || !saved)
    {
        return expect(false, "la Vue fermée : la première session s'ouvre et écrit un fichier");
    }
    int failures =
        expect(!first.seen.back().viewOpen && !first.seen.back().windows.at(ViewEntry).exists,
               "la Vue part fermée") +
        // ImGui écrit « [Window][Vue] » (le nom sans le « ### » de l'identifiant) ; chercher
        // « ###Vue » ne trouvait jamais rien. Une disposition vide ne prouverait rien non plus.
        expect(!saved->layout.empty() && saved->layout.find("[Window][Vue]") == std::string::npos,
               "la disposition gardée n'a aucun réglage de la fenêtre Vue (contrôle : "
               "sans elle, le repli ne serait pas éprouvé)") +
        expectSaveRenamed(first, "la Vue fermée");
    const Run second = play(backend, interactive(directory.path, 12));
    if (second.exitCode != 0 || second.seen.size() != 12)
    {
        return failures + expect(false, "la Vue fermée : la seconde session s'ouvre");
    }
    return failures +
           expect(consoleSays(second, "préférences relues"), "la disposition est relue") +
           expect(
               second.seen.front().nodes.center != 0 && second.seen.front().centerIsCentral,
               "le nœud de la Vue, jamais dessinée, est le nœud central de la disposition relue");
}

/// Un scénario par nom : `levain_editor_preferences <backend> <scénario>`.
struct Scenario
{
    std::string_view name;
    int (*run)(const levain::tests::TestBackend&);
};

constexpr std::array<Scenario, 1> Scenarios{{{"central", playViewClosed}}};

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
            std::println(stderr, "usage : levain_editor_preferences <vulkan|d3d12|d3d12-warp> "
                                 "<central>");
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
