#include "levain/editor/mode.hpp"

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

/// Les drapeaux de la fenêtre de la barre de mode. Sans bouton (panneaux fermés), un bandeau que
/// rien ne vise, ne navigue ni ne focalise : `sceneHasFocus` reste vrai et la route *jeu* aussi.
/// Avec le bouton, elle ne prend pas non plus le focus à son apparition, seulement à un clic.
int modeBarFlags(bool interactive)
{
    const int base = withFlags(
        withFlags(withFlags(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoMove),
                  withFlags(ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoDocking)),
        withFlags(ImGuiWindowFlags_AlwaysAutoResize, ImGuiWindowFlags_NoFocusOnAppearing));
    return interactive ? base : withFlags(base, ImGuiWindowFlags_NoInputs);
}

} // namespace

bool stopPressed(ModeState& state, const input::RawInput& raw)
{
    const bool held = raw.keys.test(StopScancode);
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
    // Le jeu repart de zéro : un `InputState` neuf, ses appuis en attente oubliés, et le
    // singleton relu (il vaut pour l'image en cours, où `frame` a peut-être déjà tourné).
    app.input = input::makeInputState(app.bindings);
    auto& playerInput = app.world.get_mut<app::PlayerInput>();
    app::forgetPresses(playerInput);
    app::takeFrameInput(playerInput, app.input);
    // La route définitive est posée à la fin de `ui` : celle-ci couvre l'image qui commence.
    app.inputRoute = inputRouteFor(mode, false);
}

std::optional<Mode> drawModeBar(Mode current, const app::ScreenRect& scene, bool interactive)
{
    std::optional<Mode> requested;
    ImGui::SetNextWindowPos({scene.x + 8.0f, scene.y + 8.0f});
    if (ImGui::Begin(ui::labelOf("Mode").c_str(), nullptr, modeBarFlags(interactive)))
    {
        if (interactive)
        {
            // Le bouton montre où l'on va ; seul il va dans les deux sens (`modeRequested`).
            const Mode target = otherMode(current);
            const char* action =
                target == Mode::Edit ? "Revenir à l'édition" : "Jouer (sans retour)";
            if (ImGui::Button(ui::labelOf(action).c_str()))
            {
                requested = target;
            }
        }
        if (current == Mode::PlayWithoutReturn)
        {
            ImGui::TextColored(
                {1.0f, 0.75f, 0.2f, 1.0f}, "%s",
                ui::tr("Jouer (sans retour) : la scène n'est pas restaurée à l'arrêt"));
        }
        ImGui::TextDisabled("%s", ui::tr(modeHintOf(current)));
    }
    ImGui::End();
    return requested;
}

bool playShortcutPressed()
{
    // Route globale : Alt+P marche quelle que soit la fenêtre qui a le focus (imgui.h, `Shortcut`).
    return ImGui::Shortcut(static_cast<ImGuiKeyChord>(static_cast<unsigned>(ImGuiMod_Alt) |
                                                      static_cast<unsigned>(ImGuiKey_P)),
                           ImGuiInputFlags_RouteGlobal);
}

bool sceneHasFocus()
{
    return !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
}

} // namespace levain::editor
