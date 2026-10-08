#include "levain/editor/editor.hpp"

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "levain/core/error.hpp"
#include "levain/core/log.hpp"
#include "levain/scene/components.hpp"

namespace levain::editor
{

namespace
{

/// Le bilan de l'éditeur, une ligne que la CI lit : elle échoue si la ligne manque, si elle compte
/// zéro entité, ou si la sélection n'est pas celle de `--select`.
void logEditor(const flecs::world& world, const Editor& editor)
{
    const flecs::entity selected = selectedIfAlive(world, editor.selected);
    const std::string name = selected ? std::string{selected.path("::", "").c_str()} : "aucune";
    core::log("editor", core::LogLevel::Info, "éditeur : {} entités ; sélection : {}",
              world.count<scene::Transform>(), name);
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
        const auto editor = std::make_shared<Editor>();
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
        }
        hooks->finish = [finish = std::move(hooks->finish), editor](app::App& app)
        {
            logEditor(app.world, *editor);
            return !finish || finish(app);
        };
        return hooks;
    };
}

} // namespace levain::editor
