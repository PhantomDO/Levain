#include "levain/editor/menus.hpp"

#include <algorithm>

#include <imgui.h>

#include "levain/editor/actions.hpp"
#include "levain/editor/console.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Les accords d'une entrée de la table, séparés par « / », ou « (aucun) ».
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
    // Une action sans accord le dit : une cellule vide ne dit pas si l'action en a un. La fenêtre
    // « Raccourcis » écrit « (aucun) » pour chaque place vide, l'Aide doit s'accorder avec elle.
    return text.empty() ? std::string{ui::tr("(aucun)")} : text;
}

/// Le raccourci qu'une entrée de menu montre : le premier accord de l'action, selon la disposition.
std::string shortcutTextOf(const ShortcutTable& table, Action action,
                           const platform::KeyAtPosition& keyAtPosition)
{
    const Binding* binding = bindingOf(table, action);
    return binding != nullptr && isBound(binding->chords.front())
               ? displayNameOf(binding->chords.front(), keyAtPosition)
               : std::string{};
}

/// L'entrée de menu d'une action de la table : son nom, son raccourci, grisée tant que son
/// milestone n'est pas là. Vrai au clic d'une entrée qui est là.
bool actionItem(Action action, const MenuBarInputs& inputs)
{
    const Availability availability = availabilityOf(action);
    const std::string shortcut = shortcutTextOf(inputs.table, action, inputs.keyAtPosition);
    const std::string label = ui::labelOf(nameOf(action));
    if (availability.available)
    {
        return ImGui::MenuItem(label.c_str(), shortcut.c_str());
    }
    greyedMenuItem(label.c_str(), shortcut.c_str());
    saySoon(availability.milestone, SoonSide::Right);
    return false;
}

/// Annuler ou Rétablir : active quand la pile et le mode le permettent, sinon grisée et la raison
/// dite au survol (hors de l'Édition la pile est gelée, ADR-0038, décision 9). Vrai au clic.
bool historyItem(Action action, const MenuBarInputs& inputs, bool& enabled)
{
    const HistoryRefusal refusal =
        action == Action::Undo ? inputs.history.undo : inputs.history.redo;
    enabled = refusal == HistoryRefusal::None;
    if (enabled)
    {
        return actionItem(action, inputs);
    }
    greyedMenuItem(ui::labelOf(nameOf(action)).c_str(),
                   shortcutTextOf(inputs.table, action, inputs.keyAtPosition).c_str());
    sayReason(historyReasonOf(refusal, action == Action::Redo), SoonSide::Right);
    return false;
}

void drawFileMenu(const MenuBarInputs& inputs, MenuBarLayout& layout, MenuRequests& requests)
{
    const bool opened = ImGui::BeginMenu(ui::labelOf("Fichier").c_str());
    layout.file = centerOfLastItem();
    layout.fileOpen = opened;
    if (!opened)
    {
        return;
    }
    if (actionItem(Action::Save, inputs))
    {
        requests.actions.push_back(Action::Save);
    }
    layout.save = centerOfLastItem();
    ImGui::Separator();
    // Quitter ne passe pas par la table : il n'a pas de raccourci (la croix de la fenêtre et sa
    // modale sont celles du morceau 13).
    requests.quit = ImGui::MenuItem(ui::labelOf("Quitter l'éditeur").c_str());
    layout.quit = centerOfLastItem();
    ImGui::EndMenu();
}

void drawEditMenu(const MenuBarInputs& inputs, MenuBarLayout& layout, MenuRequests& requests)
{
    const bool opened = ImGui::BeginMenu(ui::labelOf("Édition").c_str());
    layout.edit = centerOfLastItem();
    layout.editOpen = opened;
    if (!opened)
    {
        return;
    }
    for (const Action action : {Action::Undo, Action::Redo})
    {
        bool enabled = false;
        if (historyItem(action, inputs, enabled))
        {
            requests.actions.push_back(action);
        }
        (action == Action::Undo ? layout.undo : layout.redo) = centerOfLastItem();
        (action == Action::Undo ? layout.undoEnabled : layout.redoEnabled) = enabled;
    }
    ImGui::Separator();
    const bool preferences = ImGui::BeginMenu(ui::labelOf("Préférences").c_str());
    layout.preferences = centerOfLastItem();
    if (preferences)
    {
        layout.preferencesOpen = true;
        requests.openShortcuts = ImGui::MenuItem(ui::labelOf("Raccourcis…").c_str());
        layout.shortcutsEntry = centerOfLastItem();
        ImGui::EndMenu();
    }
    ImGui::EndMenu();
}

