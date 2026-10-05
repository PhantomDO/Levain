#pragma once

// La logique qui relie les personnages aux entités flecs (ADR-0028), en fonctions libres que la glu
// de `physics.cpp` appelle d'une ligne (ADR-0011). Interne au module.

#include <flecs.h>

#include "levain/physics/character.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/scene/components.hpp"

namespace levain::physics
{

/// Ce que le module pose sur un personnage, retiré quand il n'en est plus un : son
/// `CharacterHandle` (qui le détruit) et son `CharacterState`, que le gameplay lirait sinon,
/// périmé, au sol pour toujours.
void forgetCharacter(flecs::entity entity);

/// Remplace le personnage d'une entité par un neuf, ou le retire si elle ne peut pas en porter :
/// un enfant, une échelle (comme un corps, `whyNotABody`), ou un `Collider` en plus, qui lui
/// donnerait aussi un corps.
void rebuildCharacter(PhysicsWorld& physics, flecs::entity entity,
                      const CharacterController& controller, const scene::Transform& transform);

/// Un `Transform` posé téléporte le personnage : sa vitesse voulue remise à zéro (sans quoi elle le
/// remettrait en marche au pas suivant), son `CharacterState` relu à la nouvelle place, et son
/// `PreviousTransform` à la nouvelle pose, pour que le
/// rendu n'interpole pas la traversée. Un `Transform` qui ne convient plus (une échelle) le fait
/// reconstruire, donc refuser.
void teleportOrRebuildCharacter(PhysicsWorld& physics, flecs::entity entity, CharacterHandle handle,
                                const scene::Transform& transform);

/// Avance le personnage d'un pas, avant celui de Jolt : la rotation du `Transform` d'abord, que le
/// gameplay écrit par référence (autour de Y seulement), puis la vitesse voulue. La position que
/// Jolt trouve revient dans le `Transform`, par référence : un `set` le téléporterait.
void advanceCharacter(PhysicsWorld& physics, scene::Transform& transform,
                      const CharacterVelocity* velocity, CharacterHandle handle, float seconds);

/// Après le pas de Jolt : la vitesse du sol relue (une plateforme a bougé pendant le pas), puis ce
/// que le gameplay lira au pas suivant.
void refreshCharacterState(PhysicsWorld& physics, CharacterHandle handle, CharacterState& state);

/// Le dernier personnage d'une entité qui perd son `CharacterHandle`. Pas à la fermeture du monde :
/// le `PhysicsWorld` les libère alors tous, comme ses corps (`destroyUnlessWorldClosing`).
void destroyCharacterUnlessWorldClosing(flecs::world_t* world, CharacterHandle handle);

} // namespace levain::physics
