#include "levain/editor/quit.hpp"

#include <imgui.h>

#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

app::ScreenRect lastItemRect()
{
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    return {.x = min.x, .y = min.y, .width = max.x - min.x, .height = max.y - min.y};
}

} // namespace

void askToQuit(QuitState& state)
{
    state.asked = !state.open;
}

QuitAnswer drawQuitModal(QuitState& state)
{
    if (shouldOpenQuitModal(state))
    {
        ImGui::OpenPopup(ui::labelOf("Quitter l'éditeur ?").c_str());
        state.asked = false;
        state.open = true;
        ++state.timesOpened;
    }
    if (!state.open)
    {
        return QuitAnswer::Pending;
    }
    // Au centre de la fenêtre, comme les boîtes de dialogue des autres éditeurs.
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    QuitAnswer answer = QuitAnswer::Pending;
    if (ImGui::BeginPopupModal(ui::labelOf("Quitter l'éditeur ?").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(ui::tr("Fermer la fenêtre arrête l'éditeur."));
        if (ImGui::Button(ui::labelOf("Quitter").c_str()))
        {
            answer = QuitAnswer::Quit;
        }
        state.quitButton = lastItemRect();
        ImGui::SameLine();
        if (ImGui::Button(ui::labelOf("Annuler", "refuser").c_str()))
        {
            answer = QuitAnswer::Cancel;
        }
        state.cancelButton = lastItemRect();
        // Échap annule, comme toute boîte de dialogue ; en Édition il n'a rien d'autre à faire, et
        // en jeu l'UI garde le clavier tant que la modale est ouverte : il n'arrête pas le jeu.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            answer = QuitAnswer::Cancel;
        }
        if (answer != QuitAnswer::Pending)
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
    {
        answer = QuitAnswer::Cancel; // fermée d'ailleurs que par un bouton : on garde l'éditeur
    }
    if (answer != QuitAnswer::Pending)
    {
        state.open = false;
        state.quitButton = {};
        state.cancelButton = {};
    }
    return answer;
}

} // namespace levain::editor
