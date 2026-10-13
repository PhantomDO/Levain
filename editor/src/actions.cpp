#include "levain/editor/actions.hpp"

#include "levain/core/log.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

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
    if (action == Action::RealTime && current != Mode::Edit)
    {
        // Grisé hors de l'Édition (`switchRealTime`) : Ctrl+R le dit, il ne bascule rien.
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
    case Action::Stop:
        return modeRequested(current, {.stop = true});
    default:
        return std::nullopt;
    }
}

} // namespace levain::editor
