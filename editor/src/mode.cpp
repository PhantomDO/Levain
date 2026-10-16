#include "levain/editor/mode.hpp"

#include <algorithm>
#include <tuple>

#include <imgui.h>

#include "levain/app/player_input.hpp"
#include "levain/editor/actions.hpp"
#include "levain/ui/flags.hpp"
#include "levain/ui/icons.hpp"
#include "levain/ui/style.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Les drapeaux de la fenêtre de la barre de mode, panneaux fermés : un bandeau que rien ne vise,
/// ne navigue ni ne focalise (`NoInputs`) : `sceneHasFocus` reste vrai et la route *jeu* aussi.
int modeBarFlags()
{
    return ui::withFlags(
        ui::withFlags(ui::withFlags(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoMove),
                      ui::withFlags(ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoDocking)),
        ui::withFlags(
            ui::withFlags(ImGuiWindowFlags_AlwaysAutoResize, ImGuiWindowFlags_NoFocusOnAppearing),
            ImGuiWindowFlags_NoInputs));
}

/// L'action dont le rappel de touche parle : jouer depuis l'Édition, arrêter depuis un mode de jeu.
Action actionLeaving(Mode mode)
{
    switch (mode)
    {
    case Mode::Edit:
        return Action::Play;
    case Mode::Play:
    case Mode::Simulate:
        return Action::Stop;
    }
    return Action::Stop;
}

} // namespace

std::string modeHintOf(Mode mode, const ShortcutTable& table,
                       const platform::KeyAtPosition& keyAtPosition)
{
    const Action action = actionLeaving(mode);
    const Binding* binding = bindingOf(table, action);
    if (binding == nullptr || !isBound(binding->chords.front()))
    {
        return {};
    }
    const std::string key = displayNameOf(binding->chords.front(), keyAtPosition);
    switch (mode)
    {
    case Mode::Edit:
        return ui::trf("{} : jouer", key);
    case Mode::Play:
    case Mode::Simulate:
        return ui::trf("{} : arrêter", key);
    }
    return {};
}

GameKeys gameKeysPressed(ModeState& state, const platform::Events& events,
                         const ShortcutTable& table, bool blocked)
{
    const std::vector<KeyPress> presses = keyPressesOf(events, state.keys);
    state.keys = heldKeysAfter(events, state.keys);
    GameKeys keys;
    if (state.current == Mode::Edit || blocked)
    {
        return keys;
    }
    const Binding* stop = bindingOf(table, Action::Stop);
    for (const KeyPress& press : presses)
    {
        if (stop != nullptr && positionIs(*stop, press.scancode))
        {
            // Échap ignore les modificateurs : Maj tenu pendant le jeu n'y change rien.
            keys.stop = true;
            continue;
        }
        const auto action = actionFor(table, press, {.game = true});
        if (action && playControlOf(*action))
        {
            keys.actions.push_back(*action);
        }
    }
    return keys;
}

bool mousePressedIn(const platform::Events& events)
{
    return std::ranges::any_of(
        events.ui, [](const platform::UiEvent& event)
        { return event.type == platform::UiEventType::MouseButton && event.down; });
}

namespace
{

/// Le jeu repart de zéro : un `InputState` neuf, ses appuis en attente oubliés, et le singleton
/// relu (il vaut pour l'image en cours, où `frame` a peut-être déjà tourné). La route de l'image
/// qui commence est celle de l'état ; la définitive est posée à la fin de `ui`.
void releaseGameInput(app::App& app, const ModeState& state)
{
    app.mouseCaptureWanted = false;
    app.mouseCaptureOverPanels = false;
    app.input = input::makeInputState(app.bindings);
    auto& playerInput = app.world.get_mut<app::PlayerInput>();
    app::forgetPresses(playerInput);
    app::takeFrameInput(playerInput, app.input);
    app.inputRoute = inputRouteOf(state, false);
}

} // namespace

void enterMode(app::App& app, ModeState& state, Mode mode)
{
    state.current = mode;
    state.paused = false;
    state.mouseFreed = false;
    app.simulationPaused = !simulationRunsIn(mode, state.paused);
    // Un pas demandé par la barre d'outils (dans `ui`) et pas encore joué : Stop l'emporte, sinon
    // l'Édition jouerait ce pas après que la scène a été rendue.
    app.singleStep = false;
    releaseGameInput(app, state);
}

void applyPlayControl(PlayControl control, app::App& app, ModeState& state)
{
    switch (control)
    {
    case PlayControl::TogglePause:
        state.paused = !state.paused;
        app.simulationPaused = !simulationRunsIn(state.current, state.paused);
        releaseGameInput(app, state);
        break;
    case PlayControl::StepFrame:
        app.singleStep = true;
        break;
    case PlayControl::FreeMouse:
        state.mouseFreed = true;
        releaseGameInput(app, state);
        break;
    }
}

void keepMouseFromGame(app::App& app, const ModeState& state, bool editorWantsMouse)
{
    if (state.mouseFreed || state.paused || editorOwnsViewIn(state.current))
    {
        app.mouseCaptureWanted = editorWantsMouse;
        app.mouseCaptureOverPanels = editorWantsMouse;
    }
}

