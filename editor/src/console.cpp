#include "levain/editor/console.hpp"

#include <algorithm>
#include <cctype>
#include <format>

#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Un pixel : voir `shouldFollowTail`.
constexpr float FollowTolerance = 1.0f;

char lowerAscii(char letter)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
}

bool levelShown(const core::LogEntry& entry, const ConsoleFilter& filter)
{
    switch (core::severityOf(entry.level))
    {
    case core::LogSeverity::Info:
        return filter.info;
    case core::LogSeverity::Warning:
        return filter.warning;
    case core::LogSeverity::Error:
        return filter.error;
    }
    return true;
}

bool isSameMessage(const core::LogEntry& a, const core::LogEntry& b)
{
    return a.level == b.level && a.category == b.category && a.text == b.text;
}

} // namespace

bool containsIgnoringAsciiCase(std::string_view text, std::string_view needle)
{
    if (needle.empty())
    {
        return true;
    }
    return !std::ranges::search(text, needle,
                                [](char a, char b) { return lowerAscii(a) == lowerAscii(b); })
                .empty();
}

bool isShown(const core::LogEntry& entry, const ConsoleFilter& filter)
{
    return levelShown(entry, filter) && (containsIgnoringAsciiCase(entry.text, filter.search) ||
                                         containsIgnoringAsciiCase(entry.category, filter.search));
}

void listConsoleRows(std::span<const core::LogEntry> entries, const ConsoleFilter& filter,
                     bool collapse, std::vector<ConsoleRow>& rows)
{
    rows.clear();
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (!isShown(entries[i], filter))
        {
            continue;
        }
        if (collapse && !rows.empty() && isSameMessage(entries[rows.back().entry], entries[i]))
        {
            ++rows.back().repeats;
            continue;
        }
        rows.push_back({.entry = i, .repeats = 1});
    }
}

bool shouldFollowTail(float scrollY, float scrollMax)
{
    return scrollY >= scrollMax - FollowTolerance;
}

bool fitsOnLine(float previousEnd, float spacing, float itemWidth, float rightEdge)
{
    return previousEnd + spacing + itemWidth <= rightEdge;
}

std::array<std::string, 3> filterLabels(const core::LogCounts& counts)
{
    return {ui::trf("Infos : {}", counts.infos), ui::trf("Avertissements : {}", counts.warnings),
            ui::trf("Erreurs : {}", counts.errors)};
}

std::string singleLine(std::string_view text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    {
        text.remove_suffix(1);
    }
    std::string line;
    for (const char letter : text)
    {
        if (letter == '\n')
        {
            line += " | ";
        }
        else if (letter != '\r')
        {
            line += letter;
        }
    }
    return line;
}

std::string lineOf(const core::LogEntry& entry, std::size_t repeats)
{
    std::string line =
        std::format("[{:8.3f}] [{}] {}", entry.seconds, entry.category, singleLine(entry.text));
    if (repeats > 1)
    {
        line += std::format(" (×{})", repeats);
    }
    return line;
}

} // namespace levain::editor
