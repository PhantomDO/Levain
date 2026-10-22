#pragma once

// Créer, supprimer et renommer une entité (ADR-0038, décisions 3, 4, 6, 8, 9 et 10) : les commandes
// de structure de la pile et leurs opérations, **sans la pile** (qui est dans `undo.hpp`). Les deux
// sortes de commandes qui remplacent l'écriture d'une valeur :
//   - `SubtreeCommand` garde un sous-arbre (`readSubtree`) et les pierres tombales de ses index
//     (`removeKeepingIndex`) : supprimer l'enlève, annuler le rend avec les mêmes identifiants ;
//     créer est le même objet à l'envers (la pierre n'est posée qu'à l'annulation, le réveil
//     refait l'entité à son identifiant d'origine) ;
//   - `RenameCommand` garde les deux noms.
//
// La logique est en fonctions libres dont toutes les dépendances sont des paramètres (ADR-0011).
// Équivalents : `Undo.DestroyObjectImmediate` et `Undo.RegisterCreatedObjectUndo` d'Unity (qui
// recréent l'objet, sans garantir son identifiant), une transaction `FScopedTransaction` d'Unreal.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/core/error.hpp"
#include "levain/scene/subtree.hpp"
#include "levain/scene/tombstone.hpp"

namespace levain::editor
{

/// Le nom qu'une entité créée à la main reçoit d'abord (Unity : « GameObject »), numéroté s'il est
/// pris (`uniqueNameAmong`).
inline constexpr const char* NewEntityName = "Entité";

/// Ce que la commande d'un sous-arbre a fait quand elle a été posée.
enum class SubtreeOp : std::uint8_t
{
    Create, ///< L'entité vient d'être créée : l'annuler l'enlève, la refaire la réveille.
    Delete, ///< Le sous-arbre vient d'être supprimé : l'annuler le réveille.
};

struct SubtreeCommand
{
    SubtreeOp op = SubtreeOp::Delete;
    /// Le sous-arbre tel qu'il était (supprimer) ou venait d'être créé (créer) : de quoi le rendre.
    scene::SubtreeRecord record;
    /// Les pierres de ses index, **possédées par la commande** : posées à la suppression ou à
    /// l'annulation d'une création, détruites au réveil, rendues quand la commande sort de la pile.
    /// Vides tant qu'une création est faite (l'entité est vivante).
    scene::Tombstones tombstones;
    /// Les assets que `record` peut rendre en se réveillant (`MeshRef`) : épinglés, sans quoi la
    /// suppression de la seule instance d'un modèle le ferait décharger (décision 4).
    assets::AssetPin pin;
};

struct RenameCommand
{
    flecs::entity_t entity = 0;
    /// `nullopt` : l'entité n'avait pas de nom (une entité de la scène en a toujours un, sauf
    /// celles d'un module de test).
    std::optional<std::string> before;
    std::string after;
};

/// Les assets que la valeur d'un composant désigne et que l'annulation doit pouvoir rendre : un
/// `MeshRef` aujourd'hui, `ModelInstance` et `MeshCollider` avec leurs gestes (S2, S3). **Le seul
/// endroit à compléter** quand un composant porte un `AssetRef`.
[[nodiscard]] std::vector<assets::AssetId>
assetsDesignatedBy(const flecs::world& world, flecs::entity_t component, const void* value);

/// Les assets de toutes les valeurs d'un sous-arbre enregistré.
[[nodiscard]] std::vector<assets::AssetId> assetsOfSubtree(const flecs::world& world,
                                                           const scene::SubtreeRecord& record);

// --- Les refus, dits avant d'écrire -------------------------------------------------------------

/// Pourquoi un nom ne convient pas à `entity` (déjà vivante), ou rien s'il convient. La phrase que
/// le champ de renommage montre et que la console répète : une entité qui n'est pas de la scène, un
/// nœud `Generated` (son générateur le nomme), un nom que `refusalOfName` refuse (vide, « . »,
/// « :: », « #12 », « _ », « * », « $ » à la racine), un frère qui le porte déjà. Un nom égal au
/// sien n'est pas un refus : ce n'est pas un changement (`renameEntity` ne pose rien).
[[nodiscard]] std::optional<std::string>
renameRefusalOf(const flecs::world& world, flecs::entity_t entity, std::string_view wanted);

/// Pourquoi `parent` ne peut pas recevoir un enfant créé à la main (0 : une racine, toujours bon) :
/// morte, hors de la scène, ou un nœud `Generated` (annuler la réveillerait sous un parent que son
/// générateur peut avoir refait, décision 10).
[[nodiscard]] std::optional<std::string> createRefusalOf(const flecs::world& world,
                                                         flecs::entity_t parent);

/// Pourquoi `entity` ne se supprime pas **en jeu** (un `ecs_delete` simple, sans `readSubtree`) :
/// les refus que `readSubtree` fait en Édition et qui ne dépendent pas de l'historique (morte, hors
/// de la scène, nœud `Generated`, racine sous un nœud `Generated`).
[[nodiscard]] std::optional<std::string> plainDeleteRefusalOf(const flecs::world& world,
                                                              flecs::entity_t entity);

// --- Les opérations, sans la pile ---------------------------------------------------------------

/// makeEntity : l'entité qu'on crée à la main, nommée `name` (déjà libre, `uniqueNameAmong`), sous
/// `parent` (par `flecs::Parent`, jamais `child_of`, ADR-0015) ou à la racine. **Elle reçoit un
/// `Transform`** : la Hiérarchie ne montre que les entités placées, et sans lui la nouvelle entité
/// serait invisible dans l'arbre (Unity : tout `GameObject` a un `Transform`, documenté [U12]).
/// Hors du mode différé (flecs mettrait les écritures en file).
[[nodiscard]] flecs::entity_t makeEntity(flecs::world& world, flecs::entity_t parent,
                                         std::string_view name);

/// Ce qu'une étape d'annulation ou de rétablissement a fait de la sélection.
struct StepEffect
{
    /// L'entité que l'étape vient de rendre (annuler une suppression, refaire une création) : elle
    /// devient la sélection (décision 8). 0 sinon : une destruction se voit à `selectedIfAlive`.
    flecs::entity_t revived = 0;
};

/// stepSubtree : annule (`redo` faux) ou refait une commande de sous-arbre. L'état d'après l'étape
/// est « vivant » pour annuler une suppression et refaire une création, « enlevé » sinon.
///  - Vivant : **les deux gardes d'abord** (`checkTombstonesHold`, `checkSubtreeRestorable`), puis
///    `reviveFromTombstones` et `restoreSubtree` à la suite, sans rien entre eux (le contrat de
///    `tombstone.hpp`). Un refus laisse le monde intact.
///  - Enlevé : `removeKeepingIndex`, dont les pierres remplacent celles de la commande.
/// Un volume dont l'observateur est perdu est nommé à la console (Warning).
[[nodiscard]] core::Result<StepEffect> stepSubtree(flecs::world& world, SubtreeCommand& command,
                                                   bool redo);

/// stepRename : remet l'ancien nom (annuler) ou le nouveau (refaire). Refuse, le monde intact, une
/// entité morte ou un nom que `renameRefusalOf` ne laisserait plus passer.
[[nodiscard]] core::Result<void> stepRename(flecs::world& world, const RenameCommand& command,
                                            bool redo);

/// Écrit le nom d'une entité : le crochet de flecs qui met l'index de noms à jour (`lookup`).
void writeName(flecs::world& world, flecs::entity_t entity, const std::optional<std::string>& name);

/// Nomme à la console, en Warning, ce qui est perdu avec un sous-arbre supprimé ou réveillé : les
/// observateurs d'un volume (`LostChild`) reviennent sans leur rappel (décision 3). Rend leur
/// nombre.
std::size_t warnAboutLostChildren(const scene::Tombstones& tombstones, std::string_view verb);

} // namespace levain::editor
