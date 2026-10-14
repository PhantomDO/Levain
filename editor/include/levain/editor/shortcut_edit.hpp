#pragma once

// Changer la table des raccourcis (ADR-0036, décision 13, morceau 16) : la logique **sans ImGui**.
// Des fonctions libres dont toutes les dépendances sont des paramètres (ADR-0011) : la disposition
// du clavier en est une (`platform::KeyAtPosition`), jamais lue ici. Chaque piège porte son nom :
// `chordsCollide` (une position contre une lettre), `contextsOverlap` (global contre tout).
// Équivalents : « Editor Preferences > Keyboard Shortcuts » d'Unreal (un conflit y est averti), le
// « Shortcuts Manager » d'Unity (ses conflits sont listés, rien ne change sans choix), « Editor
// Settings > Shortcuts » de Godot.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "levain/editor/preferences.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/platform/input.hpp"

namespace levain::editor
{

/// Deux contextes se disputent un accord quand ils valent en même temps : le même, ou le global
/// (qui vaut sous tous les autres). Le vol et la Vue ne se disputent rien : W avance en vol et
/// prend l'outil Déplacer dans la Vue, par construction (la précédence de `actionFor`).
[[nodiscard]] constexpr bool contextsOverlap(Context a, Context b)
{
    return a == b || a == Context::Global || b == Context::Global;
}

/// Deux accords sont **la même touche avec les mêmes modificateurs**. Une position se compare à
/// une lettre par `keyAtPosition` : sur un AZERTY, la position du W (la touche marquée Z) est en
/// conflit avec la lettre Z, pas avec la lettre W. Un accord vide n'est en conflit avec rien.
[[nodiscard]] bool chordsCollide(const Chord& a, const Chord& b,
                                 const platform::KeyAtPosition& keyAtPosition);

/// Un accord pris par une autre action.
struct ChordConflict
{
    Action action = Action::Count;
    std::size_t slot = 0;
    Chord chord;
};

/// Les accords qui entrent en conflit avec `chord` posé sur l'une des places de `action` : dans le
/// même contexte, ou entre le global et n'importe quel autre. L'action elle-même ne compte pas,
/// ni sa place ni son autre place : reposer sur l'une l'accord de l'autre n'est pas un conflit,
/// `replaceChord` vide l'autre.
[[nodiscard]] std::vector<ChordConflict> conflictsOf(const ShortcutTable& table, Action action,
                                                     const Chord& chord,
                                                     const platform::KeyAtPosition& keyAtPosition);

/// « Ctrl+S : Enregistrer » : le conflit nommé, tel que la fenêtre le dit (accord selon la
/// disposition d'aujourd'hui, action dans la langue de l'interface).
[[nodiscard]] std::string conflictName(const ChordConflict& conflict,
                                       const platform::KeyAtPosition& keyAtPosition);

/// **« Remplacer »** (l'*Override* d'Unreal) : pose `chord` sur la place de l'action et le retire
/// à chaque action en conflit. Rend ce qui a été retiré (la console le dit). Une action qui
/// garde un seul accord le garde à la première place.
std::vector<ChordConflict> replaceChord(ShortcutTable& table, Action action, std::size_t slot,
                                        const Chord& chord,
                                        const platform::KeyAtPosition& keyAtPosition);

/// Ce que rendre ses défauts à l'action prendrait aux autres, sans rien changer : les défauts
/// qu'une autre action tient maintenant (reçus par « Remplacer »). Vide : « Réinitialiser » peut
/// agir.
[[nodiscard]] std::vector<ChordConflict>
resetConflictsOf(const ShortcutTable& table, Action action,
                 const platform::KeyAtPosition& keyAtPosition);

/// Rend ses accords par défaut à l'action. Si un défaut est pris par une autre action (qui l'a
/// reçu par « Remplacer »), il lui est retiré, comme `replaceChord` : rien ne reste en conflit
/// (`resetConflictsOf` le dit avant). Rend ce qui a été retiré aux autres.
std::vector<ChordConflict> resetAction(ShortcutTable& table, Action action,
                                       const platform::KeyAtPosition& keyAtPosition);

/// L'action a ses accords par défaut : « Réinitialiser » n'a rien à faire.
[[nodiscard]] bool isDefault(const ShortcutTable& table, Action action);

/// Toutes les actions rendent leurs défauts.
void resetAll(ShortcutTable& table);

/// Deux accords de la table qui se disputent la même touche : ce qu'on rencontre en relisant un
/// fichier écrit à la main, ou quand la disposition du clavier change (la position du W n'est
/// plus la lettre qu'elle était). Jamais rebranché en silence : la console le dit.
struct ConflictPair
{
    ChordConflict first;
    ChordConflict second;
};

[[nodiscard]] std::vector<ConflictPair> conflictsIn(const ShortcutTable& table,
                                                    const platform::KeyAtPosition& keyAtPosition);

/// Pourquoi une touche appuyée pendant la saisie n'est pas devenue l'accord.
enum class CaptureOutcome : std::uint8_t
{
    Chord,        ///< Un accord : la candidate.
    Cancel,       ///< Échap : la saisie s'arrête, Échap n'est jamais capturée.
    ModifierOnly, ///< Ctrl ou Maj seuls : on attend la touche qui les suit.
    Reserved,     ///< F1 seule : elle ouvre et ferme les panneaux (`togglesPanels`).
    Unbindable,   ///< Ce que `chordFromToken` refuse (une touche sans nom stable, Alt-Gr tenu).
};

struct CaptureResult
{
    CaptureOutcome outcome = CaptureOutcome::Unbindable;
    Chord chord;
};

/// **L'accord d'une touche appuyée pendant la saisie.** Il suit le genre par défaut de l'action
/// (`kind`) : une position garde le scancode, une lettre le caractère selon la disposition. Il doit
/// pouvoir s'écrire dans le fichier et s'y relire (`chordFromToken`), sinon il est refusé.
[[nodiscard]] CaptureResult captureChord(const KeyPress& press, ChordKind kind);

/// Le genre des accords d'une action : celui de son premier accord par défaut.
[[nodiscard]] ChordKind defaultKindOf(Action action);

/// Les lignes de `[raccourcis]` de la table : **seules les actions qui diffèrent des défauts**
/// s'écrivent (l'absence d'une action, ce sont les défauts), chacune avec ses accords en noms
/// stables (`tokenOf`). Une action sans accord s'écrit sans rien après le « = ».
[[nodiscard]] std::vector<ShortcutLine> linesOf(const ShortcutTable& table);

/// Ce que les lignes d'un fichier font aux défauts.
struct AppliedLines
{
    ShortcutTable table;
    /// Les lignes d'une action que ce code ne connaît pas (un fichier d'une autre branche) :
    /// gardées telles quelles pour que l'enregistrement ne les perde pas.
    std::vector<ShortcutLine> unknown;
    /// Les lignes d'une action connue qu'on n'a pas pu appliquer (accords illisibles, accord d'un
    /// autre genre que l'action) : les défauts restent, **mais la ligne est gardée telle quelle**
    /// pour que l'enregistrement ne perde pas ce qu'on a écrit à la main (`withKeptLines`).
    std::vector<ShortcutLine> refused;
    /// Ce qui a été refusé, une phrase par ligne : un accord illisible, une action écrite deux
    /// fois.
    std::vector<std::string> problems;
};

/// Les défauts, puis les lignes par-dessus. Une ligne fautive est écartée **et dite** (règle n°7) :
/// un accord illisible, ou d'un autre genre que celui de l'action (une lettre pour « Avancer »).
[[nodiscard]] AppliedLines shortcutsFromLines(const std::vector<ShortcutLine>& lines);

/// Les lignes à écrire (`linesOf`) suivies de celles qu'on a gardées (`unknown`, `refused`), sauf
/// celles d'une action que l'utilisateur a changée depuis : la ligne neuve l'emporte, sans quoi le
/// fichier relirait deux fois la même action.
[[nodiscard]] std::vector<ShortcutLine> withKeptLines(std::vector<ShortcutLine> written,
                                                      const std::vector<ShortcutLine>& kept);

} // namespace levain::editor
