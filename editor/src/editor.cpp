#include "levain/editor/editor.hpp"

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "levain/core/error.hpp"
#include "levain/core/log.hpp"
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

} // namespace

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

app::StartFunction withEditor(app::StartFunction start, EditorOptions options)
{
    return [start = std::move(start),
            options = std::move(options)](app::App& app) -> core::Result<app::FrameHooks>
    {
        auto hooks = start(app);
        if (!hooks)
        {
            return hooks;
        }
        const auto editor =
            std::make_shared<Editor>(Editor{.selected = 0,
                                            .hierarchy = createHierarchy(app.world),
                                            .inspector = createInspector(app.world, &app.registry),
                                            .mode = {}});
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
        hooks->frame = [frame = std::move(hooks->frame), editor](app::App& app)
        {
            // Échap ramène à l'Édition ; le `frame` du programme ne tourne qu'en jeu.
            if (const auto requested = modeRequested(
                    editor->mode.current, {.stop = stopPressed(editor->mode, app.input.raw)}))
            {
                enterMode(app, editor->mode, *requested);
            }
            if (programFrameRunsIn(editor->mode.current) && frame)
            {
                frame(app);
            }
        };
        hooks->ui = [ui = std::move(hooks->ui), editor](app::App& app)
        {
            if (ui)
            {
                ui(app);
            }
            if (app.ui.panelsOpen)
            {
                // Les écritures de l'inspecteur et leurs observateurs passent après le parcours.
                app.world.defer_begin();
                drawHierarchy(app.world, editor->hierarchy, editor->selected, app.ui.dock.left);
                drawInspector(app.world, editor->inspector, editor->selected,
                              app.ui.dock.inspector);
                app.world.defer_end();
                if (editor->inspector.wrote)
                {
                    app.recomposeAfterUi = true; // la valeur tapée se voit dans l'image même
                }
            }
            // Le bouton, Alt+P : `modeRequested` décide, la barre est dessinée panneaux fermés
            // aussi.
            if (const auto requested = modeRequested(
                    editor->mode.current,
                    {.toolbar = drawModeBar(editor->mode.current, app.sceneRect, app.ui.panelsOpen),
                     .playShortcut = playShortcutPressed()}))
            {
                enterMode(app, editor->mode, *requested);
            }
            // La route de l'image suivante : le focus ne se sait que maintenant (ADR-0036, 4).
            app.inputRoute = inputRouteFor(editor->mode.current, sceneHasFocus());
        };
        hooks->finish = [finish = std::move(hooks->finish), editor](app::App& app)
        {
            logEditor(app.world, *editor);
            logMode(app, *editor);
            return !finish || finish(app);
        };
        return hooks;
    };
}

} // namespace levain::editor
