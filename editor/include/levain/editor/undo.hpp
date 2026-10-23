#pragma once

// La pile d'annulation de l'éditeur (ADR-0038, décisions 1, 8 et 9) : 100 commandes, Annuler et
// Rétablir. Une commande de **valeur** garde l'entité (identifiant complet), le composant et deux
// `ValueBox`, l'avant et l'après, copiées par les crochets du type (jamais du JSON, qui arrondit).
// Les sortes de structure (`structure.hpp`) : supprimer garde un sous-arbre et ses pierres
// tombales, renommer deux noms (M7.3, A6) ; créer, déplacer, ajouter ou retirer un composant
// prendront leur place dans `CommandKind` ensuite (A6, A7 et A8).
//
// La logique est en fonctions libres dont toutes les dépendances sont des paramètres (ADR-0011) ;
// la glu d'`editor.cpp` tient en une ligne (`applyQueuedRequests`).
//
// **Le contrat de la pile** (tombstone.hpp) : une commande nouvelle vide d'abord les refaisables,
// en les libérant (`releaseCommand`), puis s'exécute ; une commande qui sort de la pile
// (débordement, refaisables vidés, scène chargée) rend ce qu'elle retient (ses épingles d'assets,
// plus tard ses pierres tombales), et une commande annulée puis supplantée n'est jamais libérée
// deux fois.
// **Annuler n'est actif qu'en Édition** (décision 9) : en jeu la pile est **gelée**, pas vidée
// (Stop rend la scène identique, les commandes d'avant Play restent vraies), et rien de ce qu'on
// fait en jeu n'y entre. Équivalents : `Undo` d'Unity (groupes par geste), `FScopedTransaction`
// d'Unreal, `UndoRedo` de Godot (`max_steps`).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/core/error.hpp"
#include "levain/editor/mode.hpp"
#include "levain/editor/structure.hpp"
#include "levain/scene/snapshot.hpp"

