#include "levain/editor/editor.hpp"

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>
// `ClearActiveID` : l'API interne d'ImGui, que le vol appelle en commençant (voir `frameGestures`).
#include <imgui_internal.h>

#include "levain/app/camera.hpp"
#include "levain/core/error.hpp"
#include "levain/core/log.hpp"
#include "levain/editor/layout.hpp"
#include "levain/editor/scene_bounds.hpp"
#include "levain/platform/input.hpp"
#include "levain/render/debug_lines.hpp"
#include "levain/render/renderer.hpp"
#include "levain/render/stages.hpp"
#include "levain/scene/scene.hpp"
#include "levain/ui/fonts.hpp"
#include "levain/ui/style.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Le bilan de l'éditeur, une ligne que la CI lit : les entités que la hiérarchie listait et les
/// champs que l'inspecteur a dessinés pour la sélection à la dernière image, zéro pour un panneau
/// jamais dessiné, et la sélection. La CI échoue si la ligne manque, si elle compte zéro entité ou
/// zéro champ, ou si la sélection n'est pas celle de `--select`.
void logEditor(const flecs::world& world, const Editor& editor)
{
    const flecs::entity selected = selectedIfAlive(world, editor.selected);
    const std::string name = selected ? std::string{selected.path("::", "").c_str()} : "aucune";
    core::log("editor", core::LogLevel::Info,
              "éditeur : {} entités, {} champs dessinés ; sélection : {}",
              editor.hierarchy.rows.size(), editor.inspector.fieldsDrawn, name);
}

/// Le mode et les pas joués, la ligne que lit la CI sous son nom stable (ADR-0036, décision 14) :
/// `editor.mode mode=edit steps=0`, des mots sans langue, puis la phrase. La CI échoue si la ligne
/// manque, ou si l'éditeur lancé sans script n'est pas en Édition à zéro pas. `steps` compte tous
/// les pas de la boucle (`App::stepsPlayed`), que le jeu soit passé par là ou non.
void logMode(const app::App& app, const Editor& editor)
{
    const Mode mode = editor.mode.current;
    core::log("editor", core::LogLevel::Info, "editor.mode mode={} steps={} -- {}",
              modeTokenOf(mode), app.stepsPlayed,
              ui::trf("mode : {} ; pas : {}", ui::tr(modeNameOf(mode)), app.stepsPlayed));
}

/// La taille de la Vue, la ligne que lit la CI sous son nom stable (`editor.view`, ADR-0036,
/// décision 14) : `editor.view width=640 height=360 -- vue : 640×360`. La CI échoue si la ligne
/// manque ou si la Vue n'a aucune taille à la fin d'une exécution panneaux ouverts.
void logView(const View& view)
{
    core::log("editor", core::LogLevel::Info, "editor.view width={} height={} -- {}",
              view.requested.width, view.requested.height,
              ui::trf("vue : {}×{}", view.requested.width, view.requested.height));
}

/// **Impose la caméra de l'éditeur** à l'App : son rendu, avec les plans de découpe de la scène.
/// Une ligne dans `withEditor` ; la boucle la relit après `ui` (`settleAfterUi`). `edited` : cette
/// image a écrit dans le monde ou changé de mode, la boîte de la scène est à refaire.
void imposeCamera(app::App& app, Editor& editor, bool edited = false)
{
    const render::Box scene =
        cachedSceneBoxOf(editor.sceneBounds,
                         {.entityCount = static_cast<std::size_t>(editor.everything.count()),
                          .modelCount = app.models.size(),
                          .edited = edited},
                         editor.everything, app.models, app.modelInstance);
    app.cameraOverride = toRenderCamera(editor.camera, editorClipPlanes(editor.camera, scene));
}

/// La taille de l'interface : 15 px, là où la police par défaut d'ImGui en fait 13. À corps égal,
/// Inter a des minuscules plus petites que ProggyClean (sa hauteur d'x est plus basse) : 15 px
/// rend aux menus et aux panneaux la lisibilité qu'ils avaient. Elle déplace les barres, donc les
/// points visés des tests de la Vue (`tests/data/editor/vue_pick_*.txt`).
constexpr float FontSize = 15.0f;

