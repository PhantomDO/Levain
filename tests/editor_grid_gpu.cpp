// La grille au sol, le menu « Afficher » et la touche G dans la vraie boucle (ADR-0036, décisions
// 13 et 15, morceau 17) : `withEditor` autour d'un programme minimal, un script d'input rejoué hors
// écran, la disposition épinglée (QWERTY, 1280 x 800, panneaux ouverts, aucune préférence : une
// exécution scriptée n'en lit ni n'en écrit). **Ce que la grille a dessiné est observé** :
// `GridState::framesDrawn` et `lastLines` sont ceux de l'étape de rendu, et le scénario `pixels`
// lit **l'image** (la capture de l'application). La validation du device est exigée (Debug), et le
// test échoue au moindre message d'erreur du journal. Un scénario par exécution, quatre lancements
// de l'application au plus :
//   menu    Afficher > Grille cache puis rend la grille, Afficher > Vue de jeu cache les aides ;
//           la grille décochée reste cachée quand la vue de jeu s'en va ; la souris sur le menu
//           ouvert n'est pas sur la scène
//   key     G bascule la vue de jeu dans la Vue ; avec une autre fenêtre focalisée et survolée, G
//           ne fait rien ; la Vue survolée la reprend
//   play    Jouer cache les aides, G ne fait rien en jeu ; revenir à l'Édition les rend
//   pixels  l'image : grille cochée, décochée, vue de jeu ; des pixels bleus (l'axe Z) et rouges
//           (l'axe X) n'existent que grille cochée, et seulement sous la rangée du bord lointain
//   levain_editor_grid <vulkan|d3d12|d3d12-warp> <scénario>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <print>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/assets/image.hpp"
#include "levain/core/log.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/grid.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/stages.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::editor::Action;
using levain::editor::Editor;

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 800;
/// Un point au milieu de la Vue (panneaux ouverts, 1280 x 800), et un point dans la hiérarchie.
constexpr float ViewX = 640.0f;
constexpr float ViewY = 420.0f;
constexpr float HierarchyX = 60.0f;
constexpr float HierarchyY = 300.0f;

/// Ce que l'éditeur a fait à une image, relevé à la fin de `ui`. Le dessin de la grille est celui
/// des images **d'avant** : le rendu suit `ui`.
struct Seen
{
    bool grid = false;     ///< « Grille » est cochée
    bool gameView = false; ///< « Vue de jeu » est cochée
    bool helpers = false;  ///< `App::editorHelpersVisible`, tel que `ui` l'a laissé
    int framesDrawn = 0;   ///< les images où la grille est partie
    int lastLines = 0;     ///< ses lignes à la dernière image rendue
    int gameViewPerformed = 0;
    bool viewFocused = false;
    bool viewHovered = false;
    levain::app::ScreenRect image{}; ///< où la Vue montre la scène, en pixels de la fenêtre
    /// Les barres que le texte d'ImGui recouvre dans l'image de la scène (mode, « Afficher »).
    levain::app::ScreenRect modeBar{};
    levain::app::ScreenRect showBar{};
    levain::editor::Mode mode = levain::editor::Mode::Edit;
    levain::editor::ShowMenuLayout menu;
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    std::vector<std::string> errors; ///< les messages d'erreur du journal, du démarrage à la fin
    App* app = nullptr;
    glm::vec3 cameraPosition{0.0f};
    glm::mat4 viewProjection{1.0f}; ///< celle du rendu de la scène, à la dernière image
};

