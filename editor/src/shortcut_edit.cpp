#include "levain/editor/shortcut_edit.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

constexpr std::uint16_t ScanEscape = 41, ScanF1 = 58;
// Left Ctrl à Right GUI : les modificateurs seuls (USB HID 0xE0-0xE7), que `chordFromToken` refuse
// aussi ; les nommer ici dit pourquoi la saisie attend au lieu de se plaindre.
constexpr std::uint16_t FirstModifierScancode = 224, LastModifierScancode = 231;
constexpr std::uint32_t MaxAscii = 0x7F;

std::uint32_t lowerOf(std::uint32_t letter)
{
    return letter >= 'A' && letter <= 'Z' ? letter - 'A' + 'a' : letter;
}

bool hasModifier(const platform::KeyModifiers& modifiers)
{
    return modifiers != platform::KeyModifiers{};
}

/// **AltGr, tel qu'il arrive.** SDL le pose sous Linux (`altGr`), mais sous Windows il arrive comme
/// Ctrl+Alt, indiscernable d'une vraie paire : AltGr+E d'un AZERTY y devenait « Ctrl+Alt+E » alors
/// que Linux le refusait. Ctrl+Alt+lettre est refusée des deux côtés : la même frappe, le même
/// résultat, au prix d'un accord Ctrl+Alt+lettre que Linux aurait accepté.
bool typedWithAltGr(const KeyPress& press)
{
    const bool letter = (press.keycode >= 'a' && press.keycode <= 'z') ||
                        (press.keycode >= 'A' && press.keycode <= 'Z');
    return press.modifiers.altGr || (press.modifiers.ctrl && press.modifiers.alt && letter);
}

/// Ramène l'accord restant d'une action à la première place : une place vide n'est pas devant.
void packChords(Binding& binding)
{
    if (!isBound(binding.chords.at(0)) && isBound(binding.chords.at(1)))
    {
        std::swap(binding.chords.at(0), binding.chords.at(1));
    }
}

/// Les accords par défaut, construits une fois : `isDefault` est appelée pour chaque ligne de la
/// fenêtre, à chaque image.
const ShortcutTable& defaultTable()
{
    static const ShortcutTable defaults = defaultShortcuts();
    return defaults;
}

Binding* mutableBindingOf(ShortcutTable& table, Action action)
{
    const auto found = std::ranges::find(table.bindings, action, &Binding::action);
    return found == table.bindings.end() ? nullptr : &*found;
}

/// Retire ces accords à leurs actions, puis range les places : on vide tout avant de ranger, sinon
/// le rangement d'une première place changerait la place de la suivante.
void removeConflicts(ShortcutTable& table, const std::vector<ChordConflict>& conflicts)
{
    for (const ChordConflict& conflict : conflicts)
    {
        if (Binding* binding = mutableBindingOf(table, conflict.action))
        {
            binding->chords.at(conflict.slot) = {};
        }
    }
    for (const ChordConflict& conflict : conflicts)
    {
        if (Binding* binding = mutableBindingOf(table, conflict.action))
        {
            packChords(*binding);
        }
    }
}

} // namespace

bool chordsCollide(const Chord& a, const Chord& b, const platform::KeyAtPosition& keyAtPosition)
{
    if (!isBound(a) || !isBound(b) || !(a.modifiers == b.modifiers))
    {
        return false;
    }
    if (a.kind == b.kind)
    {
        return a.key == b.key;
    }
    const Chord& position = a.kind == ChordKind::Position ? a : b;
    const Chord& letter = a.kind == ChordKind::Position ? b : a;
    return lowerOf(keyAtPosition(static_cast<std::uint16_t>(position.key))) == letter.key;
}

std::vector<ChordConflict> conflictsOf(const ShortcutTable& table, Action action,
                                       const Chord& chord,
                                       const platform::KeyAtPosition& keyAtPosition)
{
    std::vector<ChordConflict> found;
    const Binding* own = bindingOf(table, action);
    if (own == nullptr || !isBound(chord))
    {
        return found;
    }
    for (const Binding& other : table.bindings)
    {
        for (std::size_t i = 0; i < other.chords.size(); ++i)
        {
            // Une action ne se dispute pas avec elle-même : ni sa place (`slot`), ni son autre
            // place, que `replaceChord` vide si l'accord posé est le sien.
            const bool itself = other.action == action;
            if (!itself && contextsOverlap(own->context, other.context) &&
                chordsCollide(chord, other.chords.at(i), keyAtPosition))
            {
                found.push_back({.action = other.action, .slot = i, .chord = other.chords.at(i)});
            }
        }
    }
    return found;
}

std::string conflictName(const ChordConflict& conflict,
                         const platform::KeyAtPosition& keyAtPosition)
{
    return displayNameOf(conflict.chord, keyAtPosition) + " : " + ui::tr(nameOf(conflict.action));
}

