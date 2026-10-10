#include "levain/editor/mode.hpp"

#include <imgui.h>

#include "levain/app/player_input.hpp"

namespace levain::editor
{

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
