#pragma once

#include <vector>

#include <flecs.h>

#include "levain/core/error.hpp"

/// La pierre tombale (ADR-0038, décision 3) : supprimer une entité pour de bon sans perdre son
/// index, pour que l'annulation la réveille avec le même identifiant, génération comprise.
///
/// Sans pierre, flecs redonne l'index libéré à la **prochaine** création, qui peut être une requête
/// ou un observateur de l'infrastructure : l'entité ne reviendrait jamais (essais a et b de
/// `tests/flecs_tombstone_pins_test.cpp`). Ici, à la suppression, on réveille sur l'index libéré
/// une étiquette `Tombstone` à la génération suivante ; annuler la détruit et réveille l'ancienne
/// génération.
///
/// Un fichier à part de `snapshot.hpp` : l'instantané de Play/Stop copie tout le monde, la pierre
/// sert une commande de l'éditeur sur un sous-arbre. Les deux partagent le réveil
/// (`ecs_make_alive`) et se rejoignent au morceau A2 (ADR-0038), qui rend aussi types, valeurs et
/// noms.
namespace levain::scene
{

/// Posée sur la pierre : une entité vivante qui n'est pas de la scène, juste un index tenu.
/// `isSceneEntity` ne l'écarte pas encore (morceau A2) : aucun éditeur ne l'emploie avant.
struct Tombstone
{
};

/// Une entité supprimée et sa pierre : le même index, la génération suivante.
struct ReservedIndex
{
    flecs::entity_t entity = 0; ///< L'identifiant d'avant, génération comprise.
    flecs::entity_t stone = 0;  ///< Celui de la pierre qui tient l'index.
};

/// Les pierres d'une suppression, que **la commande possède** : elle les rend en sortant de la pile
/// (`releaseTombstones`) ou à l'annulation (`reviveFromTombstones`). Un monde ne survit pas à ses
/// pierres : elles ne se gardent pas d'un monde à l'autre.
struct Tombstones
{
    /// L'entité supprimée.
    std::vector<ReservedIndex> reserved;
};

/// removeKeepingIndex : détruit l'entité, puis pose une pierre sur son index libéré. Refus nommés,
/// **dans tous les builds**, le monde intact : une entité morte, une pierre, ou qui n'est pas de la
/// scène (`isSceneEntity` : un composant, un module).
///
/// Le composant `Tombstone` est enregistré **avant** la destruction : passés les 256 identifiants
/// bas de flecs, un composant enregistré après une destruction prend l'index libéré (il devenait
/// sa propre pierre, essai du relecteur).
///
/// Ne lit rien de l'entité : types, valeurs, noms et parents sont à garder avant, par l'appelant
/// (`EntityRecord`, morceau A2).
[[nodiscard]] core::Result<Tombstones> removeKeepingIndex(flecs::world& world,
                                                          flecs::entity_t entity);

/// reviveFromTombstones : annule la suppression. Détruit les pierres puis réveille chaque entité
/// avec son identifiant d'avant. Elles reviennent **vides** : ni type, ni valeur, ni nom, ni
/// parent. Les rendre est l'affaire de l'enregistrement de la commande (`restoreSnapshot` en donne
/// les étapes). Les mêmes `Tombstones` resservent à refaire : `removeKeepingIndex` repose les mêmes
/// pierres.
[[nodiscard]] core::Result<void> reviveFromTombstones(flecs::world& world,
                                                      const Tombstones& tombstones);

} // namespace levain::scene
