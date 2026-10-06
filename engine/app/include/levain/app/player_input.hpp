#pragma once

// L'input du joueur, en singleton du monde (ADR-0029, point 9) : les systèmes du jeu et de ses
// plugins le lisent, sans que `scene` connaisse l'input ni que l'input connaisse le monde.

#include <vector>

#include "levain/input/bindings.hpp"
#include "levain/input/state.hpp"

namespace levain::app
{

/// Ce que le joueur fait à cette image, et les appuis qu'aucun pas de simulation n'a encore vus.
/// `app` le pose à chaque image, avant les pas de simulation : `world.get<PlayerInput>()`.
struct PlayerInput
{
    /// Les liaisons du programme : de quoi retrouver une action ou un axe par son nom, une fois
    /// (`input::actionIndex`). Elles vivent dans `App`, qui ne bouge pas.
    const input::Bindings* bindings = nullptr;
    input::InputState state; ///< Les actions tenues et les axes de cette image.
    /// Le piège des appuis entre deux pas : une case par action, vraie si elle a été appuyée
    /// depuis le dernier pas de simulation (`pressedSinceLastStep`).
    std::vector<bool> pressesUntilNextStep;
};

/// Prend l'état de l'image, et ajoute ses appuis à ceux qui attendent un pas. Un appui ne dure
/// qu'une image : à 144 images/s, la plupart n'ont aucun pas de simulation, et un système du pas
/// fixe ne le verrait jamais.
void takeFrameInput(PlayerInput& input, const input::InputState& state);

/// Oublie les appuis, après une image qui a joué au moins un pas : ils ont été vus. Sans ça, une
/// image qui joue deux pas les verrait deux fois, et le joueur sauterait deux fois.
void forgetPressesAfterSteps(PlayerInput& input, int stepsPlayed);

/// L'action a-t-elle été appuyée depuis le dernier pas joué ? Ce qu'un système du pas fixe lit au
/// lieu de `input::actionPressed`, qui ne vaut que pour l'image.
[[nodiscard]] bool pressedSinceLastStep(const PlayerInput& input, int action);

} // namespace levain::app
