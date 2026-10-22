#pragma once

// Le geste d'édition de l'inspecteur (ADR-0038, décision 2) : un glissé, une commande. L'inspecteur
// écrit le monde **en direct** à chaque image du glissé ; ce fichier décide quand une commande
// d'annulation se pose, sans ImGui : l'inspecteur lui donne les bords que l'interface a vus
// (`GestureEdges`) et ce qu'il a écrit, et la machine rend le nombre de commandes posées. Le test
// l'alimente donc image par image, sans fenêtre.
//
// **Les règles**
//  - Le geste s'ouvre à `IsItemActivated` : l'avant est la valeur du monde au **début** de
//    l'image, l'inspecteur n'y a pas encore écrit. Il se ferme à `IsItemDeactivatedAfterEdit` :
//    une commande, avant et après (la valeur du monde alors). Un glissé de 30 images est une
//    commande, relâcher puis reprendre le champ en sont deux (pas de clé de fusion).
//  - Une écriture sans geste ouvert est **une commande d'une image** : une liste déroulante
//    (`BeginCombo` + `Selectable`) change la valeur sans donner aucun des deux bords, et
//    `flecs_defer_set` écrit tout de suite un composant que l'entité a déjà, donc la copie
//    d'avant se prend avant l'écriture (`ComponentFrame::before`).
//  - **Fermer avant d'ouvrir** : en passant de x à y d'un Tab, une même image porte la fin de x et
//    le début de y ; deux commandes, quel que soit l'ordre où l'inspecteur dessine les composants.
//    C'est pourquoi les composants ne parlent pas à la machine un à un : ils laissent un
//    `ComponentFrame` chacun, et `settleGestures` règle tout à la fin de l'image.
//  - Un geste dont l'entité change ou disparaît, ou dont l'élément n'est plus l'actif d'ImGui, est
//    fermé à la fin de l'image ; si le glissé continue et écrit encore (Play refusé en plein
//    glissé), cette écriture **rouvre** un geste sous le même identifiant actif : deux commandes,
//    rien de perdu ; tout geste ouvert est fermé avant Play (`closeOpenGesture`).
//  - Échap donne zéro commande **et rend l'avant exact** : ImGui rejoue le texte formaté à
//    l'activation (« 0.123 » pour 0.12345678f) et l'applique comme un changement, le monde ne
//    revient donc pas tout seul ; la machine y remet la valeur du début du geste (`cancelled`).
//    Une commande égale à elle-même est jetée (`sameValue`, dans `recordValueChange`).
//
// Équivalents : `Undo.RecordObject` + `Undo.FlushUndoRecordObjects` d'Unity (« mouse down events
// split undo groups »), `FScopedTransaction` d'Unreal autour d'un `PostEditChangeProperty` d'un
// glissé (`bInteractive`), `UndoRedo::create_action` de Godot (`MERGE_ENDS`, écarté ici).

#include <cstdint>
#include <optional>
#include <vector>

#include <flecs.h>

#include "levain/editor/mode.hpp"
#include "levain/editor/undo.hpp"
#include "levain/scene/snapshot.hpp"

namespace levain::editor
{

/// Les bords que l'interface a vus sur les éléments d'un composant pendant cette image.
struct GestureEdges
{
    bool activated = false; ///< `IsItemActivated` : le champ vient de saisir la souris.
    bool deactivatedAfterEdit =
        false; ///< `IsItemDeactivatedAfterEdit` : relâché après un changement.
    /// `IsItemDeactivated` et Échap pressée : la saisie est abandonnée. ImGui rejoue alors le
    /// texte formaté à l'activation (`%.3f`) et l'applique, donc `changed` et
    /// `deactivatedAfterEdit` sont vrais aussi : la valeur écrite n'est pas l'avant.
    bool cancelled = false;

    GestureEdges& operator|=(const GestureEdges& other)
    {
        activated = activated || other.activated;
        deactivatedAfterEdit = deactivatedAfterEdit || other.deactivatedAfterEdit;
        cancelled = cancelled || other.cancelled;
        return *this;
    }
};

/// Ce qu'un composant a vécu pendant une image de l'inspecteur.
struct ComponentFrame
{
    flecs::entity_t entity = 0;
    flecs::entity_t component = 0;
    GestureEdges edges;
    bool wrote = false; ///< `commitEdit` a écrit le monde à cette image.
    /// **La valeur du monde au début de l'image**, copiée avant `commitEdit` ; présente si le
    /// composant a été écrit ou si un geste s'y ouvre, absente sinon (pas de copie par image).
    std::optional<scene::ValueBox> before;
    bool settled = false; ///< Posé par `settleGestures` : cette image est déjà prise en compte.
};

/// Un geste ouvert : l'avant du début, rangé sous l'identifiant actif d'ImGui.
struct OpenGesture
{
    flecs::entity_t entity = 0;
    flecs::entity_t component = 0;
    scene::ValueBox before;
    std::uint32_t activeId = 0; ///< `ImGui::GetActiveID()` quand il s'est ouvert.
};

/// L'état de la machine entre deux images : le geste ouvert, s'il y en a un. Un glissé fermé de
/// force (Play refusé, entité changée) ne laisse rien ici : sa suite rouvre un geste.
struct EditGesture
{
    std::optional<OpenGesture> open;
};

/// closeOpenGesture : ferme le geste ouvert et pose sa commande (l'après est la valeur du monde
/// maintenant). À appeler **avant Play** : l'instantané prend le monde tel que le glissé l'a écrit,
/// et la commande, posée avant, n'est ni perdue ni doublée. Rend le nombre de commandes posées.
int closeOpenGesture(EditGesture& gesture, UndoStack& stack, flecs::world& world, Mode mode);

/// settleGestures : règle les gestes à la **fin de l'image** de l'inspecteur, avec ce que chaque
/// composant dessiné a vécu (`frames`, vidé). Ordre : fermer ce que les bords ferment, fermer ce
/// dont l'entité, l'élément actif ou la fenêtre n'est plus (`inspected` : l'entité dessinée, 0 si
/// aucune), poser les commandes d'une image des écritures sans geste, puis ouvrir. Hors de
/// l'Édition rien n'est posé (en jeu tout est perdu à Stop, ADR-0037). Rend le nombre de commandes
/// posées.
int settleGestures(EditGesture& gesture, UndoStack& stack, flecs::world& world, Mode mode,
                   std::vector<ComponentFrame>& frames, flecs::entity_t inspected,
                   std::uint32_t activeId);

} // namespace levain::editor
