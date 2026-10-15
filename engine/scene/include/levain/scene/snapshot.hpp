#pragma once

#include <optional>
#include <string>
#include <vector>

#include <flecs.h>

#include "levain/core/error.hpp"

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

/// Une valeur de composant copiée par les crochets de son type (`ecs_value_copy`), jamais par
/// octets : un type qui possède de la mémoire se copie bien. Elle garde le monde pour se libérer :
/// **un instantané ne survit pas à son monde**.
class ValueBox
{
public:
    ValueBox(const flecs::world& world, flecs::entity_t type, const void* source);
    ValueBox(const ValueBox&) = delete;
    ValueBox& operator=(const ValueBox&) = delete;
    ValueBox(ValueBox&& other) noexcept;
    ValueBox& operator=(ValueBox&& other) noexcept;
    ~ValueBox();

    [[nodiscard]] const void* data() const { return m_data; }

    [[nodiscard]] flecs::entity_t type() const { return m_type; }

private:
    ecs_world_t* m_world;
    flecs::entity_t m_type;
    void* m_data;
};

/// Un composant (ou une paire à valeur) d'une entité, avec sa valeur.
struct ComponentValue
{
    flecs::entity_t id; ///< L'identifiant dans le type de l'entité, paire comprise.
    ValueBox value;
};

/// Une entité de la scène telle que Play la trouve.
struct EntityRecord
{
    flecs::entity_t id = 0; ///< Avec sa génération : un identifiant gardé ne vaut que pour elle.
    std::optional<std::string> name;
    flecs::entity_t parent = 0; ///< `flecs::Parent`, 0 pour une racine.
    /// Le type complet, trié : composants, étiquettes et paires. Sans ce que flecs tient lui-même
    /// (le nom, `flecs::Parent`, `(ParentDepth, n)`) ni ce qu'un module marque `Transient`.
    std::vector<flecs::entity_t> type;
    std::vector<ComponentValue> values;    ///< Celles des composants du type qui ont une valeur.
    std::vector<flecs::entity_t> children; ///< Les enfants de la scène, dans l'ordre de flecs.
    /// Les enfants internes (l'observateur d'un `onEnter`), qui restent après les autres.
    std::vector<flecs::entity_t> internalChildren;
};

/// Toute la scène, une entité par ligne, triée par index.
struct Snapshot
{
    std::vector<EntityRecord> entities;
};

/// takeSnapshot : copie la scène. Refuse, en nommant l'entité et le composant, ce que Stop ne
/// saurait rendre : un composant qui ne se copie pas (`ECS_TYPE_HOOK_COPY_ILLEGAL`) et une instance
/// de prefab `(IsA, *)`, dont les valeurs héritées ne se copient pas entité par entité. Play ne
/// part pas, et l'Édition reste.
[[nodiscard]] core::Result<Snapshot> takeSnapshot(const flecs::world& world);

/// Les entités de la scène vivantes, par index croissant.
[[nodiscard]] std::vector<flecs::entity_t> sceneEntities(const flecs::world& world);

} // namespace levain::scene
