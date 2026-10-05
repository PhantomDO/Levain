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

/// Un rayon partant de la caméra : son origine, et une direction de longueur 1.
struct CameraRay
{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float length = 0.0f; ///< Du plan proche au plan lointain : plus que `farPlane` dans les coins.
};

/// Le point de l'écran `pixel`, compté depuis le coin **haut gauche** d'une image de `width` ×
/// `height` pixels, en coordonnées normalisées : x de −1 (gauche) à 1, y de −1 (bas) à 1 (haut). Le
/// piège : l'écran compte y vers le bas, l'espace de découpe vers le haut.
[[nodiscard]] glm::vec2 ndcOfPixel(glm::vec2 pixel, float width, float height);

/// Le rayon qui part de la caméra et passe par le point `ndc` de l'écran : ce que vise la souris.
/// C'est la vue-projection inversée, entre le plan proche et le plan lointain.
[[nodiscard]] CameraRay rayThrough(const Camera& camera, float aspectRatio, glm::vec2 ndc);

} // namespace levain::render
