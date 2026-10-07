#include "levain/app/ui_layer.hpp"

#include <algorithm>

namespace levain::app
{

std::vector<platform::InputEvent> gameInputOf(std::span<const platform::InputEvent> events,
                                              const input::RawInput& held, bool uiTakesMouse,
                                              bool uiTakesKeyboard)
{
    std::vector<platform::InputEvent> game;
    game.reserve(events.size());
    for (const platform::InputEvent& event : events)
    {
        const bool panelsKey = event.device == platform::InputDevice::Keyboard &&
                               event.code == PanelsKey &&
                               event.type == platform::InputEventType::ButtonDown;
        const bool taken = event.type != platform::InputEventType::ButtonUp &&
                           ((uiTakesMouse && event.device == platform::InputDevice::Mouse) ||
                            (uiTakesKeyboard && event.device == platform::InputDevice::Keyboard));
        if (!panelsKey && !taken)
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
    for (std::size_t code = 0; uiTakesMouse && code < held.mouseButtons.size(); ++code)
    {
        if (held.mouseButtons.test(code))
        {
            release(platform::InputDevice::Mouse, code);
        }
    }
    for (std::size_t code = 0; uiTakesKeyboard && code < held.keys.size(); ++code)
    {
        if (held.keys.test(code))
        {
            release(platform::InputDevice::Keyboard, code);
        }
    }
    return game;
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
