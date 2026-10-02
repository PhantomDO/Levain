#pragma once

#include <glm/glm.hpp>

namespace levain::render
{

/// Une caméra perspective qui regarde un point. La caméra libre, pilotée par l'input, viendra en
/// M3.4.
struct Camera
{
    glm::vec3 position{0.0f, 1.5f, 3.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float verticalFovRadians = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

/// La vue : ce qui passe un point du monde dans le repère de la caméra, qui regarde vers −Z.
[[nodiscard]] glm::mat4 viewOf(const Camera& camera);

/// La projection : ce qui passe un point du repère de la caméra dans l'espace de découpe.
[[nodiscard]] glm::mat4 projectionOf(const Camera& camera, float aspectRatio);

/// Projection × vue : ce qui passe un point du monde dans l'espace de découpe (clip space).
[[nodiscard]] glm::mat4 viewProjectionOf(const Camera& camera, float aspectRatio);

} // namespace levain::render
