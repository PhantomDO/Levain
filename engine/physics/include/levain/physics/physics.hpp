#pragma once

#include <vector>

#include <flecs.h>

#include "levain/physics/physics_world.hpp"

namespace levain::physics
{

/// L'étiquette d'une entité dont le corps est à (re)construire : son `Collider` ou son `RigidBody`
/// a changé. Les corps se construisent au début du pas de physique suivant, tous ensemble.
struct BodyDirty
{
};

/// La liste des corps déplacés par le dernier pas, gardée d'un pas à l'autre pour ne pas allouer.
struct MovedBodyBuffer
{
    std::vector<MovedBody> bodies;
};

/// Le module flecs de la physique (ADR-0026) : le monde Jolt en singleton, les corps qui suivent
/// les entités, et le pas de physique dans la phase `scene::Physics` du pipeline de simulation.
///
/// Les réglages se changent en posant un `PhysicsSettings` en singleton **avant** l'import :
/// `world.set<PhysicsSettings>({...}); world.import<levain::physics::PhysicsModule>();`.
struct PhysicsModule
{
    explicit PhysicsModule(flecs::world& world);
};

} // namespace levain::physics