std::string modeButtonLabel(Mode target)
{
    switch (target)
    {
    case Mode::Edit:
        return ui::icon::withIcon(ui::icon::Stop, ui::labelOf("Arrêter"));
    case Mode::Play:
        return ui::icon::withIcon(ui::icon::Play, ui::labelOf("Jouer"));
    case Mode::Simulate:
        return ui::labelOf("Simuler");
    }
    return {};
}

std::string pauseButtonLabel(bool paused)
{
    return paused ? ui::icon::withIcon(ui::icon::Play, ui::labelOf("Reprendre"))
                  : ui::icon::withIcon(ui::icon::Pause, ui::labelOf("Pause"));
}

std::string stepButtonLabel()
{
    return ui::icon::withIcon(ui::icon::Step, ui::labelOf("Image suivante"));
}

namespace
{

/// Le dernier item qu'ImGui a dessiné, à l'écran : là où un test le vise, et d'où la Vue place ce
/// qui le suit.
app::ScreenRect lastItemRect()
{
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    return {.x = min.x, .y = min.y, .width = max.x - min.x, .height = max.y - min.y};
}

/// Le contenu de la barre, dans la fenêtre ou le calque courant : le bouton qui montre où l'on va
/// (seul il va dans les deux sens, `modeRequested`), le bandeau du jeu, dans sa teinte, et le
/// rappel de la touche. Rend le mode demandé et le rectangle du bouton, s'il y en a un (le reste
/// est à l'appelant, qui tient l'enfant).
ModeBarOverlay drawModeBarContents(const ModeState& mode, bool interactive, const std::string& hint)
{
    const Mode current = mode.current;
    ModeBarOverlay bar;
    if (interactive)
    {
        const Mode target = otherMode(current);
        if (ImGui::Button(modeButtonLabel(target).c_str()))
        {
            bar.requested = target;
        }
        bar.button = lastItemRect();
    }
    if (const auto tint = playTintOf(current, mode.paused))
    {
        // L'icône dans la teinte, le texte en `Text` : une icône est un graphisme (3 de contraste
        // suffisent, sur une scène blanche comme sur une sombre), un texte en couleur de teinte
        // n'atteindrait pas 4,5 (`tests/ui_style_test.cpp`).
        ImGui::TextColored(*tint, "%s", mode.paused ? ui::icon::Pause : ui::icon::Play);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(ui::tr(modeBannerOf(current, mode.paused, mode.mouseFreed)));
    }
    if (!hint.empty())
    {
        // En `Text`, non `TextDisabled` : sur une scène claire le fond du calque reste assez
        // sombre pour le texte (5,7 sur du blanc), pas pour un gris éteint (3,9 à 178).
        ImGui::TextUnformatted(hint.c_str());
    }
    return bar;
}

} // namespace

void drawModeBar(const ModeState& mode, const app::ScreenRect& scene, const std::string& hint)
{
    ImGui::SetNextWindowPos({scene.x + 8.0f, scene.y + 8.0f});
    if (ImGui::Begin(ui::labelOf("Mode").c_str(), nullptr, modeBarFlags()))
    {
        std::ignore = drawModeBarContents(mode, false, hint);
    }
    ImGui::End();
}

ModeBarOverlay drawModeBarOverlay(const ModeState& mode, ImVec2 corner, const std::string& hint)
{
    ModeBarOverlay bar;
    // Un enfant de la fenêtre de la Vue, non une fenêtre flottante : une fenêtre flottante passe
    // derrière la Vue dès que celle-ci prend le focus (`BringWindowToDisplayFront`), et la barre
    // disparaîtrait au premier clic sur la scène.
    ImGui::SetCursorScreenPos({corner.x + 8.0f, corner.y + 8.0f});
    const int childFlags = ui::withFlags(ImGuiChildFlags_AutoResizeX, ImGuiChildFlags_AutoResizeY);
    // Un fond presque opaque : le rappel de la touche est en gris, illisible sur un ciel clair. Un
    // gris de la palette (`overlayBackground`), pas un bleu-gris de plus.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::overlayBackground());
    if (ImGui::BeginChild(ui::labelOf("Mode").c_str(), {0.0f, 0.0f},
                          ui::withFlags(childFlags, ImGuiChildFlags_Borders),
                          ImGuiWindowFlags_NoScrollbar))
    {
        bar = drawModeBarContents(mode, true, hint);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    // L'enfant est le dernier item : son survol et son rectangle, la barre entière.
    bar.hovered = ImGui::IsItemHovered();
    bar.bar = lastItemRect();
    return bar;
}

bool playShortcutPressed(const ShortcutTable& table)
{
    // Route globale : Alt+P marche quelle que soit la fenêtre qui a le focus, sauf un champ de
    // texte actif (Alt+P ne joue pas pendant qu'on tape dans la recherche de l'Aide).
    const Binding* play = bindingOf(table, Action::Play);
    return play != nullptr && letterChordPressed(*play);
}

bool sceneHasFocus()
{
    return !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
}

} // namespace levain::editor
