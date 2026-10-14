#include "levain/editor/show.hpp"

#include <imgui.h>

#include "levain/editor/window_menu.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

bool applyShowAction(ShowState& show, Action action)
{
    if (action != Action::GameView)
    {
        return false;
    }
    show.gameView = !show.gameView;
    return true;
}

bool drawShowMenu(ShowState& show, ImVec2 corner, const std::string& gameViewShortcut,
                  ShowMenuLayout& layout)
{
    constexpr const char* BarId = "##barre-afficher";
    constexpr const char* PopupId = "##menu-afficher";
    layout.open = false;
    bool gameViewClicked = false;
    bool popupHovered = false;
    // Comme la barre de mode : un enfant de la Vue, non une fenêtre flottante, qui passerait
    // derrière la Vue dès qu'elle prend le focus.
    ImGui::SetCursorScreenPos(corner);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.78f));
    const int flags = static_cast<int>(static_cast<unsigned>(ImGuiChildFlags_AutoResizeX) |
                                       static_cast<unsigned>(ImGuiChildFlags_AutoResizeY) |
                                       static_cast<unsigned>(ImGuiChildFlags_Borders));
    if (ImGui::BeginChild(BarId, {0.0f, 0.0f}, flags, ImGuiWindowFlags_NoScrollbar))
    {
        if (ImGui::Button(ui::labelOf("Afficher").c_str()))
        {
            ImGui::OpenPopup(PopupId);
        }
        layout.button = centerOfLastItem();
        if (ImGui::BeginPopup(PopupId))
        {
            layout.open = true;
            popupHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
            if (ImGui::MenuItem(ui::labelOf("Grille").c_str(), nullptr, show.grid))
            {
                show.grid = !show.grid;
            }
            layout.gridItem = centerOfLastItem();
            gameViewClicked = ImGui::MenuItem(ui::labelOf("Vue de jeu").c_str(),
                                              gameViewShortcut.c_str(), show.gameView);
            layout.gameViewItem = centerOfLastItem();
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    const ImVec2 barMin = ImGui::GetItemRectMin();
    const ImVec2 barMax = ImGui::GetItemRectMax();
    layout.bar = {barMin.x, barMin.y, barMax.x - barMin.x, barMax.y - barMin.y};
    // La souris sur le menu ouvert n'est pas sur la scène non plus (le menu déborde sur l'image).
    layout.hovered = ImGui::IsItemHovered() || popupHovered;
    return gameViewClicked;
}

} // namespace levain::editor
