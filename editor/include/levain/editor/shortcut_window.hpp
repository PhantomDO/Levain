#pragma once

// La fenêtre « Raccourcis » (ADR-0036, décision 13, morceau 16), que rien n'ouvre encore
// (`open`) : chaque action de la table, ses deux accords tels que le clavier d'aujourd'hui
// les écrit, son contexte ; un filtre par nom d'action ou par accord. **Un clic sur un accord, puis
// la touche** : la saisie (`capturing`) prend le premier appui qui compte. Un accord
// déjà pris est un conflit, nommé ; rien ne change avant « Remplacer ». La logique de la saisie
// (`stepCapture`, `acceptPending`) ne touche pas ImGui ; `drawShortcutWindow` n'est que le dessin.
// Équivalents : « Editor Preferences > Keyboard Shortcuts » d'Unreal, le Shortcuts Manager d'Unity,
// « Editor Settings > Shortcuts » de Godot.

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "levain/editor/menus.hpp"
#include "levain/editor/shortcut_edit.hpp"
#include "levain/editor/shortcuts.hpp"

namespace levain::editor
{

/// La clé de catalogue du titre de la fenêtre.
inline constexpr const char* ShortcutWindowTitle = "Raccourcis";

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

/// Où une ligne a été dessinée (le centre de ses boutons, en pixels de l'écran), pour que les tests
/// la cliquent à la souris scriptée.
struct ShortcutRowSeen
{
    Action action = Action::Count;
    std::array<ImVec2, 2> chordAt{};
    ImVec2 resetAt{};
    std::string chords; ///< Le texte des deux accords, comme la fenêtre l'écrit.
};

struct ShortcutWindow
{
    bool open = false;
    /// Le texte cherché, terminé par un zéro (le tampon du champ d'ImGui).
    std::array<char, 128> filter{};
    /// La saisie en cours.
    std::optional<ShortcutTarget> capturing;
    std::optional<PendingChord> pending;
    /// La fenêtre avait le focus à la dernière image qui l'a dessinée : la saisie ne lit le clavier
    /// que là (`captureAllowed`).
    bool focused = false;
    /// La clé de catalogue du dernier refus (F1, une touche sans nom), ou rien.
    const char* message = nullptr;
    bool resetAllAsked = false;
    /// Ce que la dernière image a dessiné : ce que les tests lisent.
    std::vector<ShortcutRowSeen> rows;
    std::string conflictShown; ///< « Ctrl+S : Enregistrer » tant qu'un conflit attend.
    ImVec2 filterAt{};
    ImVec2 resetAllAt{};
    ImVec2 replaceAt{};
    ImVec2 cancelAt{};
    ImVec2 confirmAllAt{}; ///< Le bouton de la confirmation de « Tout réinitialiser ».
    bool confirmShown = false;
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

/// Dit à la console chaque conflit de la table (`conflictsIn`), avec la cause (`when` : le fichier
/// relu, ou la disposition du clavier changée). **Rien n'est rebranché en silence** : les deux
/// actions gardent leur accord. Rend le nombre de conflits. `when` est une clé de catalogue.
std::size_t logShortcutConflicts(const ShortcutTable& table,
                                 const platform::KeyAtPosition& keyAtPosition, const char* when);

/// La ligne répond au filtre : le nom de l'action ou l'un de ses accords, sans tenir compte de la
/// casse des lettres ASCII.
[[nodiscard]] bool shortcutRowMatches(const HelpRow& row, std::string_view search);

/// Dessine la fenêtre si elle est ouverte. Vrai si la table a changé à cette image (la saisie
/// ou un bouton). `editing` : hors de l'Édition les
/// cases des accords sont grisées, les touches de l'éditeur n'arrivent plus à la saisie.
[[nodiscard]] bool drawShortcutWindow(ShortcutWindow& window, ShortcutTable& table,
                                      const platform::KeyAtPosition& keyAtPosition,
                                      std::span<const KeyPress> presses, bool editing);

} // namespace levain::editor