/// Ce qu'un scénario ajoute à une exécution : la capture de la dernière image, et ce qu'un humain
/// ferait à la souris (le banc règle la case à sa place, comme les autres tests de l'éditeur).
struct Extras
{
    std::optional<std::filesystem::path> capture;
    std::function<void(Editor&, int frame)> poke;
};

Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames,
         const Extras& extras = {})
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
    settings.capturePath = extras.capture;
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    std::mutex errorsMutex; // les messages de Jolt et du chargeur partent d'autres fils
    const levain::core::LogSinkHandle sink = levain::core::addLogSink(
        [&run, &errorsMutex](const levain::core::LogRecord& record)
        {
            if (record.level >= levain::core::LogLevel::Error)
            {
                const std::scoped_lock lock{errorsMutex};
                run.errors.push_back(std::string{record.category} + " : " +
                                     std::string{record.message});
            }
        });
    levain::editor::EditorOptions options;
    // Les polices de l'éditeur (morceau 17a) : celles d'assets-cache, que `data/` seul n'a pas.
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.afterUi = [&run, &extras](const Editor& editor)
    {
        run.seen.push_back({.grid = editor.view.show.grid,
                            .gameView = editor.view.show.gameView,
                            .helpers = run.app->editorHelpersVisible,
                            .framesDrawn = editor.grid.framesDrawn,
                            .lastLines = editor.grid.lastLines,
                            .gameViewPerformed = editor.actions.performed.at(
                                levain::editor::indexOf(Action::GameView)),
                            .viewFocused = editor.view.focused,
                            .viewHovered = editor.view.hovered,
                            .image = editor.view.image,
                            .modeBar = editor.view.modeBarRect,
                            .showBar = editor.view.showMenu.bar,
                            .mode = editor.mode.current,
                            .menu = editor.view.showMenu});
        if (extras.poke)
        {
            extras.poke(const_cast<Editor&>(editor), static_cast<int>(run.seen.size()));
        }
    };
    const auto program = [&run](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        // L'éditeur part de la caméra du jeu : 3 m de haut, penchée de 20 degrés vers le sol, de
        // quoi voir la grille et son bord lointain.
        app.world.entity("camera")
            .set(levain::scene::Transform{
                .position = {0.0f, 3.0f, 8.0f},
                .rotation = glm::angleAxis(glm::radians(-20.0f), glm::vec3{1.0f, 0.0f, 0.0f})})
            .set(levain::app::CameraLens{});
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition; // épinglé
        run.app = &app;
        levain::render::addStageFunction(app.renderer.stages, levain::render::RenderStage::Opaque,
                                         "témoin",
                                         [&run](const levain::render::StageContext& context)
                                         {
                                             run.viewProjection = context.viewProjection;
                                             run.cameraPosition = context.cameraPosition;
                                         });
        return levain::app::FrameHooks{};
    };
    run.exitCode = levain::app::runApp(settings, levain::editor::withEditor(program, options));
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

/// Une exécution qui va au bout, une image relevée par image, et aucune erreur au journal (la
/// validation du device en fait partie).
int expectClean(const Run& run, int frames)
{
    for (const std::string& error : run.errors)
    {
        std::println(stderr, "erreur au journal : {}", error);
    }
    return expect(run.exitCode == 0, "l'exécution va au bout, code 0") +
           expect(run.seen.size() == static_cast<std::size_t>(frames),
                  "une image relevée par image du script") +
           expect(run.errors.empty(), "aucune erreur au journal (validation comprise)");
}

/// Un clic à `at` : la souris y va à l'image `frame`, le bouton s'enfonce deux images plus tard.
std::string clickScript(int frame, ImVec2 at)
{
    return std::format("{} mouse move {} {}\n{} button down left\n{} button up left\n", frame, at.x,
                       at.y, frame + 2, frame + 3);
}

std::string tapKey(int frame, std::string_view key)
{
    return std::format("{} key down {}\n{} key up {}\n", frame, key, frame + 1, key);
}

std::string moveTo(int frame, float x, float y)
{
    return std::format("{} mouse move {} {}\n", frame, x, y);
}

/// Les lignes (tronçons) que la grille envoie : au moins une, jamais plus que son plafond.
constexpr int MaxGridLines = static_cast<int>(levain::editor::MaxGridLines);

