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
struct SubtreeRecord
{
    std::vector<EntityRecord> entities;
};

/// readSubtree : copie la racine et ses descendants de la scène, **avant** de les supprimer.
/// Refus nommés, dans tous les builds, le monde intact : le mode différé ; une entité morte, une
/// pierre, ou qui n'est pas de la scène ; une racine `Generated` (on supprime son parent) ; une
/// entité faite à la main sous un nœud `Generated` (décision 10) ; un
/// composant que l'annulation ne saurait pas rendre (`takeSnapshot` en donne la liste : copie
/// interdite, instance de prefab).
[[nodiscard]] core::Result<SubtreeRecord> readSubtree(const flecs::world& world,
                                                      flecs::entity_t root);

} // namespace levain::scene
