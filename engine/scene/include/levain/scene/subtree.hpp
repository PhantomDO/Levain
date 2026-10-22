#pragma once

#include <vector>

#include <flecs.h>

#include "levain/core/error.hpp"
#include "levain/scene/snapshot.hpp"

/// Ce qu'une commande de l'éditeur garde d'un sous-arbre qu'elle supprime (ADR-0038, décisions 3 et
/// 5) : de quoi le rendre tel qu'il était, avec les mêmes identifiants. La pierre tombale
/// (`removeKeepingIndex`) tient les index, ce fichier garde le reste : parents, types, valeurs,
/// noms et ordre des enfants. Les étapes de restauration sont celles de `restoreSnapshot` (un seul
/// chemin, `entity_record.cpp`) ; la différence est que l'instantané rend toute la scène, ici une
/// branche.
namespace levain::scene
{

/// Un sous-arbre de la scène : la racine d'abord, un parent toujours avant ses enfants, les frères
/// dans leur ordre. **Sans les nœuds `Generated`** (décision 10) : leur générateur les refait, et
/// leurs enfants de la scène aussi. Sans les enfants `ChildOf` (l'observateur d'un `onEnter`), qui
/// ne reviennent pas : `EntityRecord::internalChildren` les nomme, `Tombstones::lost` les rapporte.
/// Les valeurs des composants `Derived` y sont, comme à Stop : l'annulation les rend telles
/// quelles.
///
/// **Ce que le sous-arbre d'une racine à `ModelInstance` rend** (ADR-0038, décision 10) : la racine
/// et ses enfants faits à la main, sans les nœuds `Generated`. `restoreValues` réécrit
/// `ModelInstance` ; les générateurs, coupés pendant la restauration, régénèrent ensuite des
/// nœuds **neufs**, d'autres identifiants, après les enfants à la main. Une comparaison par
/// identifiant saute donc les nœuds `Generated` (décision 12), et leur place parmi les frères
/// n'est pas gardée.
struct SubtreeRecord
{
    std::vector<EntityRecord> entities;
    /// Le parent de la racine, 0 pour une racine de la scène.
    flecs::entity_t parent = 0;
    /// Les enfants de la scène de ce parent avant la suppression, la racine comprise, **nœuds
    /// `Generated` compris** (ils restent vivants et servent de repères), dans leur ordre : sans
    /// eux la racine reviendrait en dernier. À l'annulation la racine se place après son plus
    /// proche prédécesseur encore là ; les frères apparus entre-temps gardent leur place. Vide si
    /// `parent` vaut 0 (les racines n'ont pas d'ordre que flecs garde).
    std::vector<flecs::entity_t> parentChildren;
};

/// readSubtree : copie la racine et ses descendants de la scène, **avant** de les supprimer.
/// Refus nommés, dans tous les builds, le monde intact : le mode différé ; une entité morte, une
/// pierre, ou qui n'est pas de la scène ; une racine `Generated` (on supprime son parent) ; une
/// entité faite à la main sous un nœud `Generated` (décision 10) ; un
/// composant que l'annulation ne saurait pas rendre (`takeSnapshot` en donne la liste : copie
/// interdite, instance de prefab).
[[nodiscard]] core::Result<SubtreeRecord> readSubtree(const flecs::world& world,
                                                      flecs::entity_t root);

/// restoreSubtree : rend le sous-arbre aux entités que `reviveFromTombstones` a réveillées vides.
/// Parents, types, valeurs, noms, ordre des enfants de chacune, puis la place de la racine parmi
/// ses frères. Refus nommés, dans tous les builds, le monde intact : le mode différé ; une entité
/// de l'enregistrement qui n'est pas vivante sous son identifiant exact, ou son parent mort. Après
/// ces gardes, une erreur de l'ordre des enfants arrive quand parents, types, valeurs et noms sont
/// déjà écrits : le monde n'est plus intact, seul l'ordre manque. La vérification finale
/// (`differencesFrom`) est à l'appelant, qui connaît le reste de la scène.
[[nodiscard]] core::Result<void> restoreSubtree(flecs::world& world, const SubtreeRecord& record);

} // namespace levain::scene
