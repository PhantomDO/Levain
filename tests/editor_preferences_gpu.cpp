// La disposition et les préférences de l'éditeur dans la vraie boucle (ADR-0036, décision 10,
// morceau 15) : `withEditor` autour d'un programme minimal, un script d'input rejoué hors écran (le
// banc d'essai de l'App), **un dossier de préférences jetable**
// (`EditorOptions::preferencesDirectory`, jamais celui de qui lance le test) et
// `AppSettings::testInteractive` (une exécution scriptée n'a pas de préférences). **Un scénario par
// exécution** (chacun amorce son propre état, quatre ou cinq lancements de l'application au plus :
// la CI donne 120 s par test, et asan ou WARP vont deux à trois fois plus lentement) :
//   round-trip  la vitesse, « Temps réel », la console fermée par le menu et l'inspecteur rangé en
//               bas sont relus à la session suivante, sans `buildLayout` ; la disposition réécrite
//               à l'identique
//   reset       « Réinitialiser la disposition » rend la défaut, rouvre la console, garde la
//               vitesse et réécrit le fichier tout de suite
//   play        quitter en jeu garde le choix de « Temps réel » de l'Édition
//   central     la Vue fermée à la session d'avant : son nœud (le central) est retrouvé quand même
//   scripted    --steps, --seconds et --input-script ne lisent ni n'écrivent rien
//   refused     un fichier refusé est dit à la console et mis de côté (sauf un fichier d'une
//               version plus récente, laissé tel quel, sans enregistrement) ; une disposition
//               illisible est écartée, le reste gardé
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
#include "levain/editor/mode.hpp"
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
constexpr std::size_t HierarchyEntry = 0;
constexpr std::size_t InspectorEntry = 1;
constexpr std::size_t ConsoleEntry = 2;
constexpr std::size_t ViewEntry = 3;
constexpr std::size_t PassesEntry = 5;
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
    float flySpeed = 0.0f;
    bool realTime = true;
    bool consoleOpen = true;
    bool hierarchyOpen = true;
    bool viewOpen = true;
    levain::editor::Mode mode = levain::editor::Mode::Edit;
    /// Le nœud du centre est une feuille que la disposition a marquée « centrale ».
    bool centerIsCentral = false;
    levain::editor::WindowMenuLayout menu{};
    levain::editor::ToolbarLayout toolbar{};
    std::string shortcuts; ///< la table des raccourcis, un accord par ligne (`tokenOf`)
};

std::string shortcutsText(const levain::editor::ShortcutTable& table)
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

