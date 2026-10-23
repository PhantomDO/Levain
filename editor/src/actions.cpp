#include "levain/editor/actions.hpp"

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// L'action est celle que `playShortcutPressed` lit encore : la compter ici la ferait partir deux
/// fois.
bool readElsewhere(Action action)
{
    return action == Action::Play;
}

} // namespace

void switchRealTime(Mode from, Mode to, bool& appRealTime, bool& editChoice)
{
    if (from == Mode::Edit && to != Mode::Edit)
    {
        editChoice = appRealTime;
        appRealTime = true;
    }
    else if (from != Mode::Edit && to == Mode::Edit)
    {
        appRealTime = editChoice;
    }
}

std::optional<Mode> runAction(Action action, ActionCounters& counters, bool& realTime, Mode current)
{
    const Availability availability = availabilityOf(action);
    if (!availability.available)
    {
        ++counters.declined.at(indexOf(action));
        core::log("editor", core::LogLevel::Info, "{}",
                  ui::trf("{} : arrive en {}", ui::tr(nameOf(action)), availability.milestone));
        return std::nullopt;
    }
    if ((action == Action::RealTime || action == Action::Simulate || action == Action::Play ||
         isHistoryAction(action)) &&
        current != Mode::Edit)
    {
        // Grisé hors de l'Édition (`switchRealTime`) : Ctrl+R le dit, il ne bascule rien. Alt+S et
        // Alt+P aussi : on joue et on simule depuis l'Édition, la scène rendue ; en jeu, elles
        // compteraient un appui sans effet. Ctrl+Z et Ctrl+Y de même : la pile est gelée en jeu.
        ++counters.declined.at(indexOf(action));
        core::log("editor", core::LogLevel::Info, "{}",
                  ui::trf("{} : seulement en Édition", ui::tr(nameOf(action))));
        return std::nullopt;
    }
    ++counters.performed.at(indexOf(action));
    switch (action)
    {
    case Action::RealTime:
        realTime = !realTime;
        return std::nullopt;
    case Action::Play:
        return modeRequested(current, {.playShortcut = true});
    case Action::Simulate:
        return modeRequested(current, {.simulate = true});
    case Action::Stop:
        return modeRequested(current, {.stop = true});
    default:
        return std::nullopt;
    }
}

void countRefusedTransition(ActionCounters& counters, Action action)
{
    int& performed = counters.performed.at(indexOf(action));
    LEVAIN_ASSERT(performed > 0, "countRefusedTransition : l'action n'avait pas été comptée faite");
    --performed;
    ++counters.declined.at(indexOf(action));
}

std::optional<PlayControl> runPlayControl(Action action, ActionCounters& counters, Mode current,
                                          bool paused)
{
    const auto control = playControlOf(action);
    if (!control)
    {
        return std::nullopt;
    }
    const std::string name = ui::tr(nameOf(action));
    switch (playControlRefusal(*control, current, paused))
    {
    case PlayRefusal::None:
        break;
    case PlayRefusal::NotPlaying:
        ++counters.declined.at(indexOf(action));
        core::log("editor", core::LogLevel::Info, "{}", ui::trf("{} : seulement en jeu", name));
        return std::nullopt;
    case PlayRefusal::NotPaused:
        ++counters.declined.at(indexOf(action));
        core::log("editor", core::LogLevel::Info, "{}", ui::trf("{} : seulement en pause", name));
        return std::nullopt;
    case PlayRefusal::NotInPlay:
        ++counters.declined.at(indexOf(action));
        core::log("editor", core::LogLevel::Info, "{}", ui::trf("{} : seulement en Jouer", name));
        return std::nullopt;
    }
    ++counters.performed.at(indexOf(action));
    return control;
}

std::vector<Action> withoutHistoryWhileActive(std::vector<Action> shortcuts, bool itemActive,
                                              ActionCounters& counters)
{
    if (!itemActive)
    {
        return shortcuts;
    }
    std::erase_if(
        shortcuts,
        [&](Action action)
        {
            if (!isHistoryAction(action))
            {
                return false;
            }
            ++counters.declined.at(indexOf(action));
            core::log("editor", core::LogLevel::Info, "{}",
                      ui::trf("{} : refusé pendant qu'un champ est actif", ui::tr(nameOf(action))));
            return true;
        });
    return shortcuts;
}

std::optional<Action> toolActionOf(const ShortcutTable& table, const KeyPress& press,
                                   const ActiveContexts& active)
{
    const auto action = actionFor(table, press, active);
    return action && isToolAction(*action) ? action : std::nullopt;
}

std::optional<Action> sceneKeyActionOf(const ShortcutTable& table, const KeyPress& press,
                                       const ActiveContexts& active)
{
    const auto action = actionFor(table, press, active);
    return action && isSceneAction(*action) ? action : std::nullopt;
}

std::optional<Action> viewKeyActionOf(const ShortcutTable& table, const KeyPress& press,
                                      const ActiveContexts& active)
{
    const auto action = actionFor(table, press, active);
    return action && (isToolAction(*action) || *action == Action::GameView) ? action : std::nullopt;
}

std::bitset<platform::KeyCodeCount> heldKeysAfter(const platform::Events& events,
                                                  std::bitset<platform::KeyCodeCount> held)
{
    for (const platform::UiEvent& event : events.ui)
    {
        if (event.type == platform::UiEventType::FocusLost)
        {
            held.reset();
        }
        else if (event.type == platform::UiEventType::Key && event.scancode < held.size())
        {
            held.set(event.scancode, event.down);
        }
    }
    return held;
}

std::vector<KeyPress> keyPressesOf(const platform::Events& events,
                                   std::bitset<platform::KeyCodeCount> held)
{
    std::vector<KeyPress> pressed;
    for (const platform::UiEvent& event : events.ui)
    {
        if (event.type == platform::UiEventType::FocusLost)
        {
            held.reset(); // les relâchements ne viendront pas
            continue;
        }
        if (event.type != platform::UiEventType::Key || event.scancode >= held.size())
        {
            continue;
        }
        if (event.down && !held.test(event.scancode)) // une répétition n'est pas un appui
        {
            pressed.push_back({.scancode = event.scancode,
                               .keycode = event.keycode,
                               .modifiers = event.modifiers});
        }
        held.set(event.scancode, event.down);
    }
    return pressed;
}

std::vector<Action> shortcutsPressed(const ShortcutInputs& inputs)
{
    std::vector<Action> reached;
    if (!inputs.globalAllowed) // sous une modale, aucun raccourci de l'éditeur ne part
    {
        return reached;
    }
    for (const Binding& binding : inputs.table.bindings)
    {
        const bool lettered = binding.context == Context::Global ||
                              (binding.context == Context::View && inputs.viewActive);
        if (lettered && !readElsewhere(binding.action) && letterChordPressed(binding))
        {
            reached.push_back(binding.action);
        }
    }
    const ActiveContexts active{.flight = inputs.flight,
                                .view = inputs.viewActive,
                                .scene = inputs.sceneActive,
                                .game = false,
                                .textInput = inputs.textInput};
    for (const KeyPress& press : inputs.presses)
    {
        // Les outils ne valent qu'en Édition ; Suppr et F2, là où la sélection a le focus.
        const auto tool =
            inputs.editing ? viewKeyActionOf(inputs.table, press, active) : std::nullopt;
        if (const auto action = tool ? tool : sceneKeyActionOf(inputs.table, press, active))
        {
            reached.push_back(*action);
        }
    }
    return reached;
}

} // namespace levain::editor
