#include "levain/scene/tombstone.hpp"

#include <cstdint>
#include <format>
#include <span>
#include <utility>

#include "levain/scene/snapshot.hpp"

namespace levain::scene
{

namespace
{

std::uint32_t indexOf(flecs::entity_t entity)
{
    return static_cast<std::uint32_t>(entity);
}

/// Le sous-arbre de la scène (`flecs::Parent`), un parent avant ses enfants, et à part tout ce qui
/// est rangé par `ChildOf` (l'observateur d'un `onEnter`) avec ses descendants : cela ne revient
/// pas. Copié avant toute destruction : la moindre écriture invaliderait l'itérateur.
struct Subtree
{
    std::vector<flecs::entity_t> scene;
    std::vector<LostChild> lost;
};

Subtree subtreeOf(const flecs::world& world, flecs::entity_t root)
{
    Subtree subtree{.scene = {root}, .lost = {}};
    std::vector<flecs::entity_t> lostIds;
    const auto walk = [&](flecs::entity_t parent, bool inScene)
    {
        ecs_iter_t it = ecs_children(world, parent);
        while (ecs_children_next(&it))
        {
            for (const flecs::entity_t child :
                 std::span{it.entities, static_cast<std::size_t>(it.count)})
            {
                if (inScene && world.entity(child).table().has<flecs::Parent>())
                {
                    subtree.scene.push_back(child);
                }
                else
                {
                    lostIds.push_back(child);
                }
            }
        }
    };
    for (std::size_t next = 0; next < subtree.scene.size(); ++next)
    {
        walk(subtree.scene[next], true);
    }
    for (std::size_t next = 0; next < lostIds.size(); ++next)
    {
        walk(lostIds[next], false);
    }
    for (const flecs::entity_t id : lostIds)
    {
        const char* name = ecs_get_name(world, id);
        subtree.lost.push_back({.id = id, .name = name != nullptr ? name : "(sans nom)"});
    }
    return subtree;
}

bool isTombstone(const flecs::world& world, flecs::entity_t entity)
{
    return ecs_has_id(world, entity, world.id<Tombstone>());
}

/// Une pierre est tenue quand son index est vivant sous sa génération et qu'elle porte l'étiquette.
bool holds(const flecs::world& world, const ReservedIndex& reserved)
{
    return ecs_get_alive(world, indexOf(reserved.stone)) == reserved.stone &&
           isTombstone(world, reserved.stone);
}

} // namespace

core::Result<Tombstones> removeKeepingIndex(flecs::world& world, flecs::entity_t entity)
{
    // Avant toute destruction : passés les 256 identifiants bas de flecs, un composant enregistré
    // après une destruction prend l'index libéré (et devient sa propre pierre).
    const flecs::entity_t tag = world.component<Tombstone>().id();
    if (!ecs_is_alive(world, entity))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Supprimer : l'entité {} n'est pas vivante, le monde "
                                           "n'est pas touché",
                                           indexOf(entity)));
    }
    if (ecs_has_id(world, entity, tag))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Supprimer : l'index {} est une pierre tombale, pas une "
                                           "entité, le monde n'est pas touché",
                                           indexOf(entity)));
    }
    if (!isSceneEntity(world, entity))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("Supprimer : l'entité {} n'est pas une entité de la "
                                           "scène (composant, module, système…), le monde n'est "
                                           "pas touché",
                                           indexOf(entity)));
    }

    Subtree subtree = subtreeOf(world, entity);
    Tombstones tombstones;
    tombstones.lost = std::move(subtree.lost);
    tombstones.reserved.reserve(subtree.scene.size());
    for (const flecs::entity_t id : subtree.scene)
    {
        // La génération que flecs va poser dans l'index libéré (ECS_GENERATION_INC, src/entity.c).
        tombstones.reserved.push_back({.entity = id, .stone = ECS_GENERATION_INC(id)});
    }

    ecs_delete(world, entity);
    // Un observateur `OnRemove` a pu créer une entité pendant la destruction et prendre un index
    // libéré, à la génération de la pierre : `ecs_make_alive` n'y changerait rien et la pierre
    // serait cette entité. On vérifie tous les index avant d'en toucher un seul.
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        if (ecs_get_alive(world, indexOf(reserved.entity)) != 0)
        {
            return core::makeError(
                core::ErrorCode::InvalidData,
                std::format("Supprimer : l'index {} a été repris pendant la destruction (un "
                            "observateur a créé une entité) : la suppression est faite, aucune "
                            "pierre posée, l'annulation est impossible",
                            indexOf(reserved.entity)));
        }
    }
    // Toutes les pierres vivantes d'abord, l'étiquette ensuite.
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        ecs_make_alive(world, reserved.stone);
    }
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        ecs_add_id(world, reserved.stone, tag);
    }
    return tombstones;
}

core::Result<void> checkTombstonesHold(const flecs::world& world, const Tombstones& tombstones)
{
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        if (!holds(world, reserved))
        {
            return core::makeError(
                core::ErrorCode::InvalidData,
                std::format("Annuler : l'index {} n'est plus tenu par la pierre tombale de la "
                            "commande (génération vivante : {}), le monde n'est pas touché",
                            indexOf(reserved.entity),
                            ecs_get_alive(world, indexOf(reserved.entity)) >> 32U));
        }
    }
    return {};
}

namespace
{

void destroyStones(flecs::world& world, const Tombstones& tombstones)
{
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        if (ecs_is_alive(world, reserved.stone) && isTombstone(world, reserved.stone))
        {
            ecs_delete(world, reserved.stone);
        }
    }
}

} // namespace

core::Result<void> reviveFromTombstones(flecs::world& world, const Tombstones& tombstones)
{
    if (auto held = checkTombstonesHold(world, tombstones); !held)
    {
        return held;
    }
    // Toutes les pierres d'abord : `ecs_make_alive` refuse (Debug) un index vivant d'une autre
    // génération.
    destroyStones(world, tombstones);
    for (const ReservedIndex& reserved : tombstones.reserved)
    {
        ecs_make_alive(world, reserved.entity);
    }
    return {};
}

} // namespace levain::scene
