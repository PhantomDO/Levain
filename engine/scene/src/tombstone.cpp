#include "levain/scene/tombstone.hpp"

#include <cstdint>
#include <format>

#include "levain/scene/snapshot.hpp"

namespace levain::scene
{

namespace
{

std::uint32_t indexOf(flecs::entity_t entity)
{
    return static_cast<std::uint32_t>(entity);
}

bool isTombstone(const flecs::world& world, flecs::entity_t entity)
{
    return ecs_has_id(world, entity, world.id<Tombstone>());
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

    Tombstones tombstones;
    // La génération que flecs va poser dans l'index libéré (ECS_GENERATION_INC, src/entity.c).
    tombstones.reserved.push_back({.entity = entity, .stone = ECS_GENERATION_INC(entity)});

    ecs_delete(world, entity);
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
