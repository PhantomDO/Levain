#include "levain/app/player_input.hpp"

#include <cstddef>

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

void forgetPressesAfterSteps(PlayerInput& input, int stepsPlayed)
{
    if (stepsPlayed > 0)
    {
        input.pressesUntilNextStep.assign(input.pressesUntilNextStep.size(), false);
    }
}

bool pressedSinceLastStep(const PlayerInput& input, int action)
{
    return action >= 0 && static_cast<std::size_t>(action) < input.pressesUntilNextStep.size() &&
           input.pressesUntilNextStep[static_cast<std::size_t>(action)];
}

} // namespace levain::app
