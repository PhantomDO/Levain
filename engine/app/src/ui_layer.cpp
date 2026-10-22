#include "levain/app/ui_layer.hpp"

#include <algorithm>

namespace levain::app
{

std::vector<platform::InputEvent> gameInputOf(std::span<const platform::InputEvent> events,
                                              const input::RawInput& held, DevicesTaken taken)
{
    const auto isTaken = [&taken](platform::InputDevice device)
    {
        return (device == platform::InputDevice::Mouse && taken.mouse) ||
               (device == platform::InputDevice::Keyboard && taken.keyboard) ||
               (device == platform::InputDevice::Gamepad && taken.gamepad);
    };
    std::vector<platform::InputEvent> game;
    game.reserve(events.size());
    for (const platform::InputEvent& event : events)
    {
        const bool panelsKey = event.device == platform::InputDevice::Keyboard &&
                               event.code == PanelsKey &&
                               event.type == platform::InputEventType::ButtonDown;
        const bool swallowed =
            event.type != platform::InputEventType::ButtonUp && isTaken(event.device);
        if (!panelsKey && !swallowed)
        {
            game.push_back(event);
        }
    }
    // Ce qui était tenu avant que l'UI ne prenne l'appareil : relâché, sans quoi la touche
    // resterait enfoncée pour le jeu tant que l'UI la garde.
    const auto release = [&game](platform::InputDevice device, std::size_t code)
    {
        game.push_back({.type = platform::InputEventType::ButtonUp,
                        .device = device,
                        .code = static_cast<std::uint16_t>(code)});
    };
    for (std::size_t code = 0; taken.mouse && code < held.mouseButtons.size(); ++code)
    {
        if (held.mouseButtons.test(code))
        {
            release(platform::InputDevice::Mouse, code);
        }
    }
    for (std::size_t code = 0; taken.keyboard && code < held.keys.size(); ++code)
    {
        if (held.keys.test(code))
        {
            release(platform::InputDevice::Keyboard, code);
        }
    }
    for (std::size_t code = 0; taken.gamepad && code < held.padButtons.size(); ++code)
    {
        if (held.padButtons.test(code))
        {
            release(platform::InputDevice::Gamepad, code);
        }
    }
    // Un stick tenu penché se rend par sa position : on le remet au centre.
    for (std::size_t axis = 0; taken.gamepad && axis < held.padAxes.size(); ++axis)
    {
        if (held.padAxes.at(axis) != 0.0f)
        {
            game.push_back({.type = platform::InputEventType::AxisMotion,
                            .device = platform::InputDevice::Gamepad,
                            .code = static_cast<std::uint16_t>(axis),
                            .value = 0.0f});
        }
    }
    return game;
}

std::optional<platform::Events> uiEventsOf(InputRoute route, const platform::Events& events,
                                           bool mouseCaptured)
{
    if (route != InputRoute::Game && !mouseCaptured)
    {
        return std::nullopt;
    }
    platform::Events kept; // sans `text` : le texte tapé en jouant n'est pas pour ImGui
    kept.ui.reserve(events.ui.size());
    for (const platform::UiEvent& event : events.ui)
    {
        const bool gameKeyPress = route == InputRoute::Game &&
                                  event.type == platform::UiEventType::Key && event.down &&
                                  event.scancode != PanelsKey;
        if (!gameKeyPress && !mouseTakenByCapture(event, mouseCaptured))
        {
            kept.ui.push_back(event);
        }
    }
    // Pas de texte sous la capture non plus : en vol, ZQSD ne se tape pas dans le champ de
    // l'inspecteur qui était actif. Les touches, elles, restent (Ctrl+S, un accord du global, vaut
    // en vol ; décision 13). Retour arrière et les flèches agiraient encore sur un champ actif :
    // l'éditeur le désactive en commençant le vol (`ImGui::ClearActiveID`, morceau 11).
    return kept;
}

float averageFrameMs(const FrameHistory& history)
{
    float total = 0.0f;
    std::size_t measured = 0;
    for (const float frameMs : history.frameMs)
    {
        if (frameMs > 0.0f) // une case jamais remplie
        {
            total += frameMs;
            ++measured;
        }
    }
    return measured > 0 ? total / static_cast<float>(measured) : 0.0f;
}

void recordHistory(FrameHistory& history, float frameMs, std::optional<float> gpuMs)
{
    const std::size_t previous = (history.next + HistoryLength - 1) % HistoryLength;
    history.frameMs.at(history.next) = frameMs;
    history.gpuMs.at(history.next) = gpuMs.value_or(history.gpuMs.at(previous));
    history.next = (history.next + 1) % HistoryLength;
}

void recordUiCpu(UiCost& cost, double milliseconds)
{
    cost.cpuTotalMs += milliseconds;
    cost.cpuMaxMs = std::max(cost.cpuMaxMs, milliseconds);
    ++cost.cpuSamples;
}

} // namespace levain::app
