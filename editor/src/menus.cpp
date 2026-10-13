#include "levain/editor/menus.hpp"

#include "levain/editor/console.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Les accords d'une entrée de la table, séparés par « / ».
std::string chordsText(const Binding& binding, const platform::KeyAtPosition& keyAtPosition)
{
    std::string text;
    for (const Chord& chord : binding.chords)
    {
        if (isBound(chord))
        {
            text += (text.empty() ? "" : " / ") + displayNameOf(chord, keyAtPosition);
        }
    }
    return text;
}

} // namespace

std::vector<HelpRow> helpRowsOf(const ShortcutTable& table,
                                const platform::KeyAtPosition& keyAtPosition)
{
    std::vector<HelpRow> rows;
    rows.reserve(table.bindings.size());
    for (const Binding& binding : table.bindings)
    {
        const Equivalents equivalents = equivalentsOf(binding.action);
        rows.push_back({.action = binding.action,
                        .name = ui::tr(nameOf(binding.action)),
                        .chords = chordsText(binding, keyAtPosition),
                        .context = ui::tr(contextNameOf(binding.context)),
                        .unreal = ui::tr(equivalents.unreal),
                        .unity = ui::tr(equivalents.unity)});
    }
    return rows;
}

bool helpRowMatches(const HelpRow& row, std::string_view search)
{
    return search.empty() || containsIgnoringAsciiCase(row.name, search) ||
           containsIgnoringAsciiCase(row.chords, search) ||
           containsIgnoringAsciiCase(row.context, search) ||
           containsIgnoringAsciiCase(row.unreal, search) ||
           containsIgnoringAsciiCase(row.unity, search);
}

} // namespace levain::editor
