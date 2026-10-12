#include "levain/editor/gestures.hpp"

namespace levain::editor
{

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

} // namespace levain::editor