std::vector<ChordConflict> replaceChord(ShortcutTable& table, Action action, std::size_t slot,
                                        const Chord& chord,
                                        const platform::KeyAtPosition& keyAtPosition)
{
    Binding* binding = mutableBindingOf(table, action);
    if (binding == nullptr || slot >= binding->chords.size())
    {
        return {};
    }
    std::vector<ChordConflict> removed = conflictsOf(table, action, chord, keyAtPosition);
    removeConflicts(table, removed);
    // L'accord de l'autre place, s'il est le même, y serait en double : il part, l'action n'a
    // plus que la place qu'on vient de poser (et la rangée du dessous la ramène devant).
    Chord& otherSlot = binding->chords.at(binding->chords.size() - 1 - slot);
    if (chordsCollide(chord, otherSlot, keyAtPosition))
    {
        otherSlot = {};
    }
    binding->chords.at(slot) = chord;
    packChords(*binding);
    return removed;
}

std::vector<ChordConflict> resetConflictsOf(const ShortcutTable& table, Action action,
                                            const platform::KeyAtPosition& keyAtPosition)
{
    std::vector<ChordConflict> taken;
    const Binding* original = bindingOf(defaultTable(), action);
    if (original == nullptr || bindingOf(table, action) == nullptr)
    {
        return taken;
    }
    for (const Chord& chord : original->chords)
    {
        const auto found = conflictsOf(table, action, chord, keyAtPosition);
        taken.insert(taken.end(), found.begin(), found.end());
    }
    return taken;
}

std::vector<ChordConflict> resetAction(ShortcutTable& table, Action action,
                                       const platform::KeyAtPosition& keyAtPosition)
{
    std::vector<ChordConflict> removed = resetConflictsOf(table, action, keyAtPosition);
    const Binding* original = bindingOf(defaultTable(), action);
    if (Binding* binding = mutableBindingOf(table, action); binding != nullptr && original)
    {
        binding->chords = original->chords;
    }
    removeConflicts(table, removed);
    return removed;
}

bool isDefault(const ShortcutTable& table, Action action)
{
    const Binding* current = bindingOf(table, action);
    const Binding* original = bindingOf(defaultTable(), action);
    return current != nullptr && original != nullptr && current->chords == original->chords;
}

void resetAll(ShortcutTable& table)
{
    table = defaultShortcuts();
}

std::vector<ConflictPair> conflictsIn(const ShortcutTable& table,
                                      const platform::KeyAtPosition& keyAtPosition)
{
    std::vector<ConflictPair> pairs;
    for (const Binding& a : table.bindings)
    {
        for (std::size_t i = 0; i < a.chords.size(); ++i)
        {
            for (const ChordConflict& other :
                 conflictsOf(table, a.action, a.chords.at(i), keyAtPosition))
            {
                // Chaque paire une fois : l'ordre de la table décide qui est le premier.
                const auto here = std::pair{static_cast<int>(a.action), i};
                if (here < std::pair{static_cast<int>(other.action), other.slot})
                {
                    pairs.push_back(
                        {.first = {.action = a.action, .slot = i, .chord = a.chords.at(i)},
                         .second = other});
                }
            }
        }
    }
    return pairs;
}

CaptureResult captureChord(const KeyPress& typed, ChordKind kind, bool ignoresModifiers)
{
    if (typed.scancode == ScanEscape)
    {
        return {.outcome = CaptureOutcome::Cancel, .chord = {}, .modifiersDropped = false};
    }
    if (typed.scancode >= FirstModifierScancode && typed.scancode <= LastModifierScancode)
    {
        return {.outcome = CaptureOutcome::ModifierOnly, .chord = {}, .modifiersDropped = false};
    }
    // Avant le retrait des modificateurs : AltGr+E n'est pas « la touche E » même pour le vol.
    if (typedWithAltGr(typed))
    {
        return {.outcome = CaptureOutcome::Unbindable, .chord = {}, .modifiersDropped = false};
    }
    // Une action tenue ne lit aucun modificateur (`isHeldAction`) : l'accord est la touche nue, et
    // c'est elle que F1, le conflit et le fichier jugent.
    const KeyPress press{.scancode = typed.scancode,
                         .keycode = typed.keycode,
                         .modifiers =
                             ignoresModifiers ? platform::KeyModifiers{} : typed.modifiers};
    if (press.scancode == ScanF1 && press.modifiers == platform::KeyModifiers{})
    {
        return {.outcome = CaptureOutcome::Reserved, .chord = {}, .modifiersDropped = false};
    }
    // Un accord de lettre a un seul caractère ASCII : le keycode d'une touche sans lettre (une
    // flèche, un F5) porte le bit 30, et ne s'écrirait pas dans le fichier.
    if (kind == ChordKind::Letter && press.keycode >= MaxAscii)
    {
        return {.outcome = CaptureOutcome::Unbindable, .chord = {}, .modifiersDropped = false};
    }
    const Chord chord =
        kind == ChordKind::Position
            ? positionChord(press.scancode, press.modifiers)
            : letterChord(static_cast<char>(lowerOf(press.keycode)), press.modifiers);
    // Ce que le fichier saurait relire : un seul contrôle, celui du chargement.
    const bool storable = chordFromToken(tokenOf(chord)) == chord;
    return {.outcome = storable ? CaptureOutcome::Chord : CaptureOutcome::Unbindable,
            .chord = storable ? chord : Chord{},
            .modifiersDropped = storable && ignoresModifiers && hasModifier(typed.modifiers)};
}