void drawEntityMenu(const MenuBarInputs& inputs, MenuBarLayout& layout, MenuRequests& requests)
{
    const bool opened = ImGui::BeginMenu(ui::labelOf("Entité").c_str());
    layout.entity = centerOfLastItem();
    layout.entityOpen = opened;
    if (!opened)
    {
        return;
    }
    // Une racine, en renommage (Unity : GameObject > Create Empty, documenté [U12]) ; sur une ligne
    // de la Hiérarchie, le menu contextuel crée un enfant.
    if (actionItem(Action::CreateEntity, inputs))
    {
        requests.actions.push_back(Action::CreateEntity);
    }
    layout.create = centerOfLastItem();
    ImGui::EndMenu();
}

void drawHelpMenu(MenuBarLayout& layout, HelpState& help)
{
    const bool opened = ImGui::BeginMenu(ui::labelOf(HelpWindow).c_str());
    layout.help = centerOfLastItem();
    layout.helpOpen = opened;
    if (!opened)
    {
        return;
    }
    ImGui::MenuItem(ui::labelOf("Raccourcis").c_str(), nullptr, &help.open);
    layout.helpShortcuts = centerOfLastItem();
    ImGui::EndMenu();
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

MenuRequests drawMenuBar(const MenuBarInputs& inputs, WindowMenuLayout& windowLayout,
                         MenuBarLayout& layout, HelpState& help)
{
    MenuRequests requests;
    layout.fileOpen = false;
    layout.editOpen = false;
    layout.preferencesOpen = false;
    layout.entityOpen = false;
    layout.helpOpen = false;
    windowLayout.itemsShown = false;
    if (!ImGui::BeginMainMenuBar())
    {
        return requests;
    }
    drawFileMenu(inputs, layout, requests);
    drawEditMenu(inputs, layout, requests);
    drawEntityMenu(inputs, layout, requests);
    requests.resetLayout = drawWindowMenuItems(inputs.windows, windowLayout);
    drawHelpMenu(layout, help);
    // Le « * » de la scène modifiée (le titre de la fenêtre reste ASCII, ADR-0036).
    layout.modifiedShown = inputs.history.modified;
    if (inputs.history.modified)
    {
        ImGui::TextUnformatted("*");
    }
    ImGui::EndMainMenuBar();
    return requests;
}

void drawHelp(HelpState& help, const MenuBarInputs& inputs)
{
    help.shown.clear();
    help.total = 0;
    if (!help.open)
    {
        return;
    }
    // 900 x 480 : cinq colonnes (dont deux phrases, Unreal et Unity) sans rien couper à la première
    // ouverture ; avec 620 x 360 elles s'étiraient et rognaient le texte.
    ImGui::SetNextWindowSize({900.0f, 480.0f}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ui::labelOf(HelpWindow).c_str(), &help.open))
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##recherche", ui::tr("Chercher un raccourci"), help.search.data(),
                                 help.search.size());
        help.searchAt = centerOfLastItem();
        const std::vector<HelpRow> rows = helpRowsOf(inputs.table, inputs.keyAtPosition);
        help.total = rows.size();
        constexpr auto Flags =
            static_cast<ImGuiTableFlags>(static_cast<unsigned>(ImGuiTableFlags_RowBg) |
                                         static_cast<unsigned>(ImGuiTableFlags_ScrollY) |
                                         static_cast<unsigned>(ImGuiTableFlags_Resizable) |
                                         static_cast<unsigned>(ImGuiTableFlags_SizingFixedFit));
        if (ImGui::BeginTable("##raccourcis", 5, Flags))
        {
            for (const char* column : {"Action", "Raccourci", "Contexte", "Unreal", "Unity"})
            {
                ImGui::TableSetupColumn(ui::labelOf(column).c_str());
            }
            ImGui::TableHeadersRow();
            for (const HelpRow& row : rows)
            {
                if (!helpRowMatches(row, help.search.data()))
                {
                    continue;
                }
                help.shown.push_back(row);
                ImGui::TableNextRow();
                for (const std::string* cell :
                     {&row.name, &row.chords, &row.context, &row.unreal, &row.unity})
                {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(cell->c_str());
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace levain::editor
