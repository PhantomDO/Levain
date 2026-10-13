#include "levain/editor/gestures.hpp"

#include <cmath>

namespace levain::editor
{

namespace
{

bool altHeld(const std::bitset<platform::KeyCodeCount>& keys)
{
    return keys.test(LeftAltScancode) || keys.test(RightAltScancode);
}

bool shiftHeld(const std::bitset<platform::KeyCodeCount>& keys)
{
    return keys.test(LeftShiftScancode) || keys.test(RightShiftScancode);
}

/// Le curseur est dans la scène et celle-ci est sous la souris : l'endroit où un geste commence.
bool startsInScene(const GestureState& state, const GestureContext& context)
{
    return state.cursorKnown && context.sceneHovered &&
           app::rectContains(context.region, state.cursor.x, state.cursor.y);
}

float lengthOf(glm::vec2 vector)
{
    return std::sqrt((vector.x * vector.x) + (vector.y * vector.y));
}

/// Le bouton droit vient de commencer le geste `gesture` : ce qui se garde de l'appui.
void begin(GestureState& state, Gesture gesture, std::uint8_t button)
{
    state.active = gesture;
    state.button = button;
    if (button == RightButton)
    {
        state.travelled = 0.0f;
        state.rightPressInScene = true;
    }
}

} // namespace

GestureState idleGestures(const GestureState& state)
{
    GestureState idle;
    idle.cursor = state.cursor;
    idle.cursorKnown = state.cursorKnown;
    idle.flew = state.flew;
    idle.rightPressInScene = state.rightPressInScene;
    return idle;
}

glm::vec3 flightMoveOf(const ShortcutTable& table, const std::bitset<platform::KeyCodeCount>& keys)
{
    const auto axis = [&table, &keys](Action positive, Action negative)
    {
        const Binding* plus = bindingOf(table, positive);
        const Binding* minus = bindingOf(table, negative);
        return (plus != nullptr && positionHeld(*plus, keys) ? 1.0f : 0.0f) -
               (minus != nullptr && positionHeld(*minus, keys) ? 1.0f : 0.0f);
    };
    return {axis(Action::MoveRight, Action::MoveLeft), axis(Action::MoveUp, Action::MoveDown),
            axis(Action::MoveForward, Action::MoveBackward)};
}

GestureStep stepGestures(GestureState state, const platform::Events& events,
                         const GestureContext& context, const ShortcutTable& table, float seconds)
{
    GestureStep step;
    step.frame.seconds = clampFlightSeconds(seconds);
    for (const platform::UiEvent& event : events.ui)
    {
        switch (event.type)
        {
        case platform::UiEventType::MouseMoved:
            // Souris capturée, SDL continue de compter une position qui n'est pas celle du curseur.
            if (state.active != Gesture::Fly)
            {
                state.cursor = {event.x, event.y};
                state.cursorKnown = true;
            }
            break;
        case platform::UiEventType::MouseButton:
            if (event.down && event.button == RightButton)
            {
                // Chaque appui du bouton droit repart de zéro, où qu'il tombe : un `flew` resté
                // d'un vol d'avant n'est pas celui d'un clic parti d'un panneau.
                state.flew = false;
                state.rightPressInScene = false;
            }
            if (event.down && state.active == Gesture::None)
            {
                const Gesture gesture = gestureStartedBy(event.button, altHeld(state.keys));
                if (gesture != Gesture::None && startsInScene(state, context))
                {
                    begin(state, gesture, event.button);
                }
            }
            else if (!event.down && state.active != Gesture::None && event.button == state.button)
            {
                // Le mouvement de cette image est encore pour ce geste (`activeThisFrame`).
                step.frame.gesture = state.active;
                state.active = Gesture::None;
                state.button = 0;
            }
            break;
        case platform::UiEventType::MouseWheel:
            if (state.active != Gesture::None || startsInScene(state, context))
            {
                step.frame.wheelNotches += event.y;
                state.flew = state.flew || state.active == Gesture::Fly;
            }
            break;
        case platform::UiEventType::Key:
            if (event.scancode < state.keys.size())
            {
                state.keys.set(event.scancode, event.down);
            }
            if (event.down)
            {
                const KeyPress press{.scancode = event.scancode,
                                     .keycode = event.keycode,
                                     .modifiers = event.modifiers};
                const ActiveContexts active{.flight = inFlight(state),
                                            .view = context.viewActive,
                                            .game = false,
                                            .textInput = context.textInput};
                step.frameSelection = step.frameSelection ||
                                      actionFor(table, press, active) == Action::FrameSelection;
            }
            break;
        case platform::UiEventType::FocusLost:
            // Les relâchements ne viendront pas : ni touche tenue, ni geste en cours.
            state = idleGestures(state);
            break;
        default:
            break;
        }
    }
    // Cachée, la fenêtre ne reçoit plus les relâchements : ni touche tenue, ni geste en cours. Le
    // mouvement de l'image revient au geste qui s'arrête ici (`current` ci-dessous).
    for (const platform::WindowEvent& event : events.window)
    {
        if (event.type == platform::WindowEventType::Hidden)
        {
            step.frame.gesture = state.active != Gesture::None ? state.active : step.frame.gesture;
            state = idleGestures(state);
        }
    }
    // Le geste de l'image : celui qui était en cours au début ou a commencé dans l'image, même si
    // son bouton a été relâché depuis (le mouvement qui précède le relâchement lui revient).
    const Gesture current = state.active != Gesture::None ? state.active : step.frame.gesture;
    step.frame.gesture = current;
    if (current != Gesture::None)
    {
        for (const platform::InputEvent& event : events.input)
        {
            if (event.type == platform::InputEventType::AxisMotion &&
                event.device == platform::InputDevice::Mouse && event.code < 2)
            {
                (event.code == 0 ? step.frame.motion.x : step.frame.motion.y) += event.value;
            }
        }
    }
    if (current == Gesture::Fly)
    {
        step.frame.move = flightMoveOf(table, state.keys);
        step.frame.speedMultiplier = shiftHeld(state.keys) ? RunMultiplier : 1.0f;
    }
    if (current == Gesture::Fly || current == Gesture::DollyDrag)
    {
        // Le clic droit : a-t-il bougé, volé ou zoomé ? Sinon c'est un clic (`GestureState::flew`).
        state.travelled += lengthOf(step.frame.motion);
        const bool keyed = step.frame.move != glm::vec3{0.0f};
        state.flew = state.flew || state.travelled > ClickSlopPixels || keyed;
    }
    step.state = state;
    return step;
}

EditorCamera applyGesture(EditorCamera camera, const GestureFrame& frame,
                          float viewportHeightPixels)
{
    switch (frame.gesture)
    {
    case Gesture::Fly:
        camera = flyCamera(camera,
                           {.lookPixels = frame.motion,
                            .move = frame.move,
                            .speedMultiplier = frame.speedMultiplier},
                           frame.seconds);
        return scaleFlySpeed(camera, frame.wheelNotches);
    case Gesture::Orbit:
        camera = orbitCamera(camera, frame.motion);
        break;
    case Gesture::Pan:
        camera = panCamera(camera, frame.motion, viewportHeightPixels);
        break;
    case Gesture::DollyDrag:
        // Vers le haut de l'écran (y < 0), on avance : un cran par `DollyPixelsPerNotch`.
        camera = dollyCamera(camera, -frame.motion.y / DollyPixelsPerNotch);
        break;
    case Gesture::None:
        break;
    }
    return frame.wheelNotches != 0.0f ? dollyCamera(camera, frame.wheelNotches) : camera;
}

} // namespace levain::editor
