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

namespace
{

ImVec2 centerOfLastItem()
{
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    return {(low.x + high.x) / 2.0f, (low.y + high.y) / 2.0f};
}

ConsoleFilter filterOf(const Console& console)
{
    return {.info = console.info,
            .warning = console.warning,
            .error = console.error,
            .search = std::string_view{console.search.data()}};
}

/// Recopie l'anneau s'il a bougé : une copie par message reçu, pas par image.
void refreshSnapshot(Console& console, const core::LogRing& ring)
{
    if (core::generationOf(ring) != console.snapshot.generation)
    {
        console.snapshot = core::snapshotOf(ring);
    }
}

/// La largeur qu'ImGui donne à une case (`Checkbox`, imgui_widgets.cpp) : le carré, l'espace, le
/// texte sans ce qui suit « ## ».
float checkboxWidth(const char* label)
{
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
           ImGui::CalcTextSize(label, nullptr, true).x;
}

/// Reste sur la ligne de l'élément précédent s'il y tient, sinon va à la ligne (la barre d'outils
/// s'enroule dans une colonne étroite, voir `fitsOnLine`).
void sameLineIfFits(float itemWidth, float rightEdge)
{
    if (fitsOnLine(ImGui::GetItemRectMax().x, ImGui::GetStyle().ItemSpacing.x, itemWidth,
                   rightEdge))
    {
        ImGui::SameLine();
    }
}

/// Une case à cocher dont le libellé change avec le compte et l'identifiant non (`id` commence par
/// « ### »).
void drawFilterBox(const std::string& label, const char* id, bool& flag, ImVec2& center,
                   float rightEdge)
{
    const std::string text = label + id;
    sameLineIfFits(checkboxWidth(text.c_str()), rightEdge);
    ImGui::Checkbox(text.c_str(), &flag);
    center = centerOfLastItem();
}

void drawToolbar(Console& console, core::LogRing& ring)
{
    const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(ui::labelOf("Effacer").c_str()))
    {
        core::clearLogRing(ring);
        // Dans l'image même : les comptes et la liste de cette image sont ceux d'après
        // l'effacement.
        refreshSnapshot(console, ring);
    }
    console.layout.clear = centerOfLastItem();
    const std::string collapse = ui::labelOf("Réduire");
    sameLineIfFits(checkboxWidth(collapse.c_str()), rightEdge);
    ImGui::Checkbox(collapse.c_str(), &console.collapse);
    console.layout.collapse = centerOfLastItem();
    console.labels = filterLabels(console.snapshot.counts);
    drawFilterBox(console.labels[0], "###console.infos", console.info, console.layout.info,
                  rightEdge);
    drawFilterBox(console.labels[1], "###console.avertissements", console.warning,
                  console.layout.warning, rightEdge);
    drawFilterBox(console.labels[2], "###console.erreurs", console.error, console.layout.error,
                  rightEdge);
    // Le champ sur sa propre ligne, toute la largeur : dans la colonne étroite où la console
    // s'ancre pour l'instant, la place qui reste sur la ligne des cases serait nulle.
    console.layout.searchWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(console.layout.searchWidth);
    ImGui::InputTextWithHint("##recherche", ui::tr("Rechercher"), console.search.data(),
                             console.search.size());
    console.layout.search = centerOfLastItem();
}

ImVec4 colorOf(core::LogLevel level)
{
    switch (core::severityOf(level))
    {
    case core::LogSeverity::Info:
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    case core::LogSeverity::Warning:
        return {1.0f, 0.8f, 0.2f, 1.0f};
    case core::LogSeverity::Error:
        return {1.0f, 0.35f, 0.35f, 1.0f};
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

void drawRows(Console& console)
{
    // La console garde la police à chasse fixe d'ImGui (ProggyForever, ADR-0036, décision 15) quand
    // l'interface passe à Inter : des colonnes alignées, et Inter derrière pour les « œ » des
    // messages. Taille inchangée (0).
    ImGui::PushFont(console.font, 0.0f);
    if (ImGui::BeginChild("##lignes", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar))
    {
        console.layout.list = {ImGui::GetWindowPos().x + (ImGui::GetWindowSize().x / 2.0f),
                               ImGui::GetWindowPos().y + (ImGui::GetWindowSize().y / 2.0f)};
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(console.rows.size()));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const ConsoleRow& row = console.rows.at(static_cast<std::size_t>(index));
                const core::LogEntry& entry = console.snapshot.entries.at(row.entry);
                ImGui::PushStyleColor(ImGuiCol_Text, colorOf(entry.level));
                ImGui::TextUnformatted(lineOf(entry, row.repeats).c_str());
                ImGui::PopStyleColor();
            }
        }
        // ImGui connaît la hauteur de la liste à l'image d'avant : on y est si on était au bout, et
        // le message arrivé n'a pas encore allongé le maximum (imgui.h, `SetScrollHereY`).
        console.scrollY = ImGui::GetScrollY();
        console.scrollMax = ImGui::GetScrollMaxY();
        if (shouldFollowTail(console.scrollY, console.scrollMax))
        {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::PopFont();
}

} // namespace

void drawConsole(Console& console, core::LogRing& ring, ImGuiID dock, bool* open)
{
    if (open != nullptr && !*open)
    {
        return; // fermée par le menu « Fenêtre » : ni fenêtre ni onglet
    }
    ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ui::labelOf(ConsoleWindow).c_str(), open))
    {
        refreshSnapshot(console, ring);
        drawToolbar(console, ring);
        listConsoleRows(console.snapshot.entries, filterOf(console), console.collapse,
                        console.rows);
        drawRows(console);
    }
    ImGui::End();
}

} // namespace levain::editor
