#include "levain/scene/snapshot.hpp"

#include <cstdint>

namespace levain::scene
{

namespace
{

std::uint32_t indexOf(flecs::entity_t entity)
{
    return static_cast<std::uint32_t>(entity);
}

} // namespace

bool isEngineInternal(const flecs::table& table)
{
    return table.has(flecs::Module) || table.has(flecs::System) || table.has(flecs::Observer) ||
           table.has(flecs::Query) || table.has<flecs::Component>() || table.has(flecs::Prefab);
}

bool isSceneEntity(const flecs::world& world, flecs::entity_t entity)
{
    if (!ecs_is_alive(world, entity) || indexOf(entity) < EcsFirstUserEntityId)
    {
        return false;
    }
    const flecs::table table = world.entity(entity).table();
    return !isEngineInternal(table) && !table.has(flecs::ChildOf, flecs::Wildcard);
}

} // namespace levain::scene
