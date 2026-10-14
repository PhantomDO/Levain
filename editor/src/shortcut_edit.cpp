#include "levain/editor/shortcut_edit.hpp"

#include <algorithm>
#include <utility>

#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

std::uint32_t lowerOf(std::uint32_t letter)
{
    return letter >= 'A' && letter <= 'Z' ? letter - 'A' + 'a' : letter;
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

} // namespace levain::editor
