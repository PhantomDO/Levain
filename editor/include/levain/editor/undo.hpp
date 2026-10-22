#pragma once

// La pile d'annulation de l'éditeur (ADR-0038, décisions 1, 8 et 9) : 100 commandes, Annuler et
// Rétablir. Une commande de **valeur** garde l'entité (identifiant complet), le composant et deux
// `ValueBox`, l'avant et l'après, copiées par les crochets du type (jamais du JSON, qui arrondit).
// Les autres sortes (créer, supprimer, renommer, déplacer, ajouter ou retirer un composant)
// prendront leur place dans `CommandKind` aux morceaux A6 à A8.
//
// La logique est en fonctions libres dont toutes les dépendances sont des paramètres (ADR-0011) ;
// la glu d'`editor.cpp` tient en une ligne (`applyHistoryRequests`).
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
#include <variant>
#include <vector>

#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/core/error.hpp"
#include "levain/editor/mode.hpp"
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

/// Les sortes de commandes. Une seule aujourd'hui ; les autres arrivent avec leurs gestes.
using CommandKind = std::variant<ValueCommand>;

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
/// composant absent ou un type sans valeur. `value` est une valeur du type du composant.
[[nodiscard]] core::Result<bool> editValue(UndoStack& stack, flecs::world& world, Mode mode,
                                           flecs::entity_t entity, flecs::entity_t component,
                                           const void* value);

/// Ce que la file demande : une étape d'historique.
enum class HistoryRequest : std::uint8_t
{
    Undo,
    Redo,
};

/// Le bilan d'`applyEditorRequests`, pour la console et les tests.
struct RequestsReport
{
    int undone = 0;
    int redone = 0;
    int refused = 0; ///< Hors de l'Édition, ou rien à faire : dit à la console.
    int failed =
        0; ///< La commande n'a pas pu s'appliquer : elle est abandonnée, dite à la console.

    [[nodiscard]] bool changedWorld() const { return undone + redone > 0; }
};

/// **Le point nommé où les demandes d'historique s'exécutent** (décision 9). Les fenêtres,
/// dessinées entre `defer_begin` et `defer_end`, ne font que `queue.push_back` ; ce point passe
/// **juste après `defer_end`**. Les raccourcis et les menus, hors du `defer`, y passent
/// directement. Appelé dans le mode différé, il refuse bruyamment et garde la file : les écritures
/// de flecs y seraient mises en file et le monde lu ne serait pas celui qu'on a écrit.
[[nodiscard]] RequestsReport applyEditorRequests(std::vector<HistoryRequest>& queue,
                                                 UndoStack& stack, flecs::world& world, Mode mode);

} // namespace levain::editor
