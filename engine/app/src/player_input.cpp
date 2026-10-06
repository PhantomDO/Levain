#include "levain/app/player_input.hpp"

#include <cstddef>

#include "levain/scene/scene.hpp"

namespace levain::app
{

void takeFrameInput(PlayerInput& input, const input::InputState& state)
{
    input.state = state;
    input.pressesUntilNextStep.resize(state.actionsHeld.size(), false);
    for (std::size_t action = 0; action < state.actionsHeld.size(); ++action)
    {
        if (input::actionPressed(state, static_cast<int>(action)))
        {
            input.pressesUntilNextStep[action] = true;
        }
    }
}

void forgetPresses(PlayerInput& input)
{
    input.pressesUntilNextStep.assign(input.pressesUntilNextStep.size(), false);
}

void forgetPressesAtEachStep(flecs::world& world)
{
    world.system("ForgetPresses")
        .kind<scene::EndOfStep>()
        .run([](flecs::iter& it) { forgetPresses(it.world().get_mut<PlayerInput>()); });
}

bool pressedSinceLastStep(const PlayerInput& input, int action)
{
    return action >= 0 && static_cast<std::size_t>(action) < input.pressesUntilNextStep.size() &&
           input.pressesUntilNextStep[static_cast<std::size_t>(action)];
}

} // namespace levain::app
