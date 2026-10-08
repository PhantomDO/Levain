#include "levain/scene/camera_control.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace levain::scene
{

namespace
{

/// Sous ce carré de longueur, le vecteur est considéré nul : normaliser plus court que ça rendrait
/// n'importe quoi.
constexpr float NegligibleLengthSquared = 1e-8f;

} // namespace

glm::vec3 normalizeOrZero(glm::vec3 direction)
{
    const float lengthSquared = glm::dot(direction, direction);
    return lengthSquared < NegligibleLengthSquared ? glm::vec3{0.0f}
                                                   : direction / std::sqrt(lengthSquared);
}

float clampPitch(float pitchDegrees, float minDegrees, float maxDegrees)
{
    // Deux champs que l'éditeur règle l'un sans l'autre peuvent s'inverser, et `std::clamp` d'un
    // min au-dessus du max est indéfini (une assertion de la libstdc++ en Debug) : les bornes se
    // remettent dans l'ordre.
    return std::clamp(pitchDegrees, std::min(minDegrees, maxDegrees),
                      std::max(minDegrees, maxDegrees));
}

float shortestYawDelta(float fromRadians, float toRadians)
{
    return std::remainder(toRadians - fromRadians, glm::two_pi<float>());
}

HorizontalBasis horizontalBasisFrom(float yawDegrees)
{
    const float yaw = glm::radians(yawDegrees);
    // Lacet nul : la caméra regarde vers -Z, la droite est +X. Le lacet croissant tourne vers la
    // gauche, comme le veut un repère direct avec Y vers le haut.
    return {.forward = {-std::sin(yaw), 0.0f, -std::cos(yaw)},
            .right = {std::cos(yaw), 0.0f, -std::sin(yaw)}};
}

glm::vec3 forwardOf(const FpsController& controller)
{
    const HorizontalBasis basis = horizontalBasisFrom(controller.yawDegrees);
    const float pitch = glm::radians(controller.pitchDegrees);
    return basis.forward * std::cos(pitch) + glm::vec3{0.0f, std::sin(pitch), 0.0f};
}

void applyFpsInput(Transform& transform, FpsController& controller, const FpsInput& input,
                   float seconds)
{
    // Le regard : les axes sont des vitesses (ADR-0017), d'où la multiplication par la durée.
    // Tourner à droite fait décroître le lacet, puisqu'il croît vers la gauche.
    const float lookScale = controller.lookDegreesPerUnit * seconds;
    controller.yawDegrees -= input.look.x * lookScale;
    controller.pitchDegrees = clampPitch(controller.pitchDegrees + (input.look.y * lookScale),
                                         controller.minPitchDegrees, controller.maxPitchDegrees);

    const HorizontalBasis basis = horizontalBasisFrom(controller.yawDegrees);
    const glm::vec3 direction =
        normalizeOrZero((basis.forward * input.move.y) + (basis.right * input.move.x) +
                        (glm::vec3{0.0f, 1.0f, 0.0f} * input.up));
    const float speed = controller.moveSpeed * (input.sprint ? controller.sprintMultiplier : 1.0f);
    transform.position += direction * speed * seconds;

    // La rotation du Transform est **calculée** depuis les angles : lacet d'abord, tangage ensuite,
    // sinon la caméra s'incline sur le côté dès qu'on combine les deux.
    transform.rotation = glm::angleAxis(glm::radians(controller.yawDegrees), glm::vec3{0, 1, 0}) *
                         glm::angleAxis(glm::radians(controller.pitchDegrees), glm::vec3{1, 0, 0});
}

} // namespace levain::scene
