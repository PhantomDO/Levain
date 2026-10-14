#include "levain/editor/shortcut_edit.hpp"

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
            // place.
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