/// Les polices de l'éditeur (Inter, Noto Sans, Material Symbols, et celle de la console), lues dans
/// `options.fontRoots`, sinon dans les racines d'assets de l'App. Absentes, l'éditeur ne démarre
/// pas (règle n°7) : l'erreur nomme le fichier. Avant la première image.
core::Result<ui::UiFonts> loadEditorFonts(const app::App& app, const EditorOptions& options)
{
    const auto files =
        ui::findFontFiles(options.fontRoots ? *options.fontRoots : app.settings.assetRoots);
    if (!files)
    {
        return std::unexpected(files.error());
    }
    return ui::addFonts(ImGui::GetIO(), *files, FontSize);
}

/// L'accord affiché dans l'entrée de menu de l'action, selon le clavier d'aujourd'hui.
std::string shortcutTextOf(const Editor& editor, const app::App& app, Action action)
{
    const Binding* binding = bindingOf(editor.shortcuts, action);
    return binding != nullptr ? displayNameOf(binding->chords.at(0), app.keyAtPosition)
                              : std::string{};
}

/// Où un geste de caméra peut commencer : l'image de la Vue panneaux ouverts (vide si elle n'est
/// pas montrée), la scène entière panneaux fermés (« les gestes s'y font partout », décision 7).
app::ScreenRect gestureRegionOf(const app::App& app, const Editor& editor)
{
    return app.ui.panelsOpen ? editor.view.image : app.sceneRect;
}

/// F et le double-clic : place l'œil pour que la sélection tienne dans la Vue. Rien sans sélection.
void frameSelection(const app::App& app, Editor& editor)
{
    // Jouer ne bouge pas la caméra de l'éditeur : un double-clic dans la hiérarchie, ouverte en
    // jeu, la cadrerait pour le retour en Édition.
    const flecs::entity selected = selectedIfAlive(app.world, editor.selected);
    if (editor.mode.current != Mode::Edit || !canFrameEntity(selected))
    {
        return;
    }
    const app::ScreenRect region = gestureRegionOf(app, editor);
    const float aspect = region.height > 0.0f ? region.width / region.height : 1.0f;
    editor.camera =
        framingOf(editor.camera, entityBoxOf(selected, app.models, app.modelInstance), aspect);
}

/// La scène est la fenêtre sous la souris, selon ce qu'ImGui a dit à l'image d'avant. Panneaux
/// ouverts, c'est la Vue (`View::hovered`). Fermés, la scène remplit la fenêtre : elle l'est sauf
/// sous une fenêtre d'ImGui (la barre de mode flottante, une fenêtre du programme), que
/// `WantCaptureMouse` désigne.
bool sceneHoveredIn(const app::App& app, const Editor& editor)
{
    return app.ui.panelsOpen ? editor.view.hovered : !ImGui::GetIO().WantCaptureMouse;
}

/// Les touches de la Vue valent (les outils, F, Ctrl+R) : la règle `viewActiveIn`, la même pour
/// les gestes (F) et les raccourcis (`shortcutsPressed`), sur ce qu'ImGui a dit de la Vue à la
/// dernière image qui l'a dessinée.
bool viewActiveOf(const app::App& app, const Editor& editor)
{
    return viewActiveIn(app.ui.panelsOpen, editor.view.focused, editor.view.hovered);
}

/// Le contexte des gestes de cette image : ce qu'ImGui a dit de la Vue à l'image d'avant.
GestureContext gestureContextOf(const app::App& app, const Editor& editor)
{
    // **Pendant la saisie d'un accord rien ne part** : ni geste (le vol, F), ni outil.
    const bool capturing = shortcutCaptureActive(editor);
    return {.region = gestureRegionOf(app, editor),
            .sceneHovered = sceneHoveredIn(app, editor) && !capturing,
            .viewActive = viewActiveOf(app, editor) && !capturing,
            .textInput = ImGui::GetIO().WantTextInput};
}