ChordKind defaultKindOf(Action action)
{
    const Binding* binding = bindingOf(defaultTable(), action);
    return binding != nullptr ? binding->chords.front().kind : ChordKind::Position;
}

std::vector<ShortcutLine> linesOf(const ShortcutTable& table)
{
    std::vector<ShortcutLine> lines;
    for (const Binding& binding : table.bindings)
    {
        const Binding* original = bindingOf(defaultTable(), binding.action);
        if (original != nullptr && original->chords == binding.chords)
        {
            continue;
        }
        ShortcutLine line{.action = nameOf(binding.action), .chords = {}};
        for (const Chord& chord : binding.chords)
        {
            if (isBound(chord))
            {
                line.chords.push_back(tokenOf(chord));
            }
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

AppliedLines shortcutsFromLines(const std::vector<ShortcutLine>& lines)
{
    AppliedLines applied{.table = defaultShortcuts(), .unknown = {}, .refused = {}, .problems = {}};
    std::vector<Action> seen;
    for (const ShortcutLine& line : lines)
    {
        Binding* binding = nullptr;
        for (Binding& candidate : applied.table.bindings)
        {
            binding = line.action == nameOf(candidate.action) ? &candidate : binding;
        }
        if (binding == nullptr)
        {
            applied.unknown.push_back(line);
            continue;
        }
        if (std::ranges::contains(seen, binding->action))
        {
            applied.problems.push_back(
                ui::trf("{} : écrite deux fois, la seconde est ignorée", line.action));
            continue;
        }
        seen.push_back(binding->action);
        std::array<Chord, 2> chords{};
        bool readable = line.chords.size() <= chords.size();
        for (std::size_t i = 0; readable && i < line.chords.size(); ++i)
        {
            const auto chord = chordFromToken(line.chords.at(i));
            readable = chord.has_value();
            chords.at(i) = chord.value_or(Chord{});
        }
        if (!readable)
        {
            applied.problems.push_back(
                ui::trf("{} : accords illisibles, les défauts restent", line.action));
            applied.refused.push_back(line);
            continue;
        }
        // Le genre est celui de l'action : une lettre sur une action de position ne part jamais
        // (`positionHeld` ne lit que les positions), une position sur une action de lettre non
        // plus, et le menu l'afficherait comme un raccourci qui marche.
        const ChordKind kind = defaultKindOf(binding->action);
        const bool rightKind = std::ranges::all_of(
            chords, [kind](const Chord& chord) { return !isBound(chord) || chord.kind == kind; });
        if (!rightKind)
        {
            applied.problems.push_back(ui::trf(
                "{} : un accord n'est pas du genre de l'action ({}), les défauts restent",
                line.action, kind == ChordKind::Position ? ui::tr("position") : ui::tr("lettre")));
            applied.refused.push_back(line);
            continue;
        }
        // Une action tenue ne lit aucun modificateur (`positionHeld`) : l'accord serait affiché et
        // jamais lu comme tel, et il prendrait en silence la touche nue d'une autre action.
        const bool modified = isHeldAction(binding->action) &&
                              std::ranges::any_of(chords, [](const Chord& chord)
                                                  { return hasModifier(chord.modifiers); });
        if (modified)
        {
            applied.problems.push_back(
                ui::trf("{} : cette action se tient comme une touche, un accord à modificateur ne "
                        "serait jamais lu, les défauts restent",
                        line.action));
            applied.refused.push_back(line);
            continue;
        }
        binding->chords = chords;
    }
    return applied;
}

std::vector<ShortcutLine> withKeptLines(std::vector<ShortcutLine> written,
                                        const std::vector<ShortcutLine>& kept)
{
    for (const ShortcutLine& line : kept)
    {
        const bool taken = std::ranges::any_of(written, [&line](const ShortcutLine& other)
                                               { return other.action == line.action; });
        if (!taken)
        {
            written.push_back(line);
        }
    }
    return written;
}

std::vector<ShortcutLine> withoutLinesOf(std::vector<ShortcutLine> kept,
                                         std::span<const Action> written)
{
    std::erase_if(kept,
                  [written](const ShortcutLine& line)
                  {
                      return std::ranges::any_of(written, [&line](Action action)
                                                 { return line.action == nameOf(action); });
                  });
    return kept;
}

} // namespace levain::editor
