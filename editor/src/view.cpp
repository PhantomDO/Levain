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

} // namespace

std::optional<Mode> drawView(View& view, app::App& app, Mode mode)
{
    std::optional<Mode> requestedMode;
    view.shown = false;
    view.focused = false;
    view.barDrawn = false;
    view.barHovered = false;
    view.image = {};
    bool asked = false;
    ImGui::SetNextWindowDockID(app.ui.dock.center, ImGuiCond_FirstUseEver);
    if (view.focusRequested)
    {
        ImGui::SetNextWindowFocus();
        view.focusRequested = false;
    }
    // Sans marge : l'image touche les bords de la fenêtre, et le rectangle de la scène est
    // celui du contenu.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(ui::labelOf(ViewWindow).c_str(), nullptr, viewFlags());
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
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            view.image = {min.x, min.y, max.x - min.x, max.y - min.y};
            app.sceneRect = view.image;
            requestedMode = drawModeBarOverlay(mode, min, view.barHovered);
            view.barDrawn = true;
            view.barAt = ImVec2(min.x + 8.0f, min.y + 8.0f);
        }
    }
    ImGui::End();
    if (!asked)
    {
        // Repliée ou derrière un onglet : 0 × 0, jamais « rien demandé » (la scène
        // reprendrait la fenêtre derrière les panneaux, et l'image HDR changerait de taille à
        // chaque bascule : engine/app/README.md).
        view.requested = {};
        std::ignore = app::requestScene(app, view.requested);
    }
    return requestedMode;
}

} // namespace levain::editor