/// **Les gestes de l'image**, en Édition, dans `frame` : avant `ui`, pour que la capture de la
/// souris soit celle de la même image (`applyMouseCapture` suit `frame`). Hors Édition, rien ne
/// se tient : un vol commencé ne survit pas au changement de mode, ni à la croix (`quit.hpp`).
void frameGestures(app::App& app, Editor& editor)
{
    editor.presses.clear(); // les touches de l'Édition seulement, hors de la modale
    if (editor.mode.current != Mode::Edit)
    {
        editor.gestures = idleGestures(editor.gestures);
        // Jouer : le clic droit est au jeu (le bac à sable regarde à la souris), pas de menu.
        editor.view.menuAllowed = false;
        return;
    }
    if (!cameraGesturesAllowed(editor.quit))
    {
        // Sous la modale « Quitter ? » : un vol en cours s'arrête et rend la souris, qu'on puisse
        // cliquer ses boutons (Alt+F4 en vol).
        editor.gestures = idleGestures(editor.gestures);
        editor.view.menuAllowed = false;
        app.mouseCaptureWanted = false;
        app.mouseCaptureOverPanels = false;
        return;
    }
    // Avant `stepGestures` : `gestures.keys` est encore ce qui était tenu au début de l'image.
    editor.presses = keyPressesOf(app.rawFrameEvents, editor.gestures.keys);
    const GestureContext context = gestureContextOf(app, editor);
    const bool wasFlying = capturesMouse(editor.gestures);
    const GestureStep step = stepGestures(editor.gestures, app.rawFrameEvents, context,
                                          editor.shortcuts, app.frameSeconds);
    editor.gestures = step.state;
    editor.camera = applyGesture(editor.camera, step.frame, context.region.height);
    if (step.frameSelection)
    {
        frameSelection(app, editor);
    }
    const bool flying = capturesMouse(editor.gestures);
    if (flying && !wasFlying)
    {
        // Un champ de texte actif garderait Retour arrière et les flèches pendant le vol (la
        // capture ne retire à ImGui que la souris et le texte, `uiEventsOf`).
        ImGui::ClearActiveID();
        // Le curseur y revient à la fin du vol (`App::mouseReturnTo`).
        app.mouseReturnTo =
            platform::CursorPosition{editor.gestures.cursor.x, editor.gestures.cursor.y};
    }
    app.mouseCaptureWanted = flying;
    app.mouseCaptureOverPanels = flying;
    // Le menu du clic droit : pas à la fin d'un vol, ImGui y voit un clic (`View::menuAllowed`).
    editor.view.menuAllowed = !editor.gestures.flew && editor.gestures.rightPressInScene;
}

/// La disposition du clavier a changé depuis la dernière image (`App::keymapChanges`) : la position
/// du W n'est plus la lettre qu'elle était, les conflits sont refaits. Ceux qui sont nés ainsi vont
/// à la console ; aucun accord n'est rebranché en silence.
void recheckShortcutsAfterKeymap(const app::App& app, Editor& editor)
{
    if (std::exchange(editor.keymapSeen, app.keymapChanges) == app.keymapChanges)
    {
        return;
    }
    dropPendingAfterKeymapChange(editor.shortcutWindow);
    logShortcutConflicts(editor.shortcuts, app.keyAtPosition, "disposition du clavier");
}

/// Arrête le jeu : Stop rend la scène (`stopPlay`) et la console dit ce qu'il a trouvé. `false` si
/// Stop a été refusé : on reste en jeu, rien n'a changé. La sélection d'une entité née en jeu
/// disparaît avec elle (`selectedIfAlive`), les autres survivent avec leur identifiant.
bool stopEditorPlay(app::App& app, Editor& editor)
{
    if (!editor.play)
    {
        return true;
    }
    const StopReport report = stopPlay(playTargetOf(app), *editor.play);
    if (report.refused)
    {
        core::log("editor", core::LogLevel::Error, "{}",
                  ui::trf("Stop refusé : {}", report.refusal));
        return false;
    }
    logStop(report);
    editor.play.reset();
    editor.selected = selectedIfAlive(app.world, editor.selected);
    app.recomposeAfterUi = true; // la scène rendue se voit dans cette image même
    return true;
}

