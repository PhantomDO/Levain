#include "levain/editor/mode.hpp"

#include <algorithm>
#include <tuple>

#include <imgui.h>

#include "levain/app/player_input.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Les drapeaux d'ImGui sont des `int` : les combiner en non signé, comme des bits qu'ils sont
/// (`bugprone-signed-bitwise`, comme `withFlags` de hierarchy.cpp).
int withFlags(int flags, int added)
{
    return static_cast<int>(static_cast<unsigned>(flags) | static_cast<unsigned>(added));
}

/// Les drapeaux de la fenêtre de la barre de mode, panneaux fermés : un bandeau que rien ne vise,
/// ne navigue ni ne focalise (`NoInputs`) : `sceneHasFocus` reste vrai et la route *jeu* aussi.
int modeBarFlags()
{
    return withFlags(
        withFlags(withFlags(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoMove),
                  withFlags(ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoDocking)),
        withFlags(withFlags(ImGuiWindowFlags_AlwaysAutoResize, ImGuiWindowFlags_NoFocusOnAppearing),
                  ImGuiWindowFlags_NoInputs));
}

} // namespace

bool stopPressed(ModeState& state, const input::RawInput& raw, const ShortcutTable& table)
{
    const Binding* stop = bindingOf(table, Action::Stop);
    const bool held = stop != nullptr && positionHeld(*stop, raw.keys);
    const bool rose = held && !state.stopHeld;
    state.stopHeld = held;
    return rose;
}

void enterMode(app::App& app, ModeState& state, Mode mode)
{
    state.current = mode;
    state.stopHeld = false;
    app.simulationPaused = simulationPausedIn(mode);
    app.mouseCaptureWanted = false;
    app.mouseCaptureOverPanels = false;
    // Le jeu repart de zéro : un `InputState` neuf, ses appuis en attente oubliés, et le
    // singleton relu (il vaut pour l'image en cours, où `frame` a peut-être déjà tourné).
    app.input = input::makeInputState(app.bindings);
    auto& playerInput = app.world.get_mut<app::PlayerInput>();
    app::forgetPresses(playerInput);
    app::takeFrameInput(playerInput, app.input);
    // La route définitive est posée à la fin de `ui` : celle-ci couvre l'image qui commence.
    app.inputRoute = inputRouteFor(mode, false);
}

namespace
{

/// Le contenu de la barre, dans la fenêtre ou le calque courant : le bouton qui montre où l'on va
/// (seul il va dans les deux sens, `modeRequested`), le bandeau de « Jouer (sans retour) » et le
/// rappel de la touche.
std::optional<Mode> drawModeBarContents(Mode current, bool interactive)
{
    std::optional<Mode> requested;
    if (interactive)
    {
        const Mode target = otherMode(current);
        const char* action = target == Mode::Edit ? "Revenir à l'édition" : "Jouer (sans retour)";
        if (ImGui::Button(ui::labelOf(action).c_str()))
        {
            requested = target;
        }
    }
    if (current == Mode::PlayWithoutReturn)
    {
        ImGui::TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "%s",
                           ui::tr("Jouer (sans retour) : la scène n'est pas restaurée à l'arrêt"));
    }
    ImGui::TextDisabled("%s", ui::tr(modeHintOf(current)));
    return requested;
}

} // namespace

void drawModeBar(Mode current, const app::ScreenRect& scene)
{
    ImGui::SetNextWindowPos({scene.x + 8.0f, scene.y + 8.0f});
    if (ImGui::Begin(ui::labelOf("Mode").c_str(), nullptr, modeBarFlags()))
    {
        std::ignore = drawModeBarContents(current, false);
    }
    ImGui::End();
}

std::optional<Mode> drawModeBarOverlay(Mode current, ImVec2 corner, bool& hovered)
{
    std::optional<Mode> requested;
    // Un enfant de la fenêtre de la Vue, non une fenêtre flottante : une fenêtre flottante passe
    // derrière la Vue dès que celle-ci prend le focus (`BringWindowToDisplayFront`), et la barre
    // disparaîtrait au premier clic sur la scène.
    ImGui::SetCursorScreenPos({corner.x + 8.0f, corner.y + 8.0f});
    const int childFlags = withFlags(ImGuiChildFlags_AutoResizeX, ImGuiChildFlags_AutoResizeY);
    // Un fond presque opaque : le rappel de la touche est en gris, illisible sur un ciel clair.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.78f));
    if (ImGui::BeginChild(ui::labelOf("Mode").c_str(), {0.0f, 0.0f},
                          withFlags(childFlags, ImGuiChildFlags_Borders),
                          ImGuiWindowFlags_NoScrollbar))
    {
        requested = drawModeBarContents(current, true);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    hovered = ImGui::IsItemHovered();
    return requested;
}

bool playShortcutPressed(const ShortcutTable& table)
{
    const Binding* play = bindingOf(table, Action::Play);
    if (play == nullptr)
    {
        return false;
    }
    // Route globale : l'accord marche quelle que soit la fenêtre qui a le focus (imgui.h,
    // `Shortcut`). Les deux accords de l'action, ceux de lettre seulement (`imguiChordOf`).
    return std::ranges::any_of(play->chords,
                               [](const Chord& chord)
                               {
                                   const auto imgui = imguiChordOf(chord);
                                   return imgui &&
                                          ImGui::Shortcut(*imgui, ImGuiInputFlags_RouteGlobal);
                               });
}

bool sceneHasFocus()
{
    return !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
}

} // namespace levain::editor