Seen seenOf(const App& app, const levain::editor::Editor& editor)
{
    Seen seen{.nodes = app.ui.dock,
              .flySpeed = editor.camera.flySpeed,
              .realTime = app.realTime,
              .consoleOpen = editor.windows.console,
              .hierarchyOpen = editor.windows.hierarchy,
              .viewOpen = editor.view.open,
              .mode = editor.mode.current,
              .centerIsCentral = false,
              .menu = editor.menu,
              .toolbar = editor.toolbar,
              .shortcuts = shortcutsText(editor.shortcuts)};
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
    /// `--seconds` plutôt que `--steps`.
    std::optional<double> seconds;
    /// Tourne à la fin de `ui` de chaque image, après le relevé, avec le nombre d'images relevées.
    std::function<void(std::size_t)> poke;
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

/// `--seconds` : pas de script, la boucle s'arrête d'elle-même.
Session secondsSession(const fs::path& directory, double seconds)
{
    Session session = scripted(0, Idle, directory);
    session.seconds = seconds;
    return session;
}

/// Un humain devant sa fenêtre (`testInteractive`), avec un dossier de préférences jetable.
Session interactive(const fs::path& directory, int frames, std::string_view script = Idle,
                    std::function<void(std::size_t)> poke = {})
{
    Session session = scripted(frames, script, directory);
    session.interactive = true;
    session.poke = std::move(poke);
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
    if (session.seconds)
    {
        // Sans script : un script que la boucle n'aurait pas fini de jouer ferait échouer le test.
        settings.loopSeconds = *session.seconds;
    }
    else
    {
        settings.inputScript = std::move(*parsed);
        settings.steps = session.frames;
    }
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
    options.afterUi = [&run, &session](const levain::editor::Editor& editor)
    {
        run.seen.push_back(seenOf(*run.app, editor));
        run.console.clear();
        for (const auto& entry : levain::core::snapshotOf(*editor.log->ring).entries)
        {
            run.console.push_back(entry.category + " : " + entry.text);
        }
        if (session.poke)
        {
            session.poke(run.seen.size());
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

std::size_t entriesIn(const fs::path& directory)
{
    return static_cast<std::size_t>(
        std::distance(fs::directory_iterator{directory}, fs::directory_iterator{}));
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

/// Un clic à `at` : la souris y va à l'image `frame`, le bouton s'enfonce deux images plus tard.
std::string clickScript(int frame, ImVec2 at)
{
    return std::format("{} mouse move {} {}\n{} button down left\n{} button up left\n", frame, at.x,
                       at.y, frame + 2, frame + 3);
}

/// La disposition par défaut : les quatre nœuds distincts, chaque fenêtre sur le sien, l'inspecteur
/// seul (la CI le compte), la console avec Image, Passes et Scène en onglets du bas.
int expectDefaultLayout(const Seen& seen, std::string_view when)
{
    const auto& w = seen.windows;
    const auto& n = seen.nodes;
    const auto say = [&when](std::string_view what) { return std::format("{} : {}", when, what); };
    return expect(w.at(HierarchyEntry).dock == n.left && w.at(HierarchyEntry).dock != 0,
                  say("la hiérarchie est sur le nœud de gauche")) +
           expect(w.at(InspectorEntry).dock == n.inspector && w.at(InspectorEntry).dock != 0,
                  say("l'inspecteur est sur le nœud de droite")) +
           expect(w.at(InspectorEntry).tabVisible && w.at(InspectorEntry).windowsOnNode == 1,
                  say("l'inspecteur est seul sur son nœud")) +
           expect(w.at(ViewEntry).dock == n.center && w.at(ViewEntry).dock != 0,
                  say("la Vue est sur le nœud du centre")) +
           expect(w.at(ConsoleEntry).dock == n.bottom && w.at(PassesEntry).dock == n.bottom &&
                      n.bottom != 0,
                  say("la console et Passes sont en onglets du nœud du bas")) +
           expect(n.left != n.center && n.left != n.inspector && n.left != n.bottom &&
                      n.center != n.inspector && n.center != n.bottom && n.inspector != n.bottom,
                  say("les quatre nœuds sont distincts"));
}

/// Le menu « Fenêtre » : le script qui le déroule puis clique l'entrée à `item` (Console,
/// Réinitialiser…).
std::string menuClick(int frame, ImVec2 title, ImVec2 item)
{
    return clickScript(frame, title) + clickScript(frame + 6, item);
}

/// Ce qu'une session sans préférences a montré : les positions que les scripts cliquent.
struct Probe
{
    Seen seen;
    ImVec2 menuTitle{};
    ImVec2 consoleItem{};
    ImVec2 resetItem{};
    ImVec2 speed{};
    ImVec2 realTime{};
};

/// Un passage scripté (donc sans préférences, aucun dossier) pour lire où cliquer dans la barre
/// d'outils ; avec `withMenu`, un second où le menu Fenêtre est ouvert, pour ses entrées.
Probe probeOf(const levain::tests::TestBackend& backend, bool withMenu)
{
    const Run first = play(backend, scripted(6));
    if (first.seen.empty())
    {
        throw std::runtime_error{"le sondage n'a rien relevé"};
    }
    Probe probe{.seen = first.seen.back()};
    probe.menuTitle = probe.seen.menu.title;
    probe.speed = {probe.seen.toolbar.speed.x + probe.seen.toolbar.speedWidth * 0.45f,
                   probe.seen.toolbar.speed.y};
    probe.realTime = probe.seen.toolbar.realTime;
    if (!withMenu)
    {
        return probe;
    }
    const Run opened = play(backend, scripted(9, clickScript(1, probe.menuTitle)));
    if (opened.seen.empty() || !opened.seen.back().menu.itemsShown)
    {
        throw std::runtime_error{"le sondage : le menu Fenêtre ne s'ouvre pas"};
    }
    probe.consoleItem = opened.seen.back().menu.items.at(ConsoleEntry);
    probe.resetItem = opened.seen.back().menu.reset;
    return probe;
}

constexpr int SessionFrames = 40;
constexpr std::size_t MoveInspectorAt = 30;

/// L'onglet de l'inspecteur glisse dans le nœud de Passes (le bas), comme un glisser-lâcher.
void moveInspectorToBottom()
{
    ImGuiWindow* inspector =
        ImGui::FindWindowByName(levain::ui::labelOf(levain::editor::InspectorWindow).c_str());
    const ImGuiWindow* passes = ImGui::FindWindowByName(levain::ui::labelOf("Passes").c_str());
    ImGui::SetWindowDock(inspector, passes->DockId, ImGuiCond_Always);
}

/// L'inspecteur est dans le nœud de Passes, avec d'autres onglets.
bool inspectorIsInBottomTabs(const Seen& seen)
{
    return seen.windows.at(InspectorEntry).dock == seen.windows.at(PassesEntry).dock &&
           seen.windows.at(InspectorEntry).windowsOnNode > 1;
}

/// **Écrire puis relire** (quatre lancements) : la première session règle la vitesse, décoche
/// « Temps réel », ferme la console par le menu et range l'inspecteur dans l'onglet du bas ; la
/// seconde retrouve tout, au même endroit.
int playRoundTrip(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"round-trip"};
    const Probe probe = probeOf(backend, true);
    const std::function<void(std::size_t)> moveInspector = [](std::size_t recorded)
    {
        if (recorded == MoveInspectorAt)
        {
            moveInspectorToBottom();
        }
    };
    const std::string script = clickScript(1, probe.speed) + clickScript(4, probe.realTime) +
                               menuClick(8, probe.menuTitle, probe.consoleItem);
    const Run first =
        play(backend, interactive(directory.path, SessionFrames, script, moveInspector));
    if (first.exitCode != 0 || first.seen.size() != SessionFrames)
    {
        return expect(false, "la première session : code 0 et une image relevée par image");
    }
    const Seen& end1 = first.seen.back();
    int failures =
        expect(consoleSays(first, "première session"), "le dossier vide est une première session") +
        expect(first.seen.front().realTime, "« Temps réel » part coché") +
        expect(end1.flySpeed > levain::editor::DefaultFlySpeed,
               "la barre d'outils a réglé la vitesse") +
        expect(!end1.realTime, "la barre d'outils a décoché « Temps réel »") +
        expect(!end1.consoleOpen, "le menu Fenêtre a fermé la console") +
        expect(inspectorIsInBottomTabs(end1), "l'inspecteur est dans l'onglet du bas");
    const auto saved1 = savedIn(directory.path);
    if (!saved1)
    {
        return failures + expect(false, "la première session a écrit un fichier lisible");
    }
    failures +=
        expect(saved1->flySpeed == end1.flySpeed && !saved1->realTime && !saved1->windows.console &&
                   saved1->windows.inspector && !saved1->layout.empty(),
               "le fichier tient la vitesse, « Temps réel », la console fermée et la "
               "disposition");

    // La seconde session : rien à cliquer, tout doit déjà être là à la première image.
    // Un clic en plus : « Temps réel » recoché. Le fichier de la session d'avant existe, il doit
    // être remplacé (et non seulement relu) : `saved2` tient ce changement.
    const Run second =
        play(backend, interactive(directory.path, 12, clickScript(4, probe.realTime)));
    if (second.exitCode != 0 || second.seen.size() != 12)
    {
        return failures +
               expect(false, "la seconde session : code 0 et une image relevée par image");
    }
    const Seen& start2 = second.seen.at(2);
    const Seen& end2 = second.seen.back();
    failures += expect(consoleSays(second, "préférences relues"),
                       "la console dit que les préférences sont relues") +
                expect(start2.flySpeed == saved1->flySpeed && end2.flySpeed == saved1->flySpeed,
                       "la vitesse est gardée") +
                expect(!start2.realTime && end2.realTime,
                       "« Temps réel » reste décoché à l'ouverture, puis le clic le recoche") +
                expect(!start2.consoleOpen && !end2.windows.at(ConsoleEntry).exists,
                       "la console reste fermée, sa fenêtre n'est pas dessinée") +
                expect(inspectorIsInBottomTabs(end2),
                       "l'inspecteur est dans l'onglet du bas, là où on l'a laissé") +
                expect(end2.nodes.inspector == end2.windows.at(InspectorEntry).dock,
                       "le nœud de l'inspecteur, retrouvé par le nom de la fenêtre, est celui où "
                       "il est rangé (le bas), pas la colonne de droite que `buildLayout` bâtit") +
                expect(end2.windows.at(HierarchyEntry).dock == end2.nodes.left &&
                           end2.windows.at(ViewEntry).dock == end2.nodes.center &&
                           end2.nodes.left != 0 && end2.nodes.center != 0,
                       "la hiérarchie et la Vue sont sur les nœuds retrouvés") +
                expect(end2.shortcuts == end1.shortcuts, "les accords par défaut sont inchangés");
    // Relue puis réécrite : la même disposition au caractère près, et le changement de la seconde
    // session (« Temps réel » recoché) a remplacé le fichier de la première.
    const auto saved2 = savedIn(directory.path);
    return failures + expectSaveRenamed(first, "la première session") +
           expectSaveRenamed(second, "la seconde session") +
           expect(saved2 && saved2->layout == saved1->layout &&
                      saved2->windows == saved1->windows && saved2->flySpeed == saved1->flySpeed &&
                      saved2->realTime,
                  "relue puis réécrite, la disposition est la même et « Temps réel » recoché est "
                  "dans le fichier");
}

/// **« Réinitialiser la disposition »** (quatre lancements) : une session dérange la disposition
/// (vitesse, console fermée, inspecteur en bas) puis la réinitialise ; la défaut revient, toutes
/// les fenêtres rouvertes, la vitesse reste, et le fichier est réécrit tout de suite (avant la
/// fermeture). La session d'après part de la défaut.
int playReset(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"reset"};
    const Probe probe = probeOf(backend, true);
    constexpr int Frames = 70;
    constexpr std::size_t BeforeReset = 23; // image relevée, avant le clic de « Réinitialiser »
    constexpr std::size_t AfterReset = 50;  // longtemps après, bien avant la fermeture
    bool fileBefore = true;
    std::optional<Preferences> onDiskRightAfter;
    const std::function<void(std::size_t)> poke = [&](std::size_t recorded)
    {
        if (recorded == 20)
        {
            moveInspectorToBottom();
        }
        if (recorded == BeforeReset)
        {
            fileBefore = fs::exists(fileIn(directory.path));
        }
        if (recorded == AfterReset)
        {
            onDiskRightAfter = savedIn(directory.path);
        }
    };
    const std::string script = clickScript(1, probe.speed) +
                               menuClick(6, probe.menuTitle, probe.consoleItem) +
                               menuClick(26, probe.menuTitle, probe.resetItem);
    const Run run = play(backend, interactive(directory.path, Frames, script, poke));
    if (run.exitCode != 0 || run.seen.size() != Frames)
    {
        return expect(false, "la réinitialisation : code 0 et une image relevée par image");
    }
    const Seen& before = run.seen.at(BeforeReset - 1);
    const Seen& last = run.seen.back();
    int failures = expect(inspectorIsInBottomTabs(before),
                          "avant le clic, l'inspecteur est dans l'onglet du bas") +
                   expect(!before.consoleOpen, "avant le clic, la console est fermée") +
                   expect(before.flySpeed > levain::editor::DefaultFlySpeed,
                          "avant le clic, la vitesse est réglée") +
                   expect(!fileBefore, "avant le clic, rien n'est encore écrit (pas de fichier)") +
                   expectDefaultLayout(last, "après « Réinitialiser »") +
                   expect(last.consoleOpen && last.windows.at(ConsoleEntry).exists,
                          "« Réinitialiser » rouvre la console") +
                   expect(last.flySpeed == before.flySpeed,
                          "« Réinitialiser » ne touche pas à la vitesse de la caméra") +
                   expect(onDiskRightAfter && onDiskRightAfter->windows.console &&
                              onDiskRightAfter->windows.inspector &&
                              onDiskRightAfter->flySpeed == before.flySpeed,
                          "le fichier est réécrit dès la réinitialisation, avant la fermeture");
    failures += expectSaveRenamed(run, "la réinitialisation");
    // La session d'après : la disposition par défaut revient d'elle-même.
    const Run next = play(backend, interactive(directory.path, 12));
    if (next.exitCode != 0 || next.seen.size() != 12)
    {
        return failures +
               expect(false, "après la réinitialisation : code 0 et une image par image");
    }
    return failures + expectDefaultLayout(next.seen.back(), "la session suivante") +
           expect(next.seen.back().consoleOpen, "la session suivante : la console est ouverte") +
           expect(next.seen.back().flySpeed == before.flySpeed,
                  "la session suivante : la vitesse est gardée");
}

/// **Quitter en jeu** (deux lancements) : « Temps réel » décoché en Édition, Alt+P joue (le jeu
/// force le temps réel), et la fenêtre se ferme en jeu : le fichier garde le choix de l'Édition,
/// pas le temps réel du jeu.
int playQuitWhilePlaying(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"play"};
    const Probe probe = probeOf(backend, false);
    constexpr int Frames = 30;
    const std::string script = clickScript(1, probe.realTime) +
                               "10 key down Left Alt\n10 key down P\n11 key up P\n"
                               "11 key up Left Alt\n";
    const Run run = play(backend, interactive(directory.path, Frames, script));
    if (run.exitCode != 0 || run.seen.size() != Frames)
    {
        return expect(false, "quitter en jeu : code 0 et une image relevée par image");
    }
    const Seen& edit = run.seen.at(8);
    const Seen& end = run.seen.back();
    const auto saved = savedIn(directory.path);
    return expect(edit.mode == levain::editor::Mode::Edit && !edit.realTime,
                  "en Édition, la case « Temps réel » est décochée") +
           expect(end.mode == levain::editor::Mode::PlayWithoutReturn,
                  "Alt+P joue, et la fenêtre se ferme en jeu") +
           expect(end.realTime, "en jeu, le temps réel est forcé") +
           expectSaveRenamed(run, "quitter en jeu") +
           expect(saved && !saved->realTime,
                  "le fichier garde le choix de l'Édition, pas le temps réel du jeu");
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

/// **Une exécution scriptée ne lit ni n'écrit** (cinq lancements) : sous `--steps` et `--seconds`
/// le dossier reste vide ; avec un fichier présent, `--input-script` garde la disposition et les
/// accords par défaut et laisse le fichier tel quel. Contrôle (règle n°7) : le même fichier, lu par
/// une exécution interactive, est bien lu.
int playScripted(const levain::tests::TestBackend& backend)
{
    const TempDirectory empty{"scripted-writes"};
    int failures = 0;
    const Run steps = play(backend, scripted(12, Idle, empty.path));
    failures += expect(steps.exitCode == 0 && steps.seen.size() == 12, "sous --steps : code 0") +
                expect(entriesIn(empty.path) == 0, "sous --steps, aucun fichier n'est écrit");
    const Run seconds = play(backend, secondsSession(empty.path, 0.4));
    failures += expect(seconds.exitCode == 0 && !seconds.seen.empty(), "sous --seconds : code 0") +
                expect(entriesIn(empty.path) == 0, "sous --seconds, aucun fichier n'est écrit");

    const TempDirectory present{"scripted-reads"};
    Preferences seed;
    seed.flySpeed = 20.0f;
    seed.windows.console = false;
    if (!levain::editor::savePreferences(fileIn(present.path), seed))
    {
        return failures + expect(false, "le fichier d'amorce s'écrit");
    }
    const std::string before = contentOf(fileIn(present.path));
    const Run reference = play(backend, scripted(12));
    const Run run = play(backend, scripted(12, Idle, present.path));
    if (run.exitCode != 0 || run.seen.size() != 12 || reference.seen.size() != 12)
    {
        return failures +
               expect(false, "sous --input-script : code 0 et une image relevée par image");
    }
    const Seen& last = run.seen.back();
    failures +=
        expectDefaultLayout(last, "sous --input-script, un fichier présent") +
        expect(last.consoleOpen && last.flySpeed == levain::editor::DefaultFlySpeed,
               "sous --input-script, la console est ouverte et la vitesse est la défaut") +
        expect(last.shortcuts == reference.seen.back().shortcuts,
               "sous --input-script, les accords sont ceux par défaut") +
        expect(!consoleSays(run, "préférences relues"),
               "sous --input-script, la console ne dit pas que les préférences sont relues") +
        expect(contentOf(fileIn(present.path)) == before && entriesIn(present.path) == 1,
               "sous --input-script, le fichier n'est ni réécrit ni accompagné d'un autre");
    const Run control = play(backend, interactive(present.path, 12));
    if (control.exitCode != 0 || control.seen.size() != 12)
    {
        return failures + expect(false, "le contrôle interactif : code 0 et une image par image");
    }
    return failures +
           expect(control.seen.back().flySpeed == 20.0f && !control.seen.back().consoleOpen,
                  "contrôle : une exécution interactive lit ce même fichier");
}

/// **Un fichier refusé, une disposition illisible** (trois lancements) : le premier est dit à la
/// console, mis de côté, la défaut se construit, et un fichier valide prend sa place à la
/// fermeture ; la seconde est écartée (valide pour nous, sans espace d'ancrage), la console le dit
/// et le reste des préférences est gardé ; un fichier refusé qu'on ne peut pas mettre de côté n'est
/// pas écrasé à la fermeture.
int playRefused(const levain::tests::TestBackend& backend)
{
    const TempDirectory directory{"refused"};
    // Un format abandonné (version 0), pas une version plus récente : celle-ci n'est jamais mise de
    // côté ni écrasée (voir plus bas).
    const std::string broken = "levain-editor 0\n[editeur]\n";
    std::ofstream{fileIn(directory.path), std::ios::binary} << broken;
    const Run run = play(backend, interactive(directory.path, 12));
    if (run.exitCode != 0 || run.seen.size() != 12)
    {
        return expect(false, "le fichier refusé : code 0 et une image relevée par image");
    }
    fs::path aside = fileIn(directory.path);
    aside += ".bad";
    int failures = expectDefaultLayout(run.seen.back(), "fichier refusé") +
                   expect(consoleSays(run, "format abandonné"),
                          "la console dit pourquoi le fichier est refusé") +
                   expect(fs::exists(aside) && contentOf(aside) == broken,
                          "le fichier refusé est gardé en .bad, intact") +
                   expect(savedIn(directory.path).has_value(),
                          "un fichier valide le remplace à la fermeture") +
                   expectSaveRenamed(run, "le fichier refusé");

    // Un `.bad` qui est un dossier non vide : le renommage échoue, partout et même en root.
    const TempDirectory stuck{"stuck"};
    std::ofstream{fileIn(stuck.path), std::ios::binary} << broken;
    fs::path stuckAside = fileIn(stuck.path);
    stuckAside += ".bad";
    fs::create_directories(stuckAside);
    std::ofstream{stuckAside / "occupe"} << "x";
    const Run kept = play(backend, interactive(stuck.path, 12));
    if (kept.exitCode != 0 || kept.seen.size() != 12)
    {
        return failures + expect(false, "le fichier qu'on ne peut pas mettre de côté : code 0");
    }
    failures +=
        expect(consoleSays(kept, "pas d'enregistrement cette session"),
               "la console dit que la session n'enregistre pas") +
        expect(contentOf(fileIn(stuck.path)) == broken,
               "le fichier refusé, mis de côté sans succès, n'est pas écrasé à la fermeture");

    // Un fichier d'une version plus récente (une autre branche de l'éditeur l'a écrit) : ni mis
    // de côté, ni écrasé par une v1 à la fermeture. La session n'enregistre pas, et le dit.
    const TempDirectory newer{"newer"};
    const std::string future = "levain-editor 2\n[editeur]\nvitesse_camera = 7\n";
    std::ofstream{fileIn(newer.path), std::ios::binary} << future;
    const Run kept2 = play(backend, interactive(newer.path, 12));
    if (kept2.exitCode != 0 || kept2.seen.size() != 12)
    {
        return failures + expect(false, "le fichier d'une version plus récente : code 0");
    }
    fs::path newerAside = fileIn(newer.path);
    newerAside += ".bad";
    failures += expectDefaultLayout(kept2.seen.back(), "version plus récente") +
                expect(consoleSays(kept2, "version plus récente") &&
                           consoleSays(kept2, "pas d'enregistrement cette session"),
                       "la console dit la version et que la session n'enregistre pas") +
                expect(contentOf(fileIn(newer.path)) == future && !fs::exists(newerAside),
                       "le fichier plus récent reste tel quel, sans .bad, après la fermeture");

    const TempDirectory layout{"layout"};
    Preferences preferences;
    preferences.flySpeed = 20.0f;
    preferences.layout = "n'importe quoi\n[Window][Inconnue]\nPos=1,1\n";
    if (!levain::editor::savePreferences(fileIn(layout.path), preferences))
    {
        return failures + expect(false, "la disposition illisible : le fichier d'essai s'écrit");
    }
    const Run unreadable = play(backend, interactive(layout.path, 12));
    if (unreadable.exitCode != 0 || unreadable.seen.size() != 12)
    {
        return failures + expect(false, "la disposition illisible : code 0 et une image par image");
    }
    return failures + expectDefaultLayout(unreadable.seen.back(), "disposition illisible") +
           expect(consoleSays(unreadable, "aucun espace d'ancrage"),
                  "la console dit que la disposition est illisible") +
           expect(unreadable.seen.back().flySpeed == 20.0f,
                  "le reste des préférences est gardé quand seule la disposition est illisible");
}

/// Un scénario par nom : `levain_editor_preferences <backend> <scénario>`.
struct Scenario
{
    std::string_view name;
    int (*run)(const levain::tests::TestBackend&);
};

constexpr std::array<Scenario, 6> Scenarios{{{"round-trip", playRoundTrip},
                                             {"reset", playReset},
                                             {"play", playQuitWhilePlaying},
                                             {"central", playViewClosed},
                                             {"scripted", playScripted},
                                             {"refused", playRefused}}};

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
                                 "<round-trip|reset|play|central|scripted|refused>");
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
