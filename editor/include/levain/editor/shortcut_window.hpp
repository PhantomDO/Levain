#pragma once

// La fenêtre « Raccourcis » (ADR-0036, décision 13, morceau 16) : son état, et la logique de sa
// saisie, sans ImGui. **Un clic sur un accord, puis la touche** : la saisie (`capturing`) prend le
// premier appui qui compte. Un accord déjà pris est un conflit, nommé ; rien ne change avant
// « Remplacer » (`stepCapture`, `acceptPending`). Le dessin s'y ajoutera.
// Équivalents : « Editor Preferences > Keyboard Shortcuts » d'Unreal, le Shortcuts Manager d'Unity,
// « Editor Settings > Shortcuts » de Godot.

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "levain/editor/shortcut_edit.hpp"
#include "levain/editor/shortcuts.hpp"

namespace levain::editor
{

/// Une place d'accord : l'action et laquelle de ses deux places.
struct ShortcutTarget
{
    Action action = Action::Count;
    std::size_t slot = 0;

    bool operator==(const ShortcutTarget&) const = default;
};

/// Ce qui attend un « Remplacer » ou un « Annuler » parce qu'un accord est pris : l'accord saisi
/// pour une place, ou, si `resets`, les accords par défaut d'une action (« Réinitialiser » ne
/// reprend rien en silence à celle qui les tient maintenant ; `target.action` est alors l'action).
struct PendingChord
{
    ShortcutTarget target;
    Chord chord;
    std::vector<ChordConflict> conflicts;
    bool resets = false;
};

struct ShortcutWindow
{
    /// La saisie en cours.
    std::optional<ShortcutTarget> capturing;
    std::optional<PendingChord> pending;
    /// La clé de catalogue du dernier refus (F1, une touche sans nom), ou rien.
    const char* message = nullptr;
};

/// Démarre la saisie de cette place : plus rien d'autre n'est en attente.
void beginCapture(ShortcutWindow& window, ShortcutTarget target);

/// Abandonne la saisie et la candidate en attente, sans toucher à la table : la fenêtre se ferme,
/// le mode change, le clavier est pris ailleurs. Une saisie qui survivrait couperait les
/// raccourcis sans que rien ne puisse plus la finir.
void abandonCapture(ShortcutWindow& window);

/// Où est le clavier à cette image : la saisie ne le lit que si rien d'autre ne l'a.
struct CaptureSurroundings
{
    bool editing = false;       ///< Mode Édition : hors de lui les touches ne lui parviennent pas.
    bool windowFocused = false; ///< La fenêtre « Raccourcis » a le focus (un onglet caché : non).
    bool textInput = false;     ///< Un champ de texte est actif, le filtre ou celui d'un autre.
    bool appFocused = false;    ///< L'application a le focus du système (Alt+Tab : non).
};

/// La saisie peut durer : sans quoi « s » tapé dans un champ deviendrait l'accord, et une saisie
/// laissée derrière un onglet, une modale ou une autre application couperait les raccourcis.
[[nodiscard]] constexpr bool captureAllowed(const CaptureSurroundings& around)
{
    return around.editing && around.windowFocused && !around.textInput && around.appFocused;
}

/// **Une image de saisie.** Le premier appui qui compte devient la candidate : sans conflit elle
/// est posée (vrai), avec un conflit elle attend (`pending`) et la table ne bouge pas. Échap
/// annule ; un modificateur seul attend la touche qui le suit ; F1 seule et ce qui ne s'écrirait
/// pas dans le fichier sont refusés avec leur message, et la saisie continue.
bool stepCapture(ShortcutWindow& window, ShortcutTable& table,
                 const platform::KeyAtPosition& keyAtPosition, std::span<const KeyPress> presses);

/// **« Réinitialiser »** : rend ses défauts à l'action. Si un défaut est pris par une autre action
/// (qui l'a reçu par « Remplacer »), rien ne change : le conflit est nommé et attend « Remplacer »
/// comme une saisie (`pending`). Vrai si la table a changé.
bool requestReset(ShortcutWindow& window, ShortcutTable& table, Action action,
                  const platform::KeyAtPosition& keyAtPosition);

/// « Remplacer » : pose la candidate (ou rend ses défauts à l'action) et la retire aux actions en
/// conflit. Rend ce qui a été retiré.
std::vector<ChordConflict> acceptPending(ShortcutWindow& window, ShortcutTable& table,
                                         const platform::KeyAtPosition& keyAtPosition);

/// La disposition du clavier a changé : une candidate en attente se refait sur la nouvelle, ou
/// est abandonnée avec son message (ses conflits ne sont plus ceux qu'on a lus).
void dropPendingAfterKeymapChange(ShortcutWindow& window);

} // namespace levain::editor
