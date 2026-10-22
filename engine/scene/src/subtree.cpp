#include "levain/scene/subtree.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "entity_record.hpp"

namespace levain::scene
{

namespace
{

core::Result<void> refuseNotARoot(const flecs::world& world, flecs::entity_t root)
{
    const auto refuse = [&](const char* why)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Supprimer : l'entité {} {}, le monde n'est pas touché",
                                           entityIndexOf(root), why));
    };
    if (!ecs_is_alive(world, root))
    {
        return refuse("n'est pas vivante");
    }
    if (!isSceneEntity(world, root))
    {
        return refuse("n'est pas une entité de la scène (composant, module, pierre tombale…)");
    }
    if (isGenerated(world, root))
    {
        return refuse("est un nœud généré : on supprime son parent");
    }
    return {};
}

/// Les enfants d'un enregistrement sans les nœuds générés : ils ne reviennent pas par lui.
std::vector<flecs::entity_t> withoutGenerated(const flecs::world& world,
                                              std::vector<flecs::entity_t> children)
{
    std::erase_if(children, [&](flecs::entity_t child) { return isGenerated(world, child); });
    return children;
}

} // namespace

core::Result<SubtreeRecord> readSubtree(const flecs::world& world, flecs::entity_t root)
{
    if (auto deferred = refuseDeferred(world, "Supprimer"); !deferred)
    {
        return std::unexpected(deferred.error());
    }
    // Avant la lecture : `isGenerated` enregistrerait le composant, ce qui crée une entité.
    world.component<Generated>();
    if (auto refused = refuseNotARoot(world, root); !refused)
    {
        return std::unexpected(refused.error());
    }
    // La même règle que les pierres : un enregistrement par entité qui aura une pierre.
    const SubtreeMembers members = subtreeMembersOf(world, root);
    if (auto handMade = refuseHandMadeUnderGenerated(world, members, "Supprimer"); !handMade)
    {
        return std::unexpected(handMade.error());
    }
    SubtreeRecord subtree;
    subtree.entities.reserve(members.scene.size());
    for (const flecs::entity_t id : members.scene)
    {
        EntityRecord record = readRecord(world, id);
        record.children = withoutGenerated(world, std::move(record.children));
        if (auto refusal = readValues(world, record))
        {
            return core::makeError(core::ErrorCode::Unsupported,
                                   std::format("Supprimer : {}", *refusal));
        }
        subtree.entities.push_back(std::move(record));
    }
    return subtree;
}

} // namespace levain::scene
