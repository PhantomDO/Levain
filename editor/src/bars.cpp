#include "levain/editor/bars.hpp"

#include <algorithm>

#include <imgui.h>
// BeginViewportSideBar : une bande qui retire sa hauteur à l'espace d'ancrage (imgui_internal.h),
// comme la barre de menus. Il n'a pas d'équivalent public.
#include <imgui_internal.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/actions.hpp"
#include "levain/editor/camera.hpp"
#include "levain/editor/console.hpp"
#include "levain/editor/window_menu.hpp"
#include "levain/ui/flags.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Un contrôle grisé jusqu'à son milestone : sans fond ni contour et en texte éteint, donc
/// visiblement autre chose qu'un bouton qui répond (le fond atténué seul se lisait « cliquable »),
/// et qui dit au survol quand il arrive.
///
/// **Dessiné tel que le test le mesure** : `BeginDisabled` multiplierait le texte, déjà éteint
/// (`TextDisabled`), par `DisabledAlpha` : à 0,6, un contraste de 2,6 contre la barre. Le grisé est
/// donc celui de la couleur, à un alpha de 1 (`ImGuiStyleVar_DisabledAlpha`, lu à l'appel de
/// `BeginDisabled`), et le contraste de `TextDisabled` est celui que `tests/ui_style_test.cpp`
/// tient. Le contour (`Border`, `FrameBorderSize`) redessinerait la forme qu'on a retirée : rien
/// à encadrer, il resterait pour les champs de saisie.
ImVec2 disabledButton(const char* french, const char* milestone)
{
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Button(ui::labelOf(french).c_str());
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    saySoon(milestone, SoonSide::Below);
    return centerOfLastItem();
}

} // namespace

ToolbarResult drawToolbar(Mode mode, float flySpeed, bool realTime, ToolbarLayout& layout)
{
    ToolbarResult result;
    const int flags =
        ui::withFlags(ui::withFlags(ImGuiWindowFlags_NoScrollbar, ImGuiWindowFlags_NoSavedSettings),
                      ImGuiWindowFlags_NoDecoration);
    if (!ImGui::BeginViewportSideBar(
            ui::labelOf("Barre d'outils").c_str(), ImGui::GetMainViewport(), ImGuiDir_Up,
            ImGui::GetFrameHeight() + 2.0f * ImGui::GetStyle().WindowPadding.y, flags))
    {
        ImGui::End();
        return result;
    }
    // Le mode d'abord : celui qu'on cherche le plus.
    const Mode target = otherMode(mode);
    if (ImGui::Button(modeButtonLabel(target).c_str()))
    {
        result.mode = target;
    }
    layout.mode = centerOfLastItem();
    ImGui::SameLine();
    layout.simulate = disabledButton("Simuler", availabilityOf(Action::Simulate).milestone);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // Les trois outils, tous grisés jusqu'à M7.4.
    layout.tool = disabledButton("Déplacer", availabilityOf(Action::ToolMove).milestone);
    ImGui::SameLine();
    disabledButton("Tourner", availabilityOf(Action::ToolRotate).milestone);
    ImGui::SameLine();
    disabledButton("Mettre à l'échelle", availabilityOf(Action::ToolScale).milestone);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(ui::tr("Vitesse de la caméra"));
    ImGui::SameLine();
    layout.speedWidth = 8.0f * ImGui::GetFontSize();
    ImGui::SetNextItemWidth(layout.speedWidth);
    float speed = flySpeed;
    // Logarithmique : 1 → 2 compte plus que 100 → 101 (`FlySpeedPerNotch`). Borné par la caméra :
    // le champ de saisie (Ctrl+clic) laisse taper n'importe quoi.
    if (ImGui::SliderFloat("##vitesse", &speed, MinFlySpeed, MaxFlySpeed, "%.1f",
                           ImGuiSliderFlags_Logarithmic))
    {
        result.flySpeed = clampFlySpeed(speed);
    }
    layout.speed = centerOfLastItem();
    ImGui::SameLine();
    bool realTimeChecked = realTime;
    // Hors de l'Édition la case est grisée, cochée : jouer force le temps réel (`switchRealTime`).
    ImGui::BeginDisabled(mode != Mode::Edit);
    if (ImGui::Checkbox(ui::labelOf("Temps réel").c_str(), &realTimeChecked))
    {
        result.actions.push_back(
            Action::RealTime); // la case bascule : l'action est la même que Ctrl+R
    }
    ImGui::EndDisabled();
    layout.realTime = centerOfLastItem();
    ImGui::End();
    return result;
}

StatusLine statusOf(const core::LogRing& ring)
{
    StatusLine line{
        .text = {}, .severity = core::LogSeverity::Info, .counts = core::countsOf(ring)};
    if (const auto last = core::lastEntryOf(ring))
    {
        line.text = singleLine(last->text);
        line.severity = core::severityOf(last->level);
    }
    return line;
}

void drawStatusBar(const StatusLine& line)
{
    const int flags =
        ui::withFlags(ui::withFlags(ImGuiWindowFlags_NoScrollbar, ImGuiWindowFlags_NoSavedSettings),
                      ImGuiWindowFlags_NoDecoration);
    if (ImGui::BeginViewportSideBar(
            ui::labelOf("Barre d'état").c_str(), ImGui::GetMainViewport(), ImGuiDir_Down,
            ImGui::GetFrameHeight() + 2.0f * ImGui::GetStyle().WindowPadding.y, flags))
    {
        const ImVec4 color =
            line.severity == core::LogSeverity::Error     ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
            : line.severity == core::LogSeverity::Warning ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f)
                                                          : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        // Les comptes à droite : de quoi voir d'un coup d'œil qu'une erreur est passée.
        const std::string counts =
            ui::trf("Avertissements : {}   Erreurs : {}", line.counts.warnings, line.counts.errors);
        const float countsWidth = ImGui::CalcTextSize(counts.c_str()).x;
        const float countsX =
            ImGui::GetWindowWidth() - countsWidth - ImGui::GetStyle().WindowPadding.x;
        // Le message est rogné avant les comptes : un message long ne passe pas dessous (un
        // rectangle de découpe, pas de retour à la ligne : la barre fait une ligne).
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float room = countsX - ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushClipRect(
            start, {start.x + std::max(room, 0.0f), start.y + ImGui::GetTextLineHeight()}, true);
        ImGui::TextColored(color, "%s", line.text.c_str());
        ImGui::PopClipRect();
        ImGui::SameLine(countsX);
        ImGui::TextUnformatted(counts.c_str());
    }
    ImGui::End();
}

} // namespace levain::editor
