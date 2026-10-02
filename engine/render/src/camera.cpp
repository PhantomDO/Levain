#include "levain/render/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace levain::render
{

glm::mat4 viewOf(const Camera& camera)
{
    return glm::lookAtRH(camera.position, camera.target, glm::vec3{0.0f, 1.0f, 0.0f});
}

glm::mat4 projectionOf(const Camera& camera, float aspectRatio)
{
    // RH_ZO : repère droitier, profondeur de 0 à 1 comme Vulkan et Direct3D 12. glm::perspective
    // suppose celle d'OpenGL (−1 à 1) : la moitié proche de la scène sortirait du volume de vue.
    return glm::perspectiveRH_ZO(camera.verticalFovRadians, aspectRatio, camera.nearPlane,
                                 camera.farPlane);
}

glm::mat4 viewProjectionOf(const Camera& camera, float aspectRatio)
{
    return projectionOf(camera, aspectRatio) * viewOf(camera);
}

} // namespace levain::render
