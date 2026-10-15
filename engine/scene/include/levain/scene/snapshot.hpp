#pragma once

#include <flecs.h>

/// L'instantané de la scène (ADR-0037) : ce que Play garde pour que Stop rende la scène telle
/// qu'elle était, en place, avec les mêmes identifiants. Sans éditeur ni GPU : seul l'éditeur
/// l'appelle, mais `scene` le teste, web compris.
namespace levain::scene
{

/// Posée sur l'entité d'un composant, comme `Authored` : ce que le module range là n'est pas la
/// scène. L'instantané ne le copie pas (même s'il ne se copie pas), et Stop le retire des
/// entités. C'est le cas des handles de Jolt, qui désigneraient des corps détruits.
///
///     world.component<BodyHandle>().add<scene::Transient>();
struct Transient
{
};

/// Une table d'entités de flecs plutôt que de la scène : modules, systèmes, observateurs,
/// requêtes, composants, prefabs. La hiérarchie les tait, table par table et non entité par
/// entité ; un composant qui porte sa propre valeur va dans « Singletons ».
[[nodiscard]] bool isEngineInternal(const flecs::table& table);

/// Une entité de la scène : ce que la Hiérarchie pourrait montrer. Vivante, ni flecs lui-même (un
/// index sous `EcsFirstUserEntityId`), ni `isEngineInternal`, ni rangée par flecs sous un module :
/// sa **table** porte `(ChildOf, *)`. On lit la table, car un enfant de `flecs::Parent` répond oui
/// à `entity.has(ChildOf, *)` sans être sous un module (ADR-0015, ADR-0037).
[[nodiscard]] bool isSceneEntity(const flecs::world& world, flecs::entity_t entity);

} // namespace levain::scene