/// Ce que Jouer et Arrêter font avant de changer de mode ; `false` si le changement n'a pas lieu
/// (Play refusé : un composant qui ne se copie pas, Stop refusé : le monde est en mode différé).
/// La console dit pourquoi, et le mode reste celui d'avant. **Clés sur la session, non sur
/// `Mode::Play`** : quitter l'Édition ouvre une session, y revenir la ferme, quel que soit le mode
/// de jeu (Simuler, en M7.5, piece 6).
bool carryOutPlayTransition(app::App& app, Editor& editor, Mode to)
{
    if (!editor.play && to != Mode::Edit)
    {
        auto session = startPlay(playTargetOf(app));
        if (!session)
        {
            core::log("editor", core::LogLevel::Error, "{}",
                      ui::trf("Jouer refusé : {}", session.error().message));
            return false;
        }
        editor.play = std::move(*session);
        return true;
    }
    return !editor.play || to != Mode::Edit || stopEditorPlay(app, editor);
}

/// Passe de mode. Jouer met la Vue en avant : le jeu reçoit le clavier sans qu'il faille cliquer
/// dedans d'abord (la route *jeu* veut la Vue focalisée, `sceneFocusedIn`). **Jouer prend
/// l'instantané et Arrêter rend la scène avant** `enterMode`, hors du mode différé : `frame` et la
/// fin de `ui` y sont (ADR-0037, décision 3).
void changeMode(app::App& app, Editor& editor, Mode mode)
{
    const Mode from = editor.mode.current;
    if (!carryOutPlayTransition(app, editor, mode))
    {
        return;
    }
    // Une saisie d'accord ne survit pas au changement de mode : hors de l'Édition les touches ne
    // lui parviennent plus (`frameGestures`), et plus rien ne pourrait la finir.
    abandonCapture(editor.shortcutWindow);
    enterMode(app, editor.mode, mode);
    switchRealTime(from, mode, app.realTime, editor.realTimeEdit);
    editor.view.focusRequested = mode == Mode::Play;
    // Le jeu voit par sa caméra ; l'édition, par la sienne.
    if (mode == Mode::Edit)
    {
        imposeCamera(app, editor, true); // le jeu a pu tout déplacer
    }
    else
    {
        app.cameraOverride.reset();
    }
}

/// La barre de menus, la barre d'outils et la barre d'état, et la fenêtre Aide : ce qu'elles
/// atteignent (`MenuRequests`, la barre d'outils) revient, et le mode que le bouton propose va à
/// `requested` si la Vue n'en a pas déjà demandé un.
std::vector<Action> drawBars(app::App& app, Editor& editor, std::optional<Mode>& requested)
{
    const auto windows = windowEntriesOf(editor, app);
    const MenuBarInputs inputs{
        .table = editor.shortcuts, .keyAtPosition = app.keyAtPosition, .windows = windows};
    MenuRequests menus = drawMenuBar(inputs, editor.menu, editor.bar, editor.help);
    const ToolbarResult toolbar =
        drawToolbar(editor.mode.current, editor.camera.flySpeed, app.realTime, editor.toolbar);
    editor.status = statusOf(*editor.log->ring);
    drawStatusBar(editor.status);
    drawHelp(editor.help, inputs);
    editor.shortcutWindow.open = editor.shortcutWindow.open || menus.openShortcuts;
    // Les touches de l'image sont celles que la saisie lit ; elle les prend avant les raccourcis.
    if (drawShortcutWindow(editor.shortcutWindow, editor.shortcuts, app.keyAtPosition,
                           editor.presses, editor.mode.current == Mode::Edit))
    {
        editor.savePreferencesNext = true; // enregistré tout de suite, à la fin de `ui`
    }
    forgetWrittenLines(editor.shortcutWindow, editor.shortcutLines);
    if (menus.quit)
    {
        askToQuit(editor.quit);
    }
    editor.layoutResetRequested = editor.layoutResetRequested || menus.resetLayout;
    if (toolbar.flySpeed)
    {
        editor.camera.flySpeed = *toolbar.flySpeed;
    }
    requested = toolbar.mode;
    menus.actions.insert(menus.actions.end(), toolbar.actions.begin(), toolbar.actions.end());
    return std::move(menus.actions);
}