/// La grille est dessinée à l'image `frame` (relevée trois images après son changement au plus tôt)
/// : ses lignes sont parties, et la compte avance encore d'une image à la suivante.
int expectDrawn(const Run& run, std::size_t frame, std::string_view at)
{
    const Seen& now = run.seen.at(frame);
    const Seen& before = run.seen.at(frame - 1);
    return expect(now.lastLines > 0 && now.lastLines <= MaxGridLines &&
                      now.framesDrawn > before.framesDrawn,
                  std::format("{} : la grille dessine ses lignes (1 à {})", at, MaxGridLines));
}

/// Elle n'est pas dessinée : l'étape a tourné sans rien envoyer, et la compte ne bouge plus.
int expectHidden(const Run& run, std::size_t frame, std::string_view at)
{
    const Seen& now = run.seen.at(frame);
    const Seen& before = run.seen.at(frame - 1);
    return expect(now.lastLines == 0 && now.framesDrawn == before.framesDrawn,
                  std::format("{} : la grille ne dessine rien", at));
}

/// Un script qui ne fait que garder la souris sur la Vue, sans clic ni touche.
std::string idleOnView()
{
    return moveTo(0, ViewX, ViewY);
}

/// **Le menu** : Afficher > Grille cache la grille (rien ne part au rendu) puis la rend ; Afficher
/// > Vue de jeu cache les aides (la grille, le drapeau de l'App) ; décochée, la grille ne revient
/// pas avec la vue de jeu qui s'en va. Trois lancements : la position du bouton, celle des entrées,
/// le script.
int playMenu(const levain::tests::TestBackend& backend)
{
    // 1. Le bouton, et la grille qui dessine avant tout clic.
    const Run idle = play(backend, idleOnView(), 8);
    int failures = expectClean(idle, 8);
    if (failures != 0 || !idle.seen.back().viewHovered)
    {
        return failures + expect(false, "le sondage : la Vue est survolée, sans erreur");
    }
    failures +=
        expect(idle.seen.back().grid && !idle.seen.back().gameView && idle.seen.back().helpers,
               "au départ : la grille cochée, la vue de jeu non, les aides visibles") +
        expectDrawn(idle, 7, "au départ");
    const ImVec2 button = idle.seen.back().menu.button;
    // 2. Les entrées, lues dans le menu ouvert.
    const Run opened = play(backend, clickScript(2, button), 10);
    failures += expectClean(opened, 10);
    const Seen& open = opened.seen.back();
    if (!open.menu.open)
    {
        return failures + expect(false, "le sondage : le menu s'ouvre au clic sur « Afficher »");
    }
    const ImVec2 gridItem = open.menu.gridItem;
    const ImVec2 gameItem = open.menu.gameViewItem;
    // 3. Le script : à chaque étape le menu se rouvre (un clic sur une entrée le ferme).
    //   10 : Grille (décochée)       -> cachée
    //   22 : Grille (recochée)       -> dessinée
    //   34 : Vue de jeu (cochée)     -> cachée, drapeau tombé
    //   46 : Grille (décochée)       -> cachée toujours
    //   58 : Vue de jeu (décochée)   -> la grille reste décochée : cachée
    //   70 : Grille (recochée)       -> dessinée
    std::string script = idleOnView();
    const std::array<std::pair<int, ImVec2>, 6> steps{{{10, gridItem},
                                                       {22, gridItem},
                                                       {34, gameItem},
                                                       {46, gridItem},
                                                       {58, gameItem},
                                                       {70, gridItem}}};
    for (const auto& [frame, item] : steps)
    {
        script += clickScript(frame - 6, button) + clickScript(frame, item);
    }
    constexpr int Frames = 84;
    const Run run = play(backend, script, Frames);
    failures += expectClean(run, Frames);
    if (run.seen.size() != Frames)
    {
        return failures;
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    failures += expectDrawn(run, 8, "avant tout clic");
    // Le menu ouvert, la souris sur son entrée « Grille » (au-dessus de l'image de la Vue) : la
    // scène n'est pas survolée, le clic suivant ne vise pas le jeu.
    failures += expect(at(11).menu.open && !at(11).viewHovered,
                       "la souris sur le menu ouvert n'est pas sur la scène");
    // Chaque état est relevé trois images après son clic (le rendu suit `ui`).
    failures += expect(!at(16).grid && at(16).helpers && !at(16).gameView,
                       "Grille décochée : la case, les aides toujours visibles") +
                expectHidden(run, 17, "Grille décochée");
    failures += expect(at(28).grid, "Grille recochée") + expectDrawn(run, 29, "Grille recochée");
    failures += expect(at(40).gameView && !at(40).helpers && at(40).grid,
                       "Vue de jeu : cochée, les aides tombent, la grille reste cochée") +
                expectHidden(run, 41, "vue de jeu") +
                expect(at(40).gameViewPerformed == 1, "l'action est comptée une fois");
    failures += expect(!at(52).grid && at(52).gameView && !at(52).helpers,
                       "Grille décochée en vue de jeu") +
                expectHidden(run, 53, "vue de jeu sans grille");
    failures += expect(!at(64).gameView && at(64).helpers && !at(64).grid,
                       "la vue de jeu s'en va, les aides reviennent") +
                expectHidden(run, 65, "la grille décochée ne revient pas avec les aides") +
                expect(at(64).gameViewPerformed == 2, "l'action est comptée deux fois");
    failures += expect(at(76).grid, "Grille recochée en fin de script") +
                expectDrawn(run, 77, "la grille revient");
    return failures;
}

/// **La touche G** : dans la Vue elle bascule la vue de jeu (la grille s'éteint, le drapeau tombe)
/// et la bascule encore ; avec la hiérarchie focalisée et survolée elle ne fait rien ; la Vue
/// survolée la reprend sans clic.
int playKey(const levain::tests::TestBackend& backend)
{
    // 0 : la souris sur la Vue ; G à 6 (cachée) et à 16 (rendue) ;
    // 22 : clic dans la hiérarchie (focus ailleurs), G à 32 : rien ;
    // 38 : la souris revient sur la Vue (survolée, non focalisée), G à 46 : la vue de jeu.
    std::string script = idleOnView() + tapKey(6, "G") + tapKey(16, "G") +
                         clickScript(22, {HierarchyX, HierarchyY}) + tapKey(32, "G") +
                         moveTo(38, ViewX, ViewY) + tapKey(46, "G");
    constexpr int Frames = 58;
    const Run run = play(backend, script, Frames);
    int failures = expectClean(run, Frames);
    if (run.seen.size() != Frames)
    {
        return failures;
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    failures += expect(at(5).viewHovered && !at(5).gameView, "avant G : la Vue est survolée") +
                expectDrawn(run, 5, "avant G");
    failures += expect(at(12).gameView && !at(12).helpers && at(12).gameViewPerformed == 1,
                       "G dans la Vue : la vue de jeu, les aides tombent") +
                expectHidden(run, 12, "après G");
    failures += expect(!at(20).gameView && at(20).helpers && at(20).gameViewPerformed == 2,
                       "G encore : les aides reviennent") +
                expectDrawn(run, 21, "après le second G");
    failures += expect(!at(30).viewFocused && !at(30).viewHovered,
                       "la hiérarchie a pris le focus et la souris : la Vue n'est ni focalisée ni "
                       "survolée");
    failures += expect(!at(36).gameView && at(36).helpers && at(36).gameViewPerformed == 2,
                       "G ailleurs que dans la Vue ne fait rien") +
                expectDrawn(run, 37, "G ailleurs : la grille dessine toujours");
    failures +=
        expect(at(44).viewHovered && !at(44).viewFocused, "la Vue est survolée sans le focus") +
        expect(at(52).gameView && !at(52).helpers && at(52).gameViewPerformed == 3,
               "survolée, la Vue prend G sans clic") +
        expectHidden(run, 53, "après le troisième G");
    return failures;
}

/// **Jouer** : les aides tombent (la grille ne dessine plus), G ne fait rien en jeu, Échap les
/// rend.
int playPlay(const levain::tests::TestBackend& backend)
{
    // Alt+P à 6 : le jeu ; G à 14 (en jeu : rien) ; Échap à 22 : l'Édition.
    std::string script = idleOnView() +
                         "6 key down Left Alt\n6 key down P\n7 key up P\n7 key up Left Alt\n" +
                         tapKey(14, "G") + tapKey(22, "Escape");
    constexpr int Frames = 34;
    const Run run = play(backend, script, Frames);
    int failures = expectClean(run, Frames);
    if (run.seen.size() != Frames)
    {
        return failures;
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    failures += expect(at(5).mode == levain::editor::Mode::Edit && at(5).helpers,
                       "en Édition les aides se voient") +
                expectDrawn(run, 5, "en Édition");
    failures += expect(at(12).mode == levain::editor::Mode::PlayWithoutReturn && !at(12).helpers,
                       "en jeu : les aides tombent") +
                expectHidden(run, 12, "en jeu");
    failures += expect(at(18).mode == levain::editor::Mode::PlayWithoutReturn && !at(18).gameView &&
                           at(18).gameViewPerformed == 0,
                       "G en jeu va au jeu, pas à la vue de jeu") +
                expectHidden(run, 18, "G en jeu");
    failures += expect(at(30).mode == levain::editor::Mode::Edit && at(30).helpers,
                       "Échap : l'Édition, les aides reviennent") +
                expectDrawn(run, 31, "retour à l'Édition");
    return failures;
}

/// Un dossier à soi pour les captures, effacé à la fin.
struct TempDirectory
{
    std::filesystem::path path;

    TempDirectory()
        : path{std::filesystem::temp_directory_path() /
               std::format("levain-editor-grid-{}", std::random_device{}())}
    {
        std::filesystem::create_directories(path);
    }

    ~TempDirectory()
    {
        std::error_code ignored; // un destructeur ne lève pas : le dossier reste dans le temporaire
        std::filesystem::remove_all(path, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
};

/// Une capture de la scène : l'image de la fenêtre à la dernière image, et ce qu'il faut pour la
/// lire.
struct Shot
{
    levain::assets::Image image;
    levain::app::ScreenRect view{};
    levain::app::ScreenRect modeBar{}; ///< ce qu'ImGui écrit par-dessus la scène : hors du scan
    levain::app::ScreenRect showBar{};
    glm::vec3 cameraPosition{0.0f};
    glm::mat4 viewProjection{1.0f};
    int exitFailures = 0;
};

/// Lance `frames` images, règle la case à l'image 2 par `poke`, et lit la capture.
Shot shoot(const levain::tests::TestBackend& backend, const std::filesystem::path& file, int frames,
           const std::function<void(Editor&)>& poke)
{
    const Run run = play(backend, idleOnView(), frames,
                         {.capture = file,
                          .poke = [&poke](Editor& editor, int frame)
                          {
                              if (frame == 2)
                              {
                                  poke(editor);
                              }
                          }});
    Shot shot;
    // La capture rend une image de plus que `steps` (la boucle la fait après la dernière).
    shot.exitFailures = expectClean(run, frames + 1);
    shot.view = run.seen.back().image;
    shot.modeBar = run.seen.back().modeBar;
    shot.showBar = run.seen.back().showBar;
    shot.viewProjection = run.viewProjection;
    shot.cameraPosition = run.cameraPosition;
    auto image = levain::assets::loadImage(file);
    if (!image)
    {
        throw std::runtime_error{image.error().message};
    }
    shot.image = std::move(*image);
    return shot;
}

struct Pixel
{
    int r = 0, g = 0, b = 0;
};

/// Le pixel (x, y) est sous l'une des barres qu'ImGui écrit par-dessus la scène, à `margin` pixels
/// près (le contour du fond et sa bordure débordent d'un demi-pixel).
bool underOverlay(const Shot& shot, int x, int y, float margin)
{
    const auto inside = [&](const levain::app::ScreenRect& rect)
    {
        return rect.width > 0.0f && static_cast<float>(x) >= rect.x - margin &&
               static_cast<float>(x) < rect.x + rect.width + margin &&
               static_cast<float>(y) >= rect.y - margin &&
               static_cast<float>(y) < rect.y + rect.height + margin;
    };
    return inside(shot.modeBar) || inside(shot.showBar);
}

Pixel pixelAt(const levain::assets::Image& image, int x, int y)
{
    const std::size_t at =
        (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4;
    return {image.rgba[at], image.rgba[at + 1], image.rgba[at + 2]};
}

/// **L'image**, non l'appel de dessin : la grille cochée, décochée, et en vue de jeu, caméra
/// épinglée (celle du jeu, que l'éditeur reprend : en (0, 3, 8), penchée de 20 degrés), lues dans
/// la capture. Sur la Vue : des pixels bleus (l'axe Z, droit devant) et rouges (l'axe X) n'existent
/// que grille cochée ; l'image ne diffère de la grille décochée que sous la rangée où le bord
/// lointain de la grille se projette (au-dessus, rien de la grille) ; la vue de jeu est identique à
/// la grille décochée. Trois lancements.
int playPixels(const levain::tests::TestBackend& backend)
{
    constexpr int Frames = 14;
    const TempDirectory directory;
    const Shot on = shoot(backend, directory.path / "on.png", Frames, [](Editor&) {});
    const Shot off = shoot(backend, directory.path / "off.png", Frames,
                           [](Editor& editor) { editor.view.show.grid = false; });
    const Shot game = shoot(backend, directory.path / "game.png", Frames,
                            [](Editor& editor) { editor.view.show.gameView = true; });
    int failures = on.exitFailures + off.exitFailures + game.exitFailures;
    const bool sameSize =
        on.image.width == off.image.width && on.image.height == off.image.height &&
        on.image.width == game.image.width && on.image.height == game.image.height;
    failures += expect(sameSize && on.image.width == WindowWidth && on.image.height == WindowHeight,
                       "les trois captures ont la taille de la fenêtre");
    if (!sameSize || on.view.width < 100.0f)
    {
        return failures + expect(false, "le sondage : des captures de même taille, la Vue ouverte");
    }
    // La rangée où le bord lointain de la grille (z = centre − portée, à y = 0) se projette : sous
    // l'horizon, la grille y est transparente (opacité nulle à la portée, que limite le plan
    // lointain de la caméra de l'éditeur). Le bord est celui de la plus grosse famille visible.
    const float farPlane = levain::editor::farDistanceOf(
        levain::render::frustumOf(on.viewProjection), on.cameraPosition);
    float reach = 0.0f;
    float step = 1.0f;
    for (const levain::editor::GridLevel& level :
         levain::editor::gridLevelsFor(on.cameraPosition.y))
    {
        const float levelReach =
            levain::editor::gridReachFor(level.step, on.cameraPosition.y, farPlane);
        if (level.visibility >= levain::editor::MinGridVisibility && levelReach > reach)
        {
            reach = levelReach;
            step = level.step;
        }
    }
    const float centerZ = std::round(on.cameraPosition.z / step) * step;
    const glm::vec4 farEdge = on.viewProjection * glm::vec4{0.0f, 0.0f, centerZ - reach, 1.0f};
    const float farRow = on.view.y + (1.0f - farEdge.y / farEdge.w) * 0.5f * on.view.height;
    constexpr float FarBand =
        8.0f; // les rangées sous le bord lointain où la grille est presque effacée
    int blue = 0;
    int red = 0;
    int differing = 0;
    int differingGame = 0;
    int firstDifferingRow = static_cast<int>(on.image.height);
    int strongFarChanges = 0; ///< les pixels très changés juste sous le bord lointain
    const int x0 = static_cast<int>(on.view.x);
    const int y0 = static_cast<int>(on.view.y);
    // **Les barres d'ImGui sont hors du scan** (mode, « Afficher ») : leur texte n'est pas les
    // mêmes pixels d'une exécution à l'autre (deux pixels du rappel « Alt+P : jouer » changeaient,
    // sur lavapipe en Release : 10 échecs sur 16), et ce n'est pas la grille qu'on y lit. Ailleurs,
    // l'égalité reste exacte. Elles existent (rien à laisser de côté sinon : le contrôle se
    // tairait) et restent petites devant la Vue, qui ne peut pas être cachée toute entière.
    constexpr float OverlayMargin = 2.0f;
    float overlayArea = 0.0f;
    for (const auto& rect : {on.modeBar, on.showBar})
    {
        overlayArea += (rect.width + 2.0f * OverlayMargin) * (rect.height + 2.0f * OverlayMargin);
    }
    failures += expect(on.modeBar.width > 0.0f && on.showBar.width > 0.0f,
                       "les barres de mode et « Afficher » sont dessinées et lues par le test") +
                expect(overlayArea < 0.1f * on.view.width * on.view.height,
                       "les barres laissées de côté sont petites devant la Vue");
    for (int y = y0; y < y0 + static_cast<int>(on.view.height); ++y)
    {
        for (int x = x0; x < x0 + static_cast<int>(on.view.width); ++x)
        {
            if (underOverlay(on, x, y, OverlayMargin))
            {
                continue;
            }
            const Pixel a = pixelAt(on.image, x, y);
            const Pixel b = pixelAt(off.image, x, y);
            const Pixel c = pixelAt(game.image, x, y);
            if (a.r != b.r || a.g != b.g || a.b != b.b)
            {
                ++differing;
                firstDifferingRow = std::min(firstDifferingRow, y);
            }
            if (static_cast<float>(y) < farRow + FarBand)
            {
                const int change =
                    std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
                strongFarChanges += change > 60 ? 1 : 0;
            }
            if (c.r != b.r || c.g != b.g || c.b != b.b)
            {
                ++differingGame;
            }
            // Un pixel que la grille a rendu bleu (ou rouge) alors qu'il ne l'était pas dessous.
            if (a.b > a.r + 40 && a.b > b.b - 10 && a.b - b.b > 10 && a.b > a.g)
            {
                ++blue;
            }
            if (a.r > a.b + 40 && a.r - b.r > 10)
            {
                ++red;
            }
        }
    }
    std::println(
        stderr,
        "mesure : {} pixels changés, premier à la rangée {} (bord lointain {:.1f}), "
        "{} bleus, {} rouges, {} changés en vue de jeu, {} pixels très changés au bord lointain",
        differing, firstDifferingRow, farRow, blue, red, differingGame, strongFarChanges);
    failures += expect(differing > 500, "la grille change l'image de la Vue") +
                expect(blue > 20, "des pixels bleus : l'axe Z, droit devant") +
                expect(red > 20, "des pixels rouges : l'axe X") +
                // La marge de 2 pixels : le tracé d'une ligne n'est pas le même sur chaque
                // rastériseur (WARP, lavapipe).
                expect(static_cast<float>(firstDifferingRow) >= farRow - 2.0f,
                       "rien de la grille au-dessus de la rangée du bord lointain") +
                expect(strongFarChanges < 2000,
                       "la grille s'estompe au bord lointain (mêlée par son opacité, non opaque)") +
                expect(differingGame == 0, "la vue de jeu est identique à la grille décochée");
    return failures;
}

/// Un scénario par nom : `levain_editor_grid <backend> <scénario>`.
struct Scenario
{
    std::string_view name;
    int (*run)(const levain::tests::TestBackend&);
};

constexpr std::array<Scenario, 4> Scenarios{
    {{"menu", playMenu}, {"key", playKey}, {"play", playPlay}, {"pixels", playPixels}}};

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
            std::println(
                stderr,
                "usage : levain_editor_grid <vulkan|d3d12|d3d12-warp> <menu|key|play|pixels>");
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
