#pragma once

#include <string>
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
/// (`ecs_make_alive`) et se rejoignent dans `subtree.hpp`, qui garde et rend types, valeurs et
/// noms.
namespace levain::scene
{

/// Posée sur la pierre : une entité vivante qui n'est pas de la scène, juste un index tenu.
/// `isSceneEntity` l'écarte : ni instantané, ni Hiérarchie, ni fichier ne la voient.
struct Tombstone
{
};

/// Une entité supprimée et sa pierre : le même index, la génération suivante.
struct ReservedIndex
{
    flecs::entity_t entity = 0; ///< L'identifiant d'avant, génération comprise.
    flecs::entity_t stone = 0;  ///< Celui de la pierre qui tient l'index.
};

/// Une entité qui n'est pas de la scène (un enfant rangé par `ChildOf`, l'observateur d'un
/// `onEnter`, un préfab), détruite avec son parent. Elle ne revient pas (ADR-0038, décision 3) : la
/// suppression le rapporte, la console le nomme (comme l'ADR-0037, A8). Son nom est lu **avant** la
/// destruction.
struct LostChild
{
    flecs::entity_t id = 0;
    std::string name; ///< « (sans nom) » s'il n'en a pas.
};

/// Les pierres d'une suppression, que **la commande possède** : elle les rend en sortant de la pile
/// (`releaseTombstones`) ou à l'annulation (`reviveFromTombstones`). Un monde ne survit pas à ses
/// pierres : elles ne se gardent pas d'un monde à l'autre.
///
/// **Contrat pour la pile de commandes (morceau A4).** L'identifiant d'une pierre est celui de
/// l'entité, génération suivante : deux suppressions successives de la même entité (supprimer,
/// annuler, supprimer encore) donnent les **mêmes** pierres. Libérer les pierres d'une commande
/// ancienne après qu'une plus récente a supprimé la même entité détruirait les pierres de la
/// récente, dont l'annulation serait alors refusée. La pile vide donc ses commandes refaisables
/// (en les libérant) **avant** d'exécuter une nouvelle commande, et ne libère jamais une commande
/// déjà annulée puis supplantée.
///
/// **Et pour annuler** : `checkTombstonesHold` et `checkSubtreeRestorable` (subtree.hpp), qui ne
/// touchent à rien, d'abord ; `reviveFromTombstones` et `restoreSubtree` ensuite, sans rien
/// d'autre entre eux. Un refus de `restoreSubtree` après le réveil laisserait des entités vides.
struct Tombstones
{
    /// L'entité supprimée d'abord, puis ses descendants `flecs::Parent`, un parent avant ses
    /// enfants.
    std::vector<ReservedIndex> reserved;
    /// Ce qui est détruit avec le sous-arbre sans y être réservé (enfants rangés par `ChildOf`,
    /// préfabs, avec leurs descendants) : jamais réservé ni réveillé. Les enfants d'un nœud
    /// `Generated` n'y sont pas : le générateur les refait, un avertissement serait faux.
    std::vector<LostChild> lost;
    /// Vide si la suppression est annulable. Sinon la raison : **la suppression est faite** mais
    /// `reserved` est vide, et `checkTombstonesHold` comme
    /// `reviveFromTombstones` refusent. À la différence d'un refus (une erreur, le monde intact),
    /// l'appelant doit alors traiter la suppression comme faite : la pile ne pose pas de commande à
    /// annuler, mais marque la scène modifiée, rend la sélection et les épingles, et imprime
    /// `lost`. Un type de retour plutôt qu'un code d'erreur : une erreur dit « rien n'a changé »
    /// dans tout le module, et un `Result` en échec perdrait `lost`.
    std::string notUndoable;
};

/// removeKeepingIndex : détruit l'entité et son sous-arbre, puis pose une pierre sur l'index libéré
/// de chaque entité de la scène du sous-arbre (`flecs::Parent`, hors `Generated` : un nœud généré
/// part sans pierre, son générateur le refait). Ce qui n'est pas de la scène (enfants rangés par
/// `ChildOf`, préfabs) part aussi et est rapporté dans `lost`. Refus nommés, **dans tous les
/// builds**, le monde intact :
///  - en mode différé (les écritures de flecs seraient mises en file) ;
///  - une entité morte, une pierre, ou qui n'est pas de la scène (`isSceneEntity` : un composant,
///    un module) ;
///  - la plage du **jeu** active, sans exception (Play, Simulate), refusée avant tout le reste ;
///  - un index hors de la plage d'identifiants active (`ecs_entity_range_get`), ce qu'une plage
///    d'édition active ne produit pas : garde de dernier recours ;
///  - une entité de la scène **non générée dans le sous-arbre** sous un nœud `Generated`, ou une
///    racine dont le **parent** est `Generated` (une épée sur un os) : l'annuler la réveillerait
///    sous un parent que le générateur peut avoir refait (ADR-0038, décision 10).
///
/// **En jeu (Play, Simulate), supprimer n'est pas une commande** : le chemin est un `ecs_delete`
/// simple, que Stop défait (il réveille l'entité avec son identifiant, `restoreSnapshot`). Une
/// pierre posée en jeu survivrait à Stop (`bornInPlay` l'ignore), d'où le refus ; ADR-0038,
/// décision 9 (amendement du 2026-10-22).
///
/// Si un observateur `OnRemove` crée une entité pendant la destruction et prend un index libéré,
/// la suppression **est faite** mais n'est pas annulable : on rend les `Tombstones` avec `reserved`
/// vide, `lost` rempli et `notUndoable` dit pourquoi, et ce n'est **pas** un refus (aucune pierre
/// n'est posée : mieux qu'une pierre sur l'entité d'un autre).
///
/// Le composant `Tombstone` est enregistré **avant** la destruction : passés les 256 identifiants
/// bas de flecs, un composant enregistré après une destruction prend l'index libéré (il devenait
/// sa propre pierre, essai du relecteur).
///
/// Ne lit rien de l'entité : types, valeurs, noms et parents sont à garder avant, par l'appelant
/// (`readSubtree`). Les nœuds `Generated` du sous-arbre partent avec lui, sans pierre : leur
/// générateur les refait, et la pierre les réveillerait vides.
[[nodiscard]] core::Result<Tombstones> removeKeepingIndex(flecs::world& world,
                                                          flecs::entity_t entity);

/// La garde de l'index, avant chaque réveil, **dans tous les builds** : chaque index réservé doit
/// être vivant, à la génération de sa pierre, et porter `Tombstone`. Sinon une erreur qui nomme
/// l'index. Une suppression `notUndoable` (aucune pierre posée) est refusée d'emblée. En Release
/// `ecs_make_alive` écraserait sans rien dire l'entité qui a pris la place ; en Debug flecs
/// s'arrête (src/entity.c:3133-3139).
[[nodiscard]] core::Result<void> checkTombstonesHold(const flecs::world& world,
                                                     const Tombstones& tombstones);

/// reviveFromTombstones : annule la suppression. Refuse une suppression `notUndoable`, le mode
/// différé et tout index hors de la plage active, garde ensuite (`checkTombstonesHold`, le monde
/// intact si elle refuse), détruit les pierres puis réveille chaque entité avec son identifiant
/// d'avant. Elles reviennent **vides** : ni type, ni valeur, ni nom, ni parent. Les rendre est
/// l'affaire de l'enregistrement de la commande (`restoreSubtree`). Les mêmes
/// `Tombstones` resservent à refaire : `removeKeepingIndex` repose les mêmes pierres (voir le
/// contrat ci-dessus).
[[nodiscard]] core::Result<void> reviveFromTombstones(flecs::world& world,
                                                      const Tombstones& tombstones);

/// releaseTombstones : détruit les pierres sans réveiller personne, quand la commande sort de la
/// pile (débordement, refaisables vidés, scène chargée). Les index repartent dans la liste de
/// flecs. Refuse, le monde intact, le mode différé et un index hors de la plage active (même
/// raison que `removeKeepingIndex`). Ignore une pierre déjà détruite ; ne touche jamais autre chose
/// qu'une pierre.
[[nodiscard]] core::Result<void> releaseTombstones(flecs::world& world,
                                                   const Tombstones& tombstones);

} // namespace levain::scene
