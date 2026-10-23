#include "levain/editor/structure.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "levain/core/log.hpp"
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

std::vector<assets::AssetId> assetsDesignatedBy(const flecs::world& world,
                                                flecs::entity_t component, const void* value)
{
    if (component == world.id<assets::MeshRef>())
    {
        return {static_cast<const assets::MeshRef*>(value)->mesh.asset};
    }
    return {};
}

std::vector<assets::AssetId> assetsOfSubtree(const flecs::world& world,
                                             const scene::SubtreeRecord& record)
{
    std::vector<assets::AssetId> found;
    for (const scene::EntityRecord& entity : record.entities)
    {
        for (const scene::ComponentValue& component : entity.values)
        {
            const auto designated = assetsDesignatedBy(world, component.id, component.value.data());
            found.insert(found.end(), designated.begin(), designated.end());
        }
    }
    return found;
}

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

std::optional<std::string> createRefusalOf(const flecs::world& world, flecs::entity_t parent)
{
    if (parent == 0)
    {
        return std::nullopt;
    }
    if (!ecs_is_alive(world, parent) || !scene::isSceneEntity(world, parent))
    {
        return ui::tr("le parent n'est pas une entité de la scène");
    }
    if (isGeneratedNode(world, parent))
    {
        return ui::tr("un nœud généré ne reçoit pas d'enfant fait à la main : "
                      "son générateur peut le refaire");
    }
    return std::nullopt;
}

std::optional<std::string> plainDeleteRefusalOf(const flecs::world& world, flecs::entity_t entity)
{
    if (!ecs_is_alive(world, entity) || !scene::isSceneEntity(world, entity))
    {
        return ui::tr("l'entité n'est pas une entité de la scène");
    }
    if (isGeneratedNode(world, entity))
    {
        return ui::tr("un nœud généré ne se supprime pas : on supprime son parent");
    }
    const flecs::entity_t parent = parentOf(world, entity);
    if (parent != 0 && isGeneratedNode(world, parent))
    {
        return ui::tr("l'entité est sous un nœud généré : son générateur peut le refaire");
    }
    return std::nullopt;
}

flecs::entity_t makeEntity(flecs::world& world, flecs::entity_t parent, std::string_view name)
{
    const std::string owned{name}; // flecs lit une chaîne C
    const flecs::entity entity =
        parent != 0 ? world.entity(flecs::Parent{world.entity(parent)}, owned.c_str())
                    : world.entity(owned.c_str());
    entity.set(scene::Transform{});
    return entity.id();
}

void writeName(flecs::world& world, flecs::entity_t entity, const std::optional<std::string>& name)
{
    // https://www.flecs.dev/flecs/md_docs_2EntitiesComponents.html, « Names » : `ecs_set_name`
    // met l'index de noms du parent à jour, `lookup` retrouve l'entité sous son nouveau nom.
    ecs_set_name(world, entity, name ? name->c_str() : nullptr);
}

std::size_t warnAboutLostChildren(const scene::Tombstones& tombstones, std::string_view verb)
{
    for (const scene::LostChild& lost : tombstones.lost)
    {
        core::log("editor", core::LogLevel::Warning, "{}",
                  ui::trf("{} : « {} » (le rappel d'un volume) ne revient pas avec l'entité", verb,
                          lost.name));
    }
    return tombstones.lost.size();
}

core::Result<StepEffect> stepSubtree(flecs::world& world, SubtreeCommand& command, bool redo)
{
    const flecs::entity_t root = command.record.entities.front().id;
    // Annuler une suppression ou refaire une création : l'entité revient.
    const bool comesBack = (command.op == SubtreeOp::Delete) != redo;
    const std::string verb = ui::tr(redo ? "Rétablir" : "Annuler");
    if (comesBack)
    {
        // Le contrat de la pile (tombstone.hpp) : les deux gardes d'abord, qui ne touchent à rien ;
        // le réveil et la restauration ensuite, sans rien entre eux.
        if (auto held = scene::checkTombstonesHold(world, command.tombstones); !held)
        {
            return std::unexpected(held.error());
        }
        if (auto restorable = scene::checkSubtreeRestorable(world, command.record); !restorable)
        {
            return std::unexpected(restorable.error());
        }
        if (auto revived = scene::reviveFromTombstones(world, command.tombstones); !revived)
        {
            return std::unexpected(revived.error());
        }
        if (auto restored = scene::restoreSubtree(world, command.record); !restored)
        {
            return std::unexpected(restored.error());
        }
        warnAboutLostChildren(command.tombstones, verb);
        // Les pierres sont rendues au réveil : la commande n'en retient plus. Sinon, en sortant de
        // la pile, elle détruirait celles d'une suppression plus récente de la même entité (mêmes
        // identifiants, tombstone.hpp). `lost` reste : l'avertissement du prochain tour s'en sert.
        command.tombstones.reserved.clear();
        return StepEffect{.revived = root};
    }
    if (command.op == SubtreeOp::Create)
    {
        // Le relevé de la création date de la création : un enfant ajouté depuis, hors de la pile,
        // partirait avec l'annulation mais reviendrait vide au rétablissement. On relit le
        // sous-arbre tel qu'il est avant de l'enlever (N2). Un refus laisse le monde intact.
        auto current = scene::readSubtree(world, root);
        if (!current)
        {
            return std::unexpected(current.error());
        }
        command.record = std::move(*current);
    }
    auto stones = scene::removeKeepingIndex(world, root);
    if (!stones)
    {
        return std::unexpected(stones.error());
    }
    warnAboutLostChildren(*stones, verb);
    if (!stones->notUndoable.empty())
    {
        // Détruite pour de bon, sans pierre : la commande ne saurait plus la rendre. La raison
        // reste dans la commande : `stepOnce` y reconnaît une entité déjà partie.
        command.tombstones = {};
        command.tombstones.notUndoable = stones->notUndoable;
        return core::makeError(
            core::ErrorCode::InvalidData,
            std::format("l'entité est enlevée mais ne reviendra plus : {}", stones->notUndoable));
    }
    command.tombstones = std::move(*stones);
    return StepEffect{};
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
