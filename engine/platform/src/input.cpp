#include "levain/platform/input.hpp"

#include <algorithm>
#include <vector>

#include <SDL3/SDL.h>

#include "input_sdl.hpp"

#include "levain/core/log.hpp"
#include "levain/platform/window.hpp"

namespace levain::platform
{

// Les bornes annoncées dans l'en-tête doivent couvrir celles de SDL. Si une version de SDL ajoute
// des touches ou des boutons, le build casse ici, et non à l'exécution en écrivant hors d'un
// tableau d'état.
static_assert(SDL_SCANCODE_COUNT <= KeyCodeCount);
static_assert(SDL_GAMEPAD_BUTTON_COUNT <= PadButtonCount);
static_assert(SDL_GAMEPAD_AXIS_COUNT <= PadAxisCount);

namespace
{

/// Les manettes ouvertes. SDL n'envoie les événements d'une manette qu'une fois celle-ci ouverte,
/// et il faut la refermer quand elle est débranchée
/// (https://wiki.libsdl.org/SDL3/SDL_OpenGamepad). Comme la fenêtre possède déjà SDL — une seule à
/// la fois, invariant n°2 du README —, cette liste vit ici plutôt que dans l'API.
std::vector<SDL_Gamepad*> openedGamepads;

std::optional<std::uint16_t> codeOf(int value, int invalid)
{
    return value == invalid ? std::nullopt : std::optional{static_cast<std::uint16_t>(value)};
}

/// Les deux axes de la souris, envoyés ensemble : un mouvement en diagonale donne deux
/// déplacements, et `engine/input` les accumule séparément.
void appendMouseMotion(std::vector<InputEvent>& events, const SDL_MouseMotionEvent& motion)
{
    events.push_back({.type = InputEventType::AxisMotion,
                      .device = InputDevice::Mouse,
                      .code = 0,
                      .value = motion.xrel});
    events.push_back({.type = InputEventType::AxisMotion,
                      .device = InputDevice::Mouse,
                      .code = 1,
                      .value = motion.yrel});
}

/// La densité de pixels de la fenêtre `windowId` : 1 si SDL ne la connaît pas.
float pixelDensityOf(SDL_WindowID windowId)
{
    SDL_Window* window = SDL_GetWindowFromID(windowId);
    const float density = window != nullptr ? SDL_GetWindowPixelDensity(window) : 0.0f;
    return density > 0.0f ? density : 1.0f;
}

/// Un axe de manette va de -32768 à 32767 chez SDL ; le moteur ne connaît que [-1, 1]. La division
/// par 32767 laisserait -1,00003 sur la butée basse, d'où le maximum.
float normalizedAxis(std::int16_t value)
{
    return std::max(static_cast<float>(value) / 32767.0f, -1.0f);
}

} // namespace

std::optional<std::uint16_t> keyCodeFromName(const std::string& name)
{
    return codeOf(SDL_GetScancodeFromName(name.c_str()), SDL_SCANCODE_UNKNOWN);
}

std::optional<std::uint16_t> mouseButtonCodeFromName(const std::string& name)
{
    // SDL n'a pas de table de noms pour les boutons de la souris : celle-ci est à nous, et ses
    // valeurs sont celles de SDL_BUTTON_LEFT et consorts.
    if (name == "left")
    {
        return SDL_BUTTON_LEFT;
    }
    if (name == "right")
    {
        return SDL_BUTTON_RIGHT;
    }
    if (name == "middle")
    {
        return SDL_BUTTON_MIDDLE;
    }
    return std::nullopt;
}

std::optional<std::uint16_t> mouseAxisCodeFromName(const std::string& name)
{
    if (name == "x")
    {
        return 0;
    }
    if (name == "y")
    {
        return 1;
    }
    return std::nullopt;
}

std::optional<std::uint16_t> padButtonCodeFromName(const std::string& name)
{
    return codeOf(SDL_GetGamepadButtonFromString(name.c_str()), SDL_GAMEPAD_BUTTON_INVALID);
}

std::optional<std::uint16_t> padAxisCodeFromName(const std::string& name)
{
    return codeOf(SDL_GetGamepadAxisFromString(name.c_str()), SDL_GAMEPAD_AXIS_INVALID);
}

CursorPosition cursorPosition(const Window& window)
{
    CursorPosition position;
    SDL_GetMouseState(&position.x, &position.y);
    const float density = SDL_GetWindowPixelDensity(window.handle.get());
    // 0 : SDL n'a pas su la lire (la fenêtre n'existe pas encore). On garde les coordonnées telles.
    if (density > 0.0f)
    {
        position.x *= density;
        position.y *= density;
    }
    return position;
}

void setMouseCaptured(const Window& window, bool captured)
{
    if (!SDL_SetWindowRelativeMouseMode(window.handle.get(), captured))
    {
        core::log("platform", core::LogLevel::Warning, "souris non capturée : {}", SDL_GetError());
    }
}

void startTextInput(const Window& window)
{
    if (!SDL_StartTextInput(window.handle.get()))
    {
        core::log("platform", core::LogLevel::Warning, "saisie de texte refusée : {}",
                  SDL_GetError());
    }
}

void stopTextInput(const Window& window)
{
    SDL_StopTextInput(window.handle.get());
}

std::string clipboardText()
{
    // SDL rend une chaîne à libérer, vide (jamais nulle) si le presse-papiers n'a pas de texte.
    char* text = SDL_GetClipboardText();
    std::string copy = text != nullptr ? text : "";
    SDL_free(text);
    return copy;
}

void setClipboardText(const std::string& text)
{
    if (!SDL_SetClipboardText(text.c_str()))
    {
        core::log("platform", core::LogLevel::Warning, "presse-papiers refusé : {}",
                  SDL_GetError());
    }
}

float displayScale(const Window& window)
{
    // 0 : SDL n'a pas su la lire. On garde l'échelle 1 plutôt qu'une interface invisible.
    const float scale = SDL_GetWindowDisplayScale(window.handle.get());
    return scale > 0.0f ? scale : 1.0f;
}

void appendUiEvent(Events& events, const SDL_Event& event, std::uint32_t windowId)
{
    std::vector<UiEvent>& ui = events.ui;
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
    {
        // En pixels de l'image, comme `cursorPosition` : sans la densité, un clic sur un écran
        // HiDPI viserait la moitié haute gauche de l'interface.
        const float density = pixelDensityOf(event.motion.windowID);
        ui.push_back({.type = UiEventType::MouseMoved,
                      .x = event.motion.x * density,
                      .y = event.motion.y * density});
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        ui.push_back({.type = UiEventType::MouseButton,
                      .button = event.button.button,
                      .down = event.button.down});
        break;
    case SDL_EVENT_MOUSE_WHEEL:
    {
        // Telle quelle : SDL la donne déjà dans le sens que le joueur a choisi. Le défilement
        // « naturel » d'un pavé tactile (SDL_MOUSEWHEEL_FLIPPED) y est compris ; le défaire
        // ferait défiler l'UI à l'inverse de toutes les autres applications.
        ui.push_back({.type = UiEventType::MouseWheel, .x = event.wheel.x, .y = event.wheel.y});
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        ui.push_back({.type = UiEventType::Key,
                      .keycode = event.key.key,
                      .scancode = static_cast<std::uint16_t>(event.key.scancode),
                      .down = event.key.down,
                      .modifiers = {.ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0,
                                    .shift = (event.key.mod & SDL_KMOD_SHIFT) != 0,
                                    .alt = (event.key.mod & SDL_KMOD_ALT) != 0,
                                    .super = (event.key.mod & SDL_KMOD_GUI) != 0}});
        break;
    case SDL_EVENT_TEXT_INPUT:
        events.text += event.text.text;
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (event.window.windowID == windowId)
        {
            ui.push_back({.type = event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE
                                      ? UiEventType::MouseLeft
                                      : (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED
                                             ? UiEventType::FocusGained
                                             : UiEventType::FocusLost)});
        }
        break;
    default:
        break;
    }
}

void appendInputEvent(std::vector<InputEvent>& events, const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        // `repeat` : la répétition automatique d'une touche tenue. Une action ne doit se
        // déclencher qu'au vrai appui.
        if (!event.key.repeat)
        {
            events.push_back(
                {.type = event.key.down ? InputEventType::ButtonDown : InputEventType::ButtonUp,
                 .device = InputDevice::Keyboard,
                 .code = static_cast<std::uint16_t>(event.key.scancode)});
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        events.push_back(
            {.type = event.button.down ? InputEventType::ButtonDown : InputEventType::ButtonUp,
             .device = InputDevice::Mouse,
             .code = event.button.button});
        break;

    case SDL_EVENT_MOUSE_MOTION:
        appendMouseMotion(events, event.motion);
        break;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        events.push_back(
            {.type = event.gbutton.down ? InputEventType::ButtonDown : InputEventType::ButtonUp,
             .device = InputDevice::Gamepad,
             .code = event.gbutton.button});
        break;

    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        events.push_back({.type = InputEventType::AxisMotion,
                          .device = InputDevice::Gamepad,
                          .code = event.gaxis.axis,
                          .value = normalizedAxis(event.gaxis.value)});
        break;

    case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad* gamepad = SDL_OpenGamepad(event.gdevice.which))
        {
            openedGamepads.push_back(gamepad);
            core::log("platform", core::LogLevel::Info, "manette branchée : {}",
                      SDL_GetGamepadName(gamepad));
        }
        break;

    case SDL_EVENT_GAMEPAD_REMOVED:
        closeGamepad(event.gdevice.which);
        break;

    default:
        break;
    }
}

void closeGamepad(std::uint32_t instanceId)
{
    const auto opened = std::ranges::find_if(openedGamepads, [instanceId](SDL_Gamepad* gamepad)
                                             { return SDL_GetGamepadID(gamepad) == instanceId; });
    if (opened != openedGamepads.end())
    {
        SDL_CloseGamepad(*opened);
        openedGamepads.erase(opened);
        core::log("platform", core::LogLevel::Info, "manette débranchée");
    }
}

void closeAllGamepads()
{
    for (SDL_Gamepad* gamepad : openedGamepads)
    {
        SDL_CloseGamepad(gamepad);
    }
    openedGamepads.clear();
}

} // namespace levain::platform
