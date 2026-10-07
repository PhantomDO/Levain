#include "character_sync.hpp"

#include <optional>
#include <string_view>

#include "levain/core/log.hpp"
#include "levain/core/profile.hpp"
#include "levain/physics/body_rules.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/physics.hpp"

namespace levain::physics
{

namespace
{

/// Pourquoi cette entité ne peut pas porter de personnage, ou rien : les règles d'un corps (une
/// racine sans échelle, ADR-0026), et pas de `Collider` en plus. Le personnage a sa capsule dans
/// son `CharacterController` ; un `Collider` lui donnerait aussi un corps, que le personnage
/// pousserait en marchant.
std::optional<std::string_view> whyNotACharacter(flecs::entity entity,
                                                 const scene::Transform& transform)
{
    if (const auto reason = whyNotABody(transform, entity.has<flecs::Parent>()))
    {
        return reason;
    }
    if (entity.has<Collider>())
    {
        return "un personnage n'a pas de Collider : sa capsule est dans son CharacterController";
    }
    return std::nullopt;
}

} // namespace

void forgetCharacter(flecs::entity entity)
{
    entity.remove<CharacterHandle>();
    entity.remove<CharacterState>();
}

void rebuildCharacter(PhysicsWorld& physics, flecs::entity entity,
                      const CharacterController& controller, const scene::Transform& transform)
{
    entity.remove<CharacterDirty>();
    if (const auto reason = whyNotACharacter(entity, transform))
    {
        // Pas d'assertion : une donnée refusée n'est pas un bug, et l'inspecteur peut la taper
        // (ADR-0034).
        core::log("physics", core::LogLevel::Error, "{} : {}", entity.path().c_str(), *reason);
        // L'ancien personnage, s'il y en a un, part avec son CharacterHandle (observateur
        // DestroyCharacter).
        forgetCharacter(entity);
        return;
    }
    if (const CharacterHandle* previous = entity.try_get<CharacterHandle>())
    {
        destroyCharacter(physics, *previous);
    }
    const CharacterHandle created =
        createCharacter(physics, controller, poseOf(transform), entity.id());
    // Posé avant d'être retiré s'il est refusé : le retrait ne détruit pas une seconde fois
    // l'ancien.
    entity.set<CharacterHandle>(created);
    if (created.value == CharacterHandle::None)
    {
        entity.remove<CharacterHandle>();
        return;
    }
    // Ce que le gameplay lit et écrit existe dès le premier pas ; une vitesse déjà posée reste.
    entity.add<CharacterVelocity>();
    entity.set<CharacterState>({.ground = characterGround(physics, created)});
}

void teleportOrRebuildCharacter(PhysicsWorld& physics, flecs::entity entity, CharacterHandle handle,
                                const scene::Transform& transform)
{
    if (whyNotACharacter(entity, transform).has_value())
    {
        entity.add<CharacterDirty>();
        return;
    }
    teleportCharacter(physics, handle, poseOf(transform));
    if (CharacterVelocity* velocity = entity.try_get_mut<CharacterVelocity>())
    {
        velocity->value = glm::vec3{0.0f};
    }
    // Son sol est celui de la nouvelle place : le gameplay du pas suivant ne doit pas le croire
    // encore là où il était.
    if (CharacterState* state = entity.try_get_mut<CharacterState>())
    {
        *state = {.ground = characterGround(physics, handle), .velocity = glm::vec3{0.0f}};
    }
}

void advanceCharacter(PhysicsWorld& physics, scene::Transform& transform,
                      const CharacterVelocity* velocity, CharacterHandle handle, float seconds)
{
    LEVAIN_PROFILE_SCOPE();
    turnCharacter(physics, handle, transform.rotation);
    moveCharacter(physics, handle, velocity != nullptr ? velocity->value : glm::vec3{0.0f},
                  seconds);
    transform.position = characterPose(physics, handle).position;
}

void refreshCharacterState(PhysicsWorld& physics, CharacterHandle handle, CharacterState& state)
{
    refreshCharacterGround(physics, handle);
    state.ground = characterGround(physics, handle);
    state.velocity = characterVelocity(physics, handle);
}

void destroyCharacterUnlessWorldClosing(flecs::world_t* world, CharacterHandle handle)
{
    if (!ecs_is_fini(world))
    {
        destroyCharacter(flecs::world{world}.get_mut<PhysicsWorld>(), handle);
    }
}

} // namespace levain::physics
