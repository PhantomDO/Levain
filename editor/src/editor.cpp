#include "levain/editor/editor.hpp"

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "levain/core/error.hpp"
#include "levain/core/log.hpp"

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
        const auto editor = std::make_shared<Editor>(
            Editor{.selected = 0,
                   .hierarchy = createHierarchy(app.world),
                   .inspector = createInspector(app.world, &app.registry)});
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
            }
        };
        hooks->finish = [finish = std::move(hooks->finish), editor](app::App& app)
        {
            logEditor(app.world, *editor);
            return !finish || finish(app);
        };
        return hooks;
    };
}

} // namespace levain::editor
