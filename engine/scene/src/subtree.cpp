#include "levain/scene/subtree.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
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
    return refuseRootUnderGenerated(world, root, "Supprimer");
}

/// La racine reprend sa place parmi ses frères : juste après le plus proche prédécesseur de la
/// liste enregistrée qui est encore là, au début s'il n'en reste aucun. Les frères qui sont
/// apparus depuis (hors de la pile) gardent leur place relative, au lieu d'être poussés à la fin.
/// Les nœuds `Generated` sont dans la liste : ils restent vivants, ce sont des repères.
std::vector<flecs::entity_t> orderWithRootBack(const flecs::world& world,
                                               const SubtreeRecord& record)
{
    const flecs::entity_t root = record.entities.front().id;
    std::vector<flecs::entity_t> order = sceneChildrenOf(world, record.parent);
    std::erase(order, root);
    // Une table identifiant -> place : un `find` par prédécesseur coûtait O(n·m) (10 000 frères
    // générés refaits : 352 ms en Debug contre 47 ms).
    std::unordered_map<flecs::entity_t, std::size_t> place;
    place.reserve(order.size());
    for (std::size_t index = 0; index < order.size(); ++index)
    {
        place.emplace(order[index], index);
    }
    std::size_t insertAt = 0;
    const auto rootPlace = std::ranges::find(record.parentChildren, root);
    for (auto before = std::reverse_iterator{rootPlace}; before != record.parentChildren.rend();
         ++before)
    {
        if (const auto found = place.find(*before); found != place.end())
        {
            insertAt = found->second + 1;
            break;
        }
    }
    order.insert(order.begin() + static_cast<std::ptrdiff_t>(insertAt), root);
    return order;
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
    const std::unordered_set<flecs::entity_t> inSubtree(members.scene.begin(), members.scene.end());
    for (const flecs::entity_t id : members.scene)
    {
        EntityRecord record = readRecord(world, id);
        // Les enfants viennent des membres : la règle n'est écrite qu'une fois
        // (`subtreeMembersOf`).
        std::erase_if(record.children,
                      [&](flecs::entity_t child) { return !inSubtree.contains(child); });
        if (auto refusal = readValues(world, record))
        {
            return core::makeError(core::ErrorCode::Unsupported,
                                   std::format("Supprimer : {}", *refusal));
        }
        subtree.entities.push_back(std::move(record));
    }
    subtree.parent = subtree.entities.front().parent;
    if (subtree.parent != 0)
    {
        // Les nœuds `Generated` y sont : ils restent vivants et servent de repères.
        subtree.parentChildren = sceneChildrenOf(world, subtree.parent);
    }
    return subtree;
}

core::Result<void> checkSubtreeRestorable(const flecs::world& world, const SubtreeRecord& record)
{
    if (record.parent != 0 && !ecs_is_alive(world, record.parent))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Annuler : le parent {} est mort, le monde n'est pas "
                                           "touché",
                                           entityIndexOf(record.parent)));
    }
    return {};
}

core::Result<void> restoreSubtree(flecs::world& world, const SubtreeRecord& record)
{
    if (auto deferred = refuseDeferred(world, "Annuler"); !deferred)
    {
        return deferred;
    }
    for (const EntityRecord& entity : record.entities)
    {
        if (ecs_get_alive(world, entityIndexOf(entity.id)) != entity.id)
        {
            return core::makeError(
                core::ErrorCode::InvalidData,
                std::format("Annuler : l'entité {} n'est pas vivante sous son identifiant "
                            "(réveil manquant), le monde n'est pas touché",
                            entityIndexOf(entity.id)));
        }
    }
    if (auto restorable = checkSubtreeRestorable(world, record); !restorable)
    {
        return restorable;
    }
    if (auto restored = restoreRecords(world, record.entities, "Annuler", "l'enregistrement");
        !restored)
    {
        return restored;
    }
    if (record.parent == 0)
    {
        return {};
    }
    const std::vector<flecs::entity_t> order = orderWithRootBack(world, record);
    return setSceneChildOrder(world, record.parent, order, "Annuler", "l'enregistrement");
}

} // namespace levain::scene
