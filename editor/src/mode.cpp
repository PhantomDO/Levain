#include "levain/editor/mode.hpp"

#include <tuple>

#include <imgui.h>

#include "levain/app/player_input.hpp"
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
        return ui::trf("{} : arrêter", key);
    }
    return {};
}

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

std::string modeButtonLabel(Mode target)
{
    switch (target)
    {
    case Mode::Edit:
        return ui::icon::withIcon(ui::icon::Stop, ui::labelOf("Arrêter"));
    case Mode::Play:
        return ui::icon::withIcon(ui::icon::Play, ui::labelOf("Jouer"));
    }
    return {};
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
/// (seul il va dans les deux sens, `modeRequested`), le bandeau de « Jouer » et le
/// rappel de la touche. Rend le mode demandé et le rectangle du bouton, s'il y en a un (le reste
/// est à l'appelant, qui tient l'enfant).
ModeBarOverlay drawModeBarContents(Mode current, bool interactive, const std::string& hint)
{
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
    switch (current)
    {
    case Mode::Edit:
        break;
    case Mode::Play:
        ImGui::TextColored(
            {1.0f, 0.75f, 0.2f, 1.0f}, "%s",
            ui::icon::withIcon(ui::icon::Play,
                               ui::tr("Jouer : la scène n'est pas restaurée à l'arrêt"))
                .c_str());
        break;
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

void drawModeBar(Mode current, const app::ScreenRect& scene, const std::string& hint)
{
    ImGui::SetNextWindowPos({scene.x + 8.0f, scene.y + 8.0f});
    if (ImGui::Begin(ui::labelOf("Mode").c_str(), nullptr, modeBarFlags()))
    {
        std::ignore = drawModeBarContents(current, false, hint);
    }
    ImGui::End();
}

ModeBarOverlay drawModeBarOverlay(Mode current, ImVec2 corner, const std::string& hint)
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
        bar = drawModeBarContents(current, true, hint);
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