/// Atteint les actions des menus et de la barre d'outils, puis celles des raccourcis de l'image
/// (`shortcutsPressed`) : un seul chemin, `runAction`, que le clic et la touche partagent. Les
/// touches sont celles des événements bruts (`Editor::presses`, posées par `frameGestures`) ; la
/// Vue est « active » selon `viewActiveOf`, et le vol est celui des gestes (`inFlight`), qui
/// commande aussi la capture de la souris. `capturing` : la saisie d'un accord **au début de
/// l'image** (voir `ui`), non ce qu'elle est devenue depuis.
void runReachedActions(app::App& app, Editor& editor, std::vector<Action> reached, bool capturing)
{
    editor.lastShortcuts =
        shortcutsPressed({.table = editor.shortcuts,
                          .presses = editor.presses,
                          .editing = editor.mode.current == Mode::Edit,
                          .globalAllowed = globalChordsAllowed(editor.quit) && !capturing,
                          .viewActive = viewActiveOf(app, editor),
                          .flight = inFlight(editor.gestures),
                          .textInput = ImGui::GetIO().WantTextInput});
    reached.insert(reached.end(), editor.lastShortcuts.begin(), editor.lastShortcuts.end());
    for (const Action action : reached)
    {
        // **« Vue de jeu » ne bascule que si `runAction` l'a faite** (comptée) : une action refusée
        // (grisée, annoncée à la console) ne change rien.
        const int before = editor.actions.performed.at(indexOf(action));
        if (const auto next = runAction(action, editor.actions, app.realTime, editor.mode.current))
        {
            changeMode(app, editor, *next);
        }
        if (editor.actions.performed.at(indexOf(action)) > before)
        {
            applyShowAction(editor.view.show, action);
        }
    }
}

/// **La grille** : sa passe pour la cible de la scène, créée une fois, et son étape dans l'étape
/// Transparent, après le ciel et les opaques du programme (le test de profondeur la cache derrière
/// eux ; elle se mêle à ce qui est dessous par son opacité). Une passe qui ne se crée pas est dite
/// à la console, et la grille n'apparaît pas.
void addGridStage(app::App& app, Editor& editor)
{
    auto pass = render::createDebugLinesPass(*app.gpu.nvrhi, render::sceneTargetInfo());
    if (!pass)
    {
        core::log("editor", core::LogLevel::Error, "pas de grille : {}", pass.error().message);
        return;
    }
    editor.grid.pass = std::move(*pass);
    // **L'éditeur par pointeur, non par `shared_ptr`** : les étapes vivent dans le renderer,
    // détruit après le monde, et un `shared_ptr` ferait survivre l'éditeur (et sa requête flecs) au
    // monde qu'elle regarde. Les points d'accroche (`FrameHooks`) le possèdent et partent les
    // premiers ; la boucle ne rend plus rien à ce moment-là, aucune étape n'est appelée.
    render::addStageFunction(app.renderer.stages, render::RenderStage::Transparent, "grille",
                             [&app, &editor](const render::StageContext& context)
                             {
                                 drawGrid(editor.grid, context,
                                          gridVisibleIn(editor.view.show, app.editorHelpersVisible),
                                          app.settings.tonemap.exposure);
                             });
}

} // namespace

std::array<WindowEntry, EditorWindowCount> windowEntriesOf(Editor& editor, app::App& app)
{
    return {{{HierarchyWindow, &editor.windows.hierarchy},
             {InspectorWindow, &editor.windows.inspector},
             {ConsoleWindow, &editor.windows.console},
             {ViewWindow, &editor.view.open},
             {"Image", &app.ui.windows.image},
             {"Passes", &app.ui.windows.passes},
             {"Scène", &app.ui.windows.scene}}};
}

