#include "levain/editor/structure.hpp"

#include <format>

#include "levain/scene/components.hpp"
#include "levain/scene/entity_name.hpp"
#include "levain/scene/snapshot.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Le parent d'une entité de la scène (`flecs::Parent`), 0 pour une racine.
flecs::entity_t parentOf(const flecs::world& world, flecs::entity_t entity)
{
    const auto* parent = world.entity(entity).try_get<flecs::Parent>();
    return parent != nullptr ? parent->value : 0;
}

/// Un nœud que son générateur a créé (décision 10). Le composant est enregistré d'abord : `has<T>`
/// d'un type inconnu le crée, et ce serait une entité de plus au milieu d'une opération.
bool isGeneratedNode(const flecs::world& world, flecs::entity_t entity)
{
    world.component<scene::Generated>();
    return world.entity(entity).has<scene::Generated>();
}

} // namespace

std::optional<std::string> renameRefusalOf(const flecs::world& world, flecs::entity_t entity,
                                           std::string_view wanted)
{
    if (!ecs_is_alive(world, entity) || !scene::isSceneEntity(world, entity))
    {
        return ui::tr("l'entité n'est pas une entité de la scène");
    }
    if (isGeneratedNode(world, entity))
    {
        return ui::tr("un nœud généré ne se renomme pas : son générateur le nomme");
    }
    const flecs::entity_t parent = parentOf(world, entity);
    // Un nom égal au sien n'est pas un changement : ni refus ni commande.
    const char* current = ecs_get_name(world, entity);
    if (current != nullptr && wanted == current)
    {
        return std::nullopt;
    }
    // La même règle que `uniqueNameAmong` (le nom est libre parmi les frères, `self` écarté) : un
    // nom qu'elle changerait est un nom pris.
    auto unique = scene::uniqueNameAmong(world, parent, wanted, entity);
    if (!unique)
    {
        const auto refusal = scene::refusalOfName(wanted, parent == 0);
        if (!refusal)
        {
            return unique.error().message;
        }
        // La clé du catalogue est une chaîne terminée par un zéro : la copie vit le temps de
        // l'appel.
        const std::string key{scene::reasonOf(*refusal)};
        return std::string{ui::tr(key.c_str())};
    }
    if (*unique != wanted)
    {
        return ui::trf("un frère s'appelle déjà « {} »", wanted);
    }
    return std::nullopt;
}

void writeName(flecs::world& world, flecs::entity_t entity, const std::optional<std::string>& name)
{
    // https://www.flecs.dev/flecs/md_docs_2EntitiesComponents.html, « Names » : `ecs_set_name`
    // met l'index de noms du parent à jour, `lookup` retrouve l'entité sous son nouveau nom.
    ecs_set_name(world, entity, name ? name->c_str() : nullptr);
}

core::Result<void> stepRename(flecs::world& world, const RenameCommand& command, bool redo)
{
    const auto refuse = [](std::string why)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{}, le monde n'est pas touché", why));
    };
    if (ecs_is_deferred(world))
    {
        return refuse("appelée en mode différé");
    }
    if (!ecs_is_alive(world, command.entity))
    {
        return refuse("l'entité n'existe plus");
    }
    const std::optional<std::string>& target =
        redo ? std::optional<std::string>{command.after} : command.before;
    if (target)
    {
        if (const auto refusal = renameRefusalOf(world, command.entity, *target))
        {
            return refuse(std::format("le nom « {} » ne passe plus : {}", *target, *refusal));
        }
    }
    writeName(world, command.entity, target);
    return {};
}

} // namespace levain::editor
