#pragma once

// La session scriptée de la CI pour l'annulation (ADR-0038, « Les contrôles », « La ligne de la CI
// »). Elle fait 150 changements de valeur, en annule 100, et compare le monde à celui d'après le
// 50e changement : la pile garde 100 niveaux, donc 100 annulations en arrière rendent exactement
// cet état. Elle écrit une ligne sous un nom stable, que la CI lit (ADR-0036, décision 14) :
//   editor.undo changes=150 undone=100 differences=0 -- <phrase en français>
//
// **Pourquoi une API de commandes et non un script d'input** : 150 changements au clavier et à la
// souris demandent 150 glissés au pixel près dans une fenêtre dont la disposition change avec la
// machine ; le script d'input ne sait pas non plus taper un nombre. Ce que la session éprouve est
// la pile et les annulations, sur la **même porte** que l'inspecteur (`editValue`, puis
// `applyEditorRequests`, comme Ctrl+Z). Les gestes de l'inspecteur, de leur côté, sont cliqués et
// glissés dans la vraie fenêtre par `gpu.editor-undo.*` (tests/editor_undo_gpu.cpp), et Ctrl+Z
// au clavier par le même banc.
//
// Les fonctions sont libres et reçoivent leurs dépendances (ADR-0011) : `stepUndoSession` rend le
// bilan une fois, à la fin, et un test l'appelle sans fenêtre.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <flecs.h>

#include "levain/core/error.hpp"
#include "levain/editor/mode.hpp"
#include "levain/editor/undo.hpp"
#include "levain/scene/snapshot.hpp"

namespace levain::editor
{

/// Les nombres de la session : ceux de la ligne de la CI sont exigés tels quels (règle n°7).
inline constexpr int UndoSessionChanges = 150;
inline constexpr int UndoSessionUndos = 100;
/// L'état de référence : celui d'après le 50e changement (150 - 100).
inline constexpr int UndoSessionReference = UndoSessionChanges - UndoSessionUndos;

/// Où en est la session, et sur quoi elle travaille.
struct UndoSession
{
    flecs::entity_t entity = 0; ///< L'entité dont le `Transform` change.
    int changes = 0;            ///< Les changements posés (une commande chacun).
    int undone = 0;             ///< Les annulations faites.
    bool reported = false;
    /// Le monde après le 50e changement, pris à l'image suivante (les matrices monde ont eu le
    /// temps de se mettre à jour).
    std::optional<scene::Snapshot> reference{};
};

/// Le bilan, rendu une fois.
struct UndoSessionReport
{
    int changes = 0;
    int undone = 0;
    std::size_t differences = 0;
    std::string differencesText;
};

/// startUndoSession : la session sur l'entité nommée, qui doit exister et porter un `Transform`.
/// Une erreur nommée sinon (règle n°7 : une session sans cible ne vérifie rien).
[[nodiscard]] core::Result<UndoSession> startUndoSession(const flecs::world& world,
                                                         const std::string& entityName);

/// stepUndoSession : une image de la session, **hors du mode différé**. D'abord les 150
/// changements (un par image), puis les 100 annulations (une par image), puis, à l'image suivante,
/// la comparaison au monde de référence. Rend le bilan à cette dernière image, rien avant ni après.
[[nodiscard]] std::optional<UndoSessionReport>
stepUndoSession(UndoSession& session, UndoStack& stack, flecs::world& world, Mode mode);

/// La ligne de la CI, sous son nom stable, puis la phrase en français.
[[nodiscard]] std::string undoLineOf(const UndoSessionReport& report);

} // namespace levain::editor
