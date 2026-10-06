#pragma once

// L'input du joueur, en singleton du monde (ADR-0029, point 9) : les systèmes du jeu et de ses
// plugins le lisent, sans que `scene` connaisse l'input ni que l'input connaisse le monde.

#include <vector>

#include <flecs.h>

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

/// Oublie les appuis : le pas qui vient de finir les a vus.
void forgetPresses(PlayerInput& input);

/// Inscrit l'oubli des appuis à la fin de **chaque** pas (`scene::EndOfStep`), après tous les
/// systèmes du pas. Oublier après tous les pas de l'image ne suffirait pas : une image qui en joue
/// deux ferait voir l'appui aux deux, et le joueur sauterait deux fois. `app` l'appelle à la
/// création du monde.
void forgetPressesAtEachStep(flecs::world& world);

/// L'action a-t-elle été appuyée depuis le dernier pas joué ? Ce qu'un système du pas fixe lit au
/// lieu de `input::actionPressed`, qui ne vaut que pour l'image. Les axes, eux, sont des vitesses
/// (ADR-0017) : un système du pas les multiplie par la durée du pas, sans ce piège.
[[nodiscard]] bool pressedSinceLastStep(const PlayerInput& input, int action);

} // namespace levain::app
