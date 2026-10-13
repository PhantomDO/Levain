#include "levain/editor/window_menu.hpp"

#include <algorithm>

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

void saySoon(const char* milestone, SoonSide side)
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
    ImGui::SetTooltip("%s", ui::trf("Arrive en {}", milestone).c_str());
}

void drawWindowMenuItems(std::span<const WindowEntry> entries, WindowMenuLayout& layout)
{
    layout.itemsShown = false;
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
        ImGui::EndMenu();
    }
}

void drawWindowMenu(std::span<const WindowEntry> entries, WindowMenuLayout& layout)
{
    layout.itemsShown = false;
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }
    drawWindowMenuItems(entries, layout);
    ImGui::EndMainMenuBar();
}

} // namespace levain::editor
