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
#include "levain/editor/scene_bounds.hpp"
#include "levain/scene/scene.hpp"
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

/// L'accord affiché dans l'entrée « Cadrer la sélection » du menu, selon le clavier d'aujourd'hui.
std::string frameShortcutOf(const Editor& editor, const app::App& app)
{
    const Binding* binding = bindingOf(editor.shortcuts, Action::FrameSelection);
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

/// Le contexte des gestes de cette image : ce qu'ImGui a dit de la Vue à l'image d'avant.
GestureContext gestureContextOf(const app::App& app, const Editor& editor)
{
    return {.region = gestureRegionOf(app, editor),
            .sceneHovered = sceneHoveredIn(app, editor),
            .sceneFocused = app.ui.panelsOpen && editor.view.focused,
            .textInput = ImGui::GetIO().WantTextInput};
}

/// **Les gestes de l'image**, en Édition, dans `frame` : avant `ui`, pour que la capture de la
/// souris soit celle de la même image (`applyMouseCapture` suit `frame`). Hors Édition, rien ne
/// se tient : un vol commencé ne survit pas au changement de mode, ni à la croix (`quit.hpp`).
void frameGestures(app::App& app, Editor& editor)
{
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

/// Passe de mode. Jouer met la Vue en avant : le jeu reçoit le clavier sans qu'il faille cliquer
/// dedans d'abord (la route *jeu* veut la Vue focalisée, `sceneFocusedIn`).
void changeMode(app::App& app, Editor& editor, Mode mode)
{
    enterMode(app, editor.mode, mode);
    editor.view.focusRequested = mode == Mode::PlayWithoutReturn;
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
                   .log = capture,
                   .console = {},
                   .windows = {},
                   .view = {},
                   .menu = {},
                   .quit = {},
                   .camera = {},
                   .gestures = {},
                   .shortcuts = defaultShortcuts(),
                   .everything = app.world.query<const scene::WorldTransform>(),
                   .sceneBounds = {}});
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
        imposeCamera(app, *editor, true);
        hooks->frame = [frame = std::move(hooks->frame), editor](app::App& app)
        {
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
            // L'écriture de cette image seulement : `inspector.wrote` survit, panneaux fermés.
            bool wroteWorld = false;
            if (app.ui.panelsOpen)
            {
                // Le menu d'abord : la barre retire sa hauteur à l'espace d'ancrage.
                drawWindowMenu(windowEntriesOf(*editor, app), editor->menu);
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
                editor->view.frameShortcut = frameShortcutOf(*editor, app);
                requested = drawView(editor->view, app, editor->mode.current);
                if (editor->view.frameRequested || editor->hierarchy.frameRequested)
                {
                    frameSelection(app, *editor);
                }
            }
            // Avant les raccourcis et la route, qui lisent si elle est ouverte.
            if (drawQuitModal(editor->quit) == QuitAnswer::Quit)
            {
                app.quitRequested = true;
            }
            // La barre de mode : en calque de la Vue panneaux ouverts, bandeau flottant sinon (le
            // bandeau de « Jouer » et le rappel de la touche valent panneaux fermés aussi).
            // `modeRequested` décide, avec Alt+P, qui ne joue pas sous la modale.
            if (!app.ui.panelsOpen)
            {
                drawModeBar(editor->mode.current, app.sceneRect);
            }
            if (const auto next = modeRequested(
                    editor->mode.current, {.toolbar = requested,
                                           .playShortcut = globalChordsAllowed(editor->quit) &&
                                                           playShortcutPressed(editor->shortcuts)}))
            {
                changeMode(app, *editor, *next);
            }
            if (editor->mode.current == Mode::Edit)
            {
                // Après les gestes, le cadrage et les écritures de l'image.
                imposeCamera(app, *editor, wroteWorld);
            }
            // La route de l'image suivante : le focus ne se sait que maintenant (ADR-0036, 4).
            app.inputRoute = inputRouteWhileQuit(
                editor->quit,
                inputRouteFor(editor->mode.current,
                              sceneFocusedIn(app.ui.panelsOpen, viewHasFocus(editor->view),
                                             !sceneHasFocus(), editor->view.barHovered)));
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
            logEditor(app.world, *editor);
            logMode(app, *editor);
            logView(editor->view);
            return !finish || finish(app);
        };
        return hooks;
    };
}

} // namespace levain::editor
