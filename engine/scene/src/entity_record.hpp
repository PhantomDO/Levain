#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <flecs.h>

#include "levain/core/error.hpp"
#include "levain/scene/snapshot.hpp"
#include "levain/scene/tombstone.hpp"

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

/// Les enfants de la scène d'un parent, dans l'ordre de flecs, sans les enfants internes (un
/// observateur rangé par `ChildOf`).
[[nodiscard]] std::vector<flecs::entity_t> sceneChildrenOf(const flecs::world& world,
                                                           flecs::entity_t parent);

/// Qui est dans le sous-arbre d'une suppression : **une seule règle**, que `removeKeepingIndex`
/// (les pierres) et `readSubtree` (l'enregistrement) lisent toutes les deux. Deux règles avaient
/// donné un préfab sous `flecs::Parent` réservé mais pas enregistré, qui revenait en racine vide.
struct SubtreeMembers
{
    /// La racine, puis ses descendants de la scène (`isSceneEntity`, hors `Generated`), largeur
    /// d'abord, les frères dans leur ordre : ce qu'on enregistre et ce qui reçoit une pierre.
    std::vector<flecs::entity_t> scene;
    /// Les nœuds `Generated` et leurs descendants générés : détruits avec le sous-arbre, sans
    /// pierre ni enregistrement, leur générateur les refait.
    std::vector<flecs::entity_t> generated;
    /// Une entité de la scène **non générée** sous un nœud `Generated` (une épée sur un os) : la
    /// première trouvée, 0 s'il n'y en a pas. Annuler la réveillerait sous un parent mort.
    flecs::entity_t handMadeUnderGenerated = 0;
    /// Tout le reste sous la **scène** (enfants `ChildOf` d'un observateur, préfabs, avec leurs
    /// descendants) : détruit, jamais réservé ni réveillé, rapporté par son nom.
    std::vector<flecs::entity_t> other;
    /// Le reste sous un nœud `Generated` : détruit aussi, mais **pas rapporté** (`lostChildrenOf`
    /// ne le lit pas) : le générateur le refait avec le nœud, un avertissement serait faux.
    std::vector<flecs::entity_t> otherUnderGenerated;
};

[[nodiscard]] SubtreeMembers subtreeMembersOf(const flecs::world& world, flecs::entity_t root);

/// Le refus de `SubtreeMembers::handMadeUnderGenerated`, nommé : annuler réveillerait l'entité sous
/// un parent mort, et le fichier ne saurait pas nommer ce parent (ADR-0038, décision 10). Le monde
/// n'est pas touché.
[[nodiscard]] core::Result<void> refuseHandMadeUnderGenerated(const flecs::world& world,
                                                              const SubtreeMembers& members,
                                                              const char* gesture);

/// Le refus d'une racine dont le **parent** est un nœud `Generated` (une épée sur un os) :
/// `refuseHandMadeUnderGenerated` ne regarde que le dedans du sous-arbre. Le générateur peut
/// refaire l'os avec un autre identifiant, et l'annulation n'aurait plus de parent (ADR-0038,
/// décision 10). Le monde n'est pas touché.
[[nodiscard]] core::Result<void>
refuseRootUnderGenerated(const flecs::world& world, flecs::entity_t root, const char* gesture);

/// Ce que la suppression perd et ne rend pas (`SubtreeMembers::other`), nom lu **avant** la
/// destruction.
[[nodiscard]] std::vector<LostChild> lostChildrenOf(const flecs::world& world,
                                                    const SubtreeMembers& members);

/// L'entité porte `Generated` : elle se recrée depuis sa racine et n'entre pas dans un sous-arbre.
[[nodiscard]] bool isGenerated(const flecs::world& world, flecs::entity_t entity);

/// Le refus du mode différé, **dans tous les builds** : les écritures de flecs (réveil,
/// destruction, ordre des enfants) y seraient mises en file, et la lecture suivante verrait un
/// monde pas encore écrit. `gesture` ouvre le message ; le monde n'est pas touché.
[[nodiscard]] core::Result<void> refuseDeferred(const flecs::world& world, const char* gesture);

/// L'ordre des enfants d'un parent (étape 5) : `sceneChildren` d'abord dans cet ordre, puis les
/// enfants internes. Une erreur, le monde intact, si ce n'est pas une permutation des enfants.
/// `gesture` (« Stop », « Annuler ») ouvre le message, `source` dit d'où vient la liste
/// (« l'instantané », « l'enregistrement »).
[[nodiscard]] core::Result<void> setSceneChildOrder(flecs::world& world, flecs::entity_t parent,
                                                    std::span<const flecs::entity_t> sceneChildren,
                                                    const char* gesture, const char* source);

/// Les étapes 4 et 5 de `restoreSnapshot` : parents (par profondeur croissante), types, valeurs,
/// noms, puis l'ordre des enfants de chaque enregistrement. Les entités doivent être vivantes ;
/// un parent hors de la liste (la racine d'un sous-arbre) doit l'être aussi. Hors du mode différé.
/// Une erreur de l'étape 5 vient **après** l'étape 4 : parents, types, valeurs et noms sont déjà
/// écrits, seul l'ordre des enfants manque (l'appelant vérifie la scène, `differencesFrom`).
[[nodiscard]] core::Result<void> restoreRecords(flecs::world& world,
                                                std::span<const EntityRecord> records,
                                                const char* gesture, const char* source);

} // namespace levain::scene
