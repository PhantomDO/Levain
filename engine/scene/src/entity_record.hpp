#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <flecs.h>

#include "levain/core/error.hpp"
#include "levain/scene/snapshot.hpp"

/// Lire et rendre un enregistrement d'entité (`EntityRecord`) : ce que Play/Stop (`snapshot.cpp`)
/// et la commande d'un sous-arbre (`subtree.cpp`, ADR-0038) partagent, **un seul chemin** pour les
/// deux. Privé à `scene` : l'API publique est `takeSnapshot`, `restoreSnapshot`, `readSubtree` et
/// `restoreSubtree`.
namespace levain::scene
{

/// Le chemin sans le « :: » de tête que flecs met aux enfants de `flecs::Parent` : lisible, et
/// `lookup` le relit aussi bien.
[[nodiscard]] std::string pathOf(const flecs::world& world, flecs::entity_t entity);

[[nodiscard]] std::string idLabel(const flecs::world& world, flecs::entity_t id);

/// Le type d'une entité sans ce qui n'est pas son état.
[[nodiscard]] std::vector<flecs::entity_t> sceneTypeOf(const flecs::world& world,
                                                       flecs::entity_t entity);

/// Ce qu'un module marque `Transient` et que l'entité porte : Stop le retire.
[[nodiscard]] std::vector<flecs::entity_t> transientTypeOf(const flecs::world& world,
                                                           flecs::entity_t entity);

/// Le type de la valeur d'un identifiant du type ; 0 pour une étiquette ou une paire sans valeur.
[[nodiscard]] flecs::entity_t valueTypeOf(const flecs::world& world, flecs::id_t id);

/// L'état d'une entité de la scène tel que le monde le tient : tout l'enregistrement sauf les
/// valeurs (`readValues`).
[[nodiscard]] EntityRecord readRecord(const flecs::world& world, flecs::entity_t entity);

/// Complète `record.values`. Le premier composant que Stop ne saurait pas rendre (copie interdite,
/// instance de prefab) rend « chemin : cause » et arrête la lecture.
[[nodiscard]] std::optional<std::string> readValues(const flecs::world& world,
                                                    EntityRecord& record);

/// Une valeur se compare (`sameComponentValue`) si son type est décrit ou porte un crochet `cmp`.
[[nodiscard]] bool isComparable(const flecs::world& world, flecs::entity_t type);
[[nodiscard]] bool sameComponentValue(const flecs::world& world, flecs::entity_t type,
                                      const void* first, const void* second);

/// L'ordre des enfants d'un parent (étape 5) : `sceneChildren` d'abord dans cet ordre, puis les
/// enfants internes. Une erreur, le monde intact, si ce n'est pas une permutation des enfants.
/// `gesture` (« Stop », « Annuler ») ouvre le message.
[[nodiscard]] core::Result<void> setSceneChildOrder(flecs::world& world, flecs::entity_t parent,
                                                    std::span<const flecs::entity_t> sceneChildren,
                                                    const char* gesture);

/// Les étapes 4 et 5 de `restoreSnapshot` : parents (par profondeur croissante), types, valeurs,
/// noms, puis l'ordre des enfants de chaque enregistrement. Les entités doivent être vivantes ;
/// un parent hors de la liste (la racine d'un sous-arbre) doit l'être aussi. Hors du mode différé.
[[nodiscard]] core::Result<void>
restoreRecords(flecs::world& world, std::span<const EntityRecord> records, const char* gesture);

} // namespace levain::scene