std::vector<char*> takeEditorOptions(std::span<char* const> arguments, EditorOptions& options)
{
    std::vector<char*> rest;
    if (!arguments.empty())
    {
        rest.push_back(arguments.front()); // le nom du programme
    }
    for (std::size_t i = 1; i < arguments.size(); i += 2)
    {
        const bool hasValue = i + 1 < arguments.size();
        if (hasValue && std::string_view{arguments[i]} == "--select")
        {
            options.select = arguments[i + 1];
            continue;
        }
        rest.push_back(arguments[i]);
        if (hasValue)
        {
            rest.push_back(arguments[i + 1]);
        }
    }
    return rest;
}

flecs::entity selectedIfAlive(const flecs::world& world, flecs::entity_t selected)
{
    return selected != 0 && world.is_alive(selected) ? world.entity(selected) : flecs::entity{};
}

core::Result<void> requireEditorBackend(nvrhi::GraphicsAPI api)
{
    if (api == nvrhi::GraphicsAPI::WEBGPU)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "--gpu webgpu : l'éditeur n'a pas de WebGPU (la page web n'a pas "
                               "d'éditeur, et la Vue de l'éditeur natif n'a pas été éprouvée sur "
                               "Dawn) ; --gpu vulkan ou d3d12");
    }
    return {};
}

