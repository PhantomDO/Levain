#include "levain/editor/view.hpp"

#include <optional>
#include <tuple>

#include <imgui.h>

#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Les drapeaux de la fenêtre : la scène la remplit, sans défilement ni barre qui la rogneraient.
int viewFlags()
{
    return static_cast<int>(static_cast<unsigned>(ImGuiWindowFlags_NoScrollbar) |
                            static_cast<unsigned>(ImGuiWindowFlags_NoScrollWithMouse));
}

/// Le menu du clic droit, ouvert au relâchement d'un clic droit sur l'image **qui n'a pas volé**
/// (`View::menuAllowed`). Le relâchement d'un vol arrive à ImGui, que la capture ne prive jamais
/// des relâchements : sans ce garde, le menu s'ouvrirait à la fin de chaque vol (Unreal ne l'ouvre
/// que pour un clic droit sans glissé : supposé).
void drawContextMenu(View& view)
{
    constexpr const char* MenuId = "##menu-vue";
    if (view.menuAllowed && view.hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup(MenuId);
    }
    if (ImGui::BeginPopup(MenuId))
    {
        view.menuOpen = true;
        if (ImGui::MenuItem(ui::labelOf("Cadrer la sélection").c_str(), view.frameShortcut.c_str(),
                            false, view.canFrame))
        {
            view.frameRequested = true;
        }
        ImGui::EndPopup();
    }
}

} // namespace

std::optional<Mode> drawView(View& view, app::App& app, Mode mode)
{
    std::optional<Mode> requestedMode;
    view.shown = false;
    view.focused = false;
    view.barDrawn = false;
    view.barHovered = false;
    view.hovered = false;
    view.menuOpen = false;
    view.frameRequested = false;
    view.image = {};
    bool asked = false;
    if (view.open)
    {
        ImGui::SetNextWindowDockID(app.ui.dock.center, ImGuiCond_FirstUseEver);
        if (view.focusRequested)
        {
            ImGui::SetNextWindowFocus();
            view.focusRequested = false;
        }
        // Sans marge : l'image touche les bords de la fenêtre, et le rectangle de la scène est
        // celui du contenu.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool visible = ImGui::Begin(ui::labelOf(ViewWindow).c_str(), &view.open, viewFlags());
        ImGui::PopStyleVar();
        if (visible)
        {
            view.shown = true;
            view.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            const ImVec2 content = ImGui::GetContentRegionAvail();
            view.requested = sceneSizeOfContent(content.x, content.y);
            asked = true;
            if (const auto image = app::requestScene(app, view.requested))
            {
                ImGui::Image(*image, content);
                // Tout de suite : « survolé » est celui de l'image, avant que la barre ne soit un
                // item.
                // Menu ouvert, un clic droit glissé doit voler : sans le drapeau, la Vue n'est pas
                // « survolée » (le menu la bloque), le geste ne commence pas, et le menu se rouvre
                // au relâchement.
                const bool imageHovered =
                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                view.image = {min.x, min.y, max.x - min.x, max.y - min.y};
                app.sceneRect = view.image;
                requestedMode = drawModeBarOverlay(mode, min, view.barHovered, view.modeHint);
                view.barDrawn = true;
                view.barAt = ImVec2(min.x + 8.0f, min.y + 8.0f);
                view.hovered = imageHovered && !view.barHovered;
                drawContextMenu(view);
            }
        }
        ImGui::End();
    }
    if (!asked)
    {
        // Fermée, repliée ou derrière un onglet : 0 × 0, jamais « rien demandé » (la scène
        // reprendrait la fenêtre derrière les panneaux, et l'image HDR changerait de taille à
        // chaque bascule : engine/app/README.md).
        view.requested = {};
        std::ignore = app::requestScene(app, view.requested);
    }
    return requestedMode;
}

} // namespace levain::editor
