#include "levain/editor/window_menu.hpp"

#include <algorithm>
#include <string>

#include <imgui.h>

#include "levain/ui/tr.hpp"

namespace levain::editor
{

ImVec2 centerOfLastItem()
{
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
}

namespace
{

/// La bulle de la dernière entrée, à son côté, si elle est survolée (grisée comprise).
void tooltipBeside(const std::string& text, SoonSide side)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        return;
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    constexpr float Gap = 6.0f;
    ImGui::SetNextWindowPos(side == SoonSide::Right ? ImVec2(max.x + Gap, min.y)
                                                    : ImVec2(min.x, max.y + Gap));
    ImGui::SetTooltip("%s", text.c_str());
}

} // namespace

void saySoon(const char* milestone, SoonSide side)
{
    tooltipBeside(ui::trf("Arrive en {}", milestone), side);
}

void sayReason(const char* french, SoonSide side)
{
    tooltipBeside(ui::tr(french), side);
}

void greyedMenuItem(const char* label, const char* shortcut)
{
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::MenuItem(label, shortcut, false, false);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

bool drawWindowMenuItems(std::span<const WindowEntry> entries, WindowMenuLayout& layout)
{
    layout.itemsShown = false;
    bool resetClicked = false;
    const bool opened = ImGui::BeginMenu(ui::labelOf("Fenêtre").c_str());
    layout.title = centerOfLastItem();
    if (opened)
    {
        layout.itemsShown = true;
        std::size_t index = 0;
        for (const WindowEntry& entry :
             entries.first(std::min(entries.size(), layout.items.size())))
        {
            ImGui::MenuItem(ui::tr(entry.french), nullptr, entry.open);
            layout.items.at(index++) = centerOfLastItem();
        }
        ImGui::Separator();
        resetClicked = ImGui::MenuItem(ui::tr("Réinitialiser la disposition"));
        layout.reset = centerOfLastItem();
        ImGui::EndMenu();
    }
    return resetClicked;
}

} // namespace levain::editor