namespace levain::editor
{

/// La profondeur de la pile (ROADMAP) : une 101e commande fait sortir la plus ancienne.
inline constexpr std::size_t UndoDepth = 100;

/// Écrire `after` dans le composant d'une entité ; annuler y remet `before`.
struct ValueCommand
{
    flecs::entity_t entity = 0; ///< Avec sa génération : une entité recyclée n'est pas celle-ci.
    flecs::entity_t component = 0;
    scene::ValueBox before;
    scene::ValueBox after;
    /// Les assets que `before` et `after` désignent (`MeshRef`) : épinglés tant que la commande est
    /// dans la pile, pour que le modèle ne se décharge pas sous elle (ADR-0038, décision 4).
    assets::AssetPin pin;
};

/// Les sortes de commandes : une valeur écrite, un sous-arbre créé ou supprimé, un nom changé.
using CommandKind = std::variant<ValueCommand, SubtreeCommand, RenameCommand>;

/// Les assets qu'une commande épingle, pour que le test de la pile lise la même vérité que le
/// décompte d'`AssetUsage` : une surcharge par sorte, un visiteur les atteint (une sorte de plus
/// ajoute la sienne ici).
[[nodiscard]] inline const std::vector<assets::AssetId>& pinnedAssetsOf(const ValueCommand& command)
{
    return command.pin.assets;
}

[[nodiscard]] inline std::vector<assets::AssetId> pinnedAssetsOf(const SubtreeCommand&)
{
    return {}; // le sous-arbre n'épingle rien encore : ses assets viennent en A6e
}

[[nodiscard]] inline std::vector<assets::AssetId> pinnedAssetsOf(const RenameCommand&)
{
    return {}; // un nom ne désigne aucun asset
}

struct Command
{
    /// Le numéro de la commande, croissant, jamais réutilisé : « modifié » compare des numéros
    /// (décision 9), un compte de pile serait faux après un débordement.
    std::uint64_t number = 0;
    CommandKind kind;
};

struct UndoStack
{
    std::vector<Command> done;   ///< Les commandes faites, la plus ancienne d'abord.
    std::vector<Command> undone; ///< Les refaisables, la prochaine à refaire en dernier.
    std::uint64_t nextNumber = 1;
    /// Le numéro du sommet quand la scène a été enregistrée (0 : la pile vide, l'état d'ouverture).
    std::uint64_t savedNumber = 0;
    /// Le numéro de la dernière commande sortie de la pile par le bas (0 : aucune). L'état d'après
    /// elle est le fond de la pile : on y revient en tout annulant, et `topNumberOf` le rend quand
    /// `done` est vide. Les numéros ne sont jamais réutilisés : un point d'enregistrement parmi des
    /// refaisables vidés ne revient jamais, sans drapeau à tenir.
    std::uint64_t bottomNumber = 0;
};

[[nodiscard]] bool canUndo(const UndoStack& stack);
[[nodiscard]] bool canRedo(const UndoStack& stack);

/// Le numéro du sommet de la pile ; vide, celui du fond (`bottomNumber`).
[[nodiscard]] std::uint64_t topNumberOf(const UndoStack& stack);

/// **« Modifié »** se lit dans la pile (décision 9) : le sommet n'est plus le numéro noté à
/// l'enregistrement. Un « modifié » tenu à la main oublierait
/// qu'annuler jusqu'au point d'enregistrement rend la scène propre (la leçon de Godot, [6]).
[[nodiscard]] bool isModified(const UndoStack& stack);

/// Note le sommet comme l'état enregistré.
void markSaved(UndoStack& stack);

/// La pile du chargement d'une scène et de « Nouveau » : tout est libéré (épingles, plus tard les
/// pierres), les numéros continuent, l'état d'ouverture est propre.
void clearUndoStack(UndoStack& stack, flecs::world& world);

/// Ce que la commande retient et que la pile doit rendre en la laissant partir.
void releaseCommand(flecs::world& world, Command& command);

/// Pourquoi Annuler ou Rétablir ne part pas maintenant.
enum class HistoryRefusal : std::uint8_t
{
    None,
    NotEditing, ///< « seulement en Édition » : en jeu la pile est gelée.
    Nothing,    ///< Rien à annuler (ou à rétablir).
};

/// Le refus d'Annuler (`redo` faux) ou de Rétablir, selon le mode et la pile. Un nom pour le piège
/// : le menu, le raccourci et la file lisent la même règle.
[[nodiscard]] HistoryRefusal historyRefusalOf(const UndoStack& stack, Mode mode, bool redo);

/// recordValueChange : pose la commande d'une valeur changée, **après** l'écriture. Rien n'entre
/// en jeu (c'est perdu à Stop, ADR-0037), ni une commande dont l'avant égale l'après (`sameValue` :
/// ImGui rend des faux positifs). Une commande vide d'abord les refaisables, puis la plus ancienne
/// sort si la pile dépasse `UndoDepth`. Rend vrai si la commande est posée.
[[nodiscard]] bool recordValueChange(UndoStack& stack, flecs::world& world, Mode mode,
                                     flecs::entity_t entity, flecs::entity_t component,
                                     scene::ValueBox before, scene::ValueBox after);

/// editValue : l'écriture d'un geste d'une image, par la **même** porte que l'annulation
/// (`setComponentValue`) : copie l'avant, écrit, pose la commande (`recordValueChange`). Rend vrai
/// si une commande est posée ; une erreur nommée, le monde intact, pour une entité morte, un
/// composant absent, un type sans valeur, ou **un type que `sameValue` ne sait pas comparer**
/// (`isComparableValue` : refusé avant d'écrire, sinon l'assertion de `sameValue` s'arrêterait sur
/// une valeur déjà écrite). `value` est une valeur du type du composant.
[[nodiscard]] core::Result<bool> editValue(UndoStack& stack, flecs::world& world, Mode mode,
                                           flecs::entity_t entity, flecs::entity_t component,
                                           const void* value);

// --- Créer, supprimer, renommer : les opérations de la pile
// ---------------------------------------

/// deleteEntity : supprime `entity` et son sous-arbre, avec sa commande : `readSubtree` (les
/// refus : mode différé, nœud `Generated`, racine sous un nœud `Generated`, composant qui ne se
/// copie pas), les refaisables libérés, puis `removeKeepingIndex`. Les observateurs d'un volume
/// perdus sont nommés à la console. **En jeu, un `ecs_delete` simple, jamais une commande**
/// (décision 9, amendement du 2026-10-22). Rend vrai si une commande est posée. Une suppression
/// que les pierres n'ont pas pu tenir (`notUndoable`) est faite, sans commande.
[[nodiscard]] core::Result<bool> deleteEntity(UndoStack& stack, flecs::world& world, Mode mode,
                                              flecs::entity_t entity);

/// renameEntity : donne `wanted` à `entity`. Un nom que `renameRefusalOf` refuse (vide, « . », « ::
/// », « #12 », « _ », « * », « $ » à la racine, un frère identique, un nœud `Generated`) est une
/// erreur qui dit pourquoi, le monde intact ; un nom égal au sien ne change rien (faux, pas de
/// commande). Hors de l'Édition, le nom change sans commande. Rend vrai si le nom a changé.
[[nodiscard]] core::Result<bool> renameEntity(UndoStack& stack, flecs::world& world, Mode mode,
                                              flecs::entity_t entity, std::string_view wanted);

// --- La file des demandes ------------------------------------------------------------------------

/// Ce que la file demande.
enum class RequestKind : std::uint8_t
{
    Undo,
    Redo,
    Delete, ///< `entity` : l'entité à supprimer.
    Rename, ///< `entity` : l'entité ; `name` : son nouveau nom.
};

struct EditorRequest
{
    // Implicite, pour qu'un test écrive `{RequestKind::Undo, RequestKind::Redo}`.
    EditorRequest(RequestKind kind, flecs::entity_t entity = 0, std::string name = {})
        : kind(kind), entity(entity), name(std::move(name))
    {
    }

    RequestKind kind;
    flecs::entity_t entity = 0;
    std::string name;
};

/// Le bilan d'`applyEditorRequests`, pour la console et les tests.
struct RequestsReport
{
    int undone = 0;
    int redone = 0;
    int deleted = 0;
    int renamed = 0;
    int refused = 0; ///< Hors de l'Édition, rien à faire, ou un refus nommé : dit à la console.
    int failed =
        0; ///< La commande n'a pas pu s'appliquer : elle est abandonnée, dite à la console.
    /// L'entité que l'éditeur sélectionne (décision 8) : la dernière réveillée.
    flecs::entity_t select = 0;

    [[nodiscard]] bool changedWorld() const { return undone + redone + deleted + renamed > 0; }
};

/// **Le point nommé où les demandes s'exécutent** (décision 9). Les fenêtres, dessinées entre
/// `defer_begin` et `defer_end`, ne font que `queue.push_back` ; ce point passe **juste après
/// `defer_end`**. Les raccourcis et les menus, hors du `defer`, y passent directement. Appelé dans
/// le mode différé, il refuse bruyamment et garde la file : les écritures de flecs y seraient mises
/// en file et le monde lu ne serait pas celui qu'on a écrit.
[[nodiscard]] RequestsReport applyEditorRequests(std::vector<EditorRequest>& queue,
                                                 UndoStack& stack, flecs::world& world, Mode mode);

} // namespace levain::editor