app::StartFunction withEditor(app::StartFunction start, EditorOptions options)
{
    // Captée ici, avant `runApp` : ce que la fenêtre et le device ont écrit en s'ouvrant est dans
    // la console.
    auto capture = std::make_shared<core::LogCapture>(core::captureLog(ConsoleCapacity));
    return [start = std::move(start), options = std::move(options),
            capture = std::move(capture)](app::App& app) -> core::Result<app::FrameHooks>
    {
        // Avant le programme : une police absente l'arrête tout de suite, et rien ne dessine avec
        // la police d'ImGui.
        const auto fonts = loadEditorFonts(app, options);
        if (!fonts)
        {
            return std::unexpected(fonts.error());
        }
        // L'allure de l'éditeur, avant la première image : ImGui lit son style à chaque widget. Le
        // sandbox seul et *Rando* gardent celui d'ImGui. `App` en est le propriétaire : il le
        // refait à chaque changement d'échelle de l'écran (`setUiStyle`).
        app::setUiStyle(app, ui::applyEditorStyle);
        auto hooks = start(app);
        if (!hooks)
        {
            return hooks;
        }
        // Avant la première image de panneaux : la disposition se construit à ce moment-là.
        app.ui.editorLayout = true;
        const auto editor = std::make_shared<Editor>(
            Editor{.selected = 0,
                   .hierarchy = createHierarchy(app.world),
                   .inspector = createInspector(app.world, &app.registry),
                   .mode = {},
                   .play = {},
                   .log = capture,
                   .console = {},
                   .windows = {},
                   .view = {},
                   .menu = {},
                   .quit = {},
                   .camera = {},
                   .gestures = {},
                   .presses = {},
                   .shortcuts = defaultShortcuts(),
                   .everything = app.world.query<const scene::WorldTransform>(),
                   .sceneBounds = {},
                   .actions = {},
                   .bar = {},
                   .help = {},
                   .toolbar = {},
                   .status = {},
                   .realTimeEdit = true,
                   .lastShortcuts = {},
                   .preferencesFile = {},
                   .shortcutLines = {},
                   .layoutResetRequested = false,
                   .savePreferencesNext = false,
                   .shortcutWindow = {},
                   .keymapSeen = 0,
                   .grid = {}});
        editor->console.font = fonts->console;
        if (options.select)
        {
            const flecs::entity chosen = app.world.lookup(options.select->c_str());
            if (!chosen)
            {
                return core::makeError(
                    core::ErrorCode::InvalidData,
                    std::format("--select : aucune entité « {} »", *options.select));
            }
            editor->selected = chosen;
            revealInHierarchy(editor->hierarchy, chosen);
        }
        // L'éditeur s'ouvre en Édition : rien ne tourne, le jeu ne reçoit rien (ADR-0036).
        enterMode(app, editor->mode, Mode::Edit);
        // La caméra de l'éditeur part de celle du jeu s'il y en a une (décision 6), et s'impose
        // avant la première image : `startApp` ne consulte alors pas les entités.
        scene::composeWorldTransforms(app.world);
        if (const auto game = app::renderCameraOf(app.cameras))
        {
            editor->camera = editorCameraFrom(*game);
        }
        // Avant la première image de panneaux, et après la caméra du jeu, dont la vitesse n'est pas
        // celle qu'on a gardée : la disposition relue, ou la défaut qui se construira.
        startPreferences(app, *editor, options.preferencesDirectory);
        addGridStage(app, *editor);
        editor->keymapSeen = app.keymapChanges;
        imposeCamera(app, *editor, true);
        hooks->frame = [frame = std::move(hooks->frame), editor](app::App& app)
        {
            if (std::exchange(editor->layoutResetRequested, false))
            {
                resetLayout(app, *editor);
                editor->savePreferencesNext = true;
            }
            // Échap ramène à l'Édition ; le `frame` du programme ne tourne qu'en jeu.
            if (const auto requested = modeRequested(
                    editor->mode.current,
                    {.stop = stopPressed(editor->mode, app.input.raw, editor->shortcuts)}))
            {
                changeMode(app, *editor, *requested);
            }
            frameGestures(app, *editor);
            if (programFrameRunsIn(editor->mode.current) && frame)
            {
                frame(app);
            }
        };
        hooks->ui = [ui = std::move(hooks->ui), editor, afterUi = options.afterUi](app::App& app)
        {
            if (ui)
            {
                ui(app);
            }
            std::optional<Mode> requested;
            // **La saisie telle qu'elle commence l'image** : la touche qui la finit (l'accord, ou
            // Échap) ne doit pas partir aussi comme raccourci, et `drawBars` l'a déjà terminée
            // quand les raccourcis sont lus.
            const bool capturing = shortcutCaptureActive(*editor);
            recheckShortcutsAfterKeymap(app, *editor);
            // L'écriture de cette image seulement : `inspector.wrote` survit, panneaux fermés.
            bool wroteWorld = false;
            // Ce que les menus, les barres et les raccourcis atteignent à cette image.
            std::vector<Action> reached;
            if (app.ui.panelsOpen)
            {
                // Les barres d'abord : elles retirent leur hauteur à l'espace d'ancrage.
                reached = drawBars(app, *editor, requested);
                // Avant la hiérarchie : dessinée après elle, la console prendrait l'onglet visible
                // de son nœud (ImGui montre le dernier venu, `DockNodeUpdateTabBar` dans
                // imgui.cpp) ; elle ne partage plus de nœud avec elle, mais garde l'ordre. Elle
                // n'écrit pas dans le monde : hors du `defer`.
                drawConsole(editor->console, *editor->log->ring, app.ui.dock.bottom,
                            &editor->windows.console);
                // Les écritures de l'inspecteur et leurs observateurs passent après le parcours.
                app.world.defer_begin();
                drawHierarchy(app.world, editor->hierarchy, editor->selected, app.ui.dock.left,
                              &editor->windows.hierarchy);
                drawInspector(app.world, editor->inspector, editor->selected, app.ui.dock.inspector,
                              &editor->windows.inspector);
                app.world.defer_end();
                wroteWorld = editor->inspector.wrote;
                if (wroteWorld)
                {
                    app.recomposeAfterUi = true; // la valeur tapée se voit dans l'image même
                }
                // La Vue en dernier : elle pose `App::sceneRect`, et sa barre de mode en calque.
                editor->view.canFrame =
                    canFrameEntity(selectedIfAlive(app.world, editor->selected));
                editor->view.frameShortcut = shortcutTextOf(*editor, app, Action::FrameSelection);
                editor->view.gameViewShortcut = shortcutTextOf(*editor, app, Action::GameView);
                editor->view.modeHint =
                    modeHintOf(editor->mode.current, editor->shortcuts, app.keyAtPosition);
                if (const auto fromView = drawView(editor->view, app, editor->mode.current))
                {
                    requested = fromView; // le bouton de la Vue, avant celui de la barre d'outils
                }
                if (editor->view.frameRequested || editor->hierarchy.frameRequested)
                {
                    frameSelection(app, *editor);
                }
                if (editor->view.gameViewRequested)
                {
                    reached.push_back(Action::GameView); // le menu, comme la touche G
                }
            }
            // Avant les raccourcis et la route, qui lisent si elle est ouverte.
            if (drawQuitModal(editor->quit) == QuitAnswer::Quit)
            {
                // Quitter en jeu rend la scène d'abord (ADR-0037) : une exécution scriptée qui
                // s'arrête avant son terme ne passe pas par `finish`.
                if (editor->play)
                {
                    changeMode(app, *editor, Mode::Edit);
                }
                app.quitRequested = true;
            }
            runReachedActions(app, *editor, std::move(reached), capturing);
            // La barre de mode : en calque de la Vue panneaux ouverts, bandeau flottant sinon (le
            // bandeau de « Jouer » et le rappel de la touche valent panneaux fermés aussi).
            // `modeRequested` décide, avec Alt+P, qui ne joue pas sous la modale.
            if (!app.ui.panelsOpen)
            {
                drawModeBar(editor->mode.current, app.sceneRect,
                            modeHintOf(editor->mode.current, editor->shortcuts, app.keyAtPosition));
            }
            if (const auto next = modeRequested(
                    editor->mode.current,
                    {.toolbar = requested,
                     .playShortcut = globalChordsAllowed(editor->quit) && !capturing &&
                                     playShortcutPressed(editor->shortcuts)}))
            {
                changeMode(app, *editor, *next);
            }
            if (editor->mode.current == Mode::Edit)
            {
                // Après les gestes, le cadrage et les écritures de l'image.
                imposeCamera(app, *editor, wroteWorld);
            }
            // Les aides se voient en Édition hors vue de jeu : la grille et le contour du
            // programme.
            app.editorHelpersVisible =
                helpersVisibleIn(editor->mode.current, editor->view.show.gameView);
            // La route de l'image suivante : le focus ne se sait que maintenant (ADR-0036, 4).
            app.inputRoute = inputRouteWhileQuit(
                editor->quit,
                inputRouteFor(editor->mode.current,
                              sceneFocusedIn(app.ui.panelsOpen, viewHasFocus(editor->view),
                                             !sceneHasFocus(), editor->view.barHovered)));
            app.ui.panelsKeyBlocked = shortcutCaptureActive(*editor); // F1 est refusée, pas obéie
            if (std::exchange(editor->savePreferencesNext, false))
            {
                savePreferencesNow(app, *editor);
            }
            if (afterUi)
            {
                afterUi(*editor);
            }
        };
        // La croix : on demande d'abord (la modale s'ouvre dans `ui`). **Remplace** le
        // `closeRequested` du programme enveloppé, s'il en avait un : une fermeture que
        // l'éditeur intercepte n'est pas à lui d'arbitrer, et `app` n'appelle qu'un crochet.
        hooks->closeRequested = [editor](app::App&)
        {
            askToQuit(editor->quit);
            return true;
        };
        hooks->finish = [finish = std::move(hooks->finish), editor](app::App& app)
        {
            savePreferencesNow(app, *editor);
            logEditor(app.world, *editor);
            logMode(app, *editor);
            logView(editor->view);
            // Le programme rend compte de ce qu'il a vu en jeu (le sandbox dit ses corps) AVANT
            // que Stop ne rende la scène, puis quitter ou finir une exécution scriptée en jeu passe
            // par Stop (ADR-0037). Une capture (`--capture`) est prise après ce point : elle voit
            // la scène rendue.
            const bool programOk = !finish || finish(app);
            if (editor->play)
            {
                changeMode(app, *editor, Mode::Edit);
            }
            return programOk;
        };
        return hooks;
    };
}

} // namespace levain::editor
