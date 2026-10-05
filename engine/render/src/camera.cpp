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

glm::vec2 ndcOfPixel(glm::vec2 pixel, float width, float height)
{
    return {2.0f * pixel.x / width - 1.0f, 1.0f - 2.0f * pixel.y / height};
}

CameraRay rayThrough(const Camera& camera, float aspectRatio, glm::vec2 ndc)
{
    const glm::mat4 inverse = glm::inverse(viewProjectionOf(camera, aspectRatio));
    // Profondeur 0 au plan proche, 1 au plan lointain (RH_ZO).
    const glm::vec4 nearPoint = inverse * glm::vec4{ndc, 0.0f, 1.0f};
    const glm::vec4 farPoint = inverse * glm::vec4{ndc, 1.0f, 1.0f};
    const glm::vec3 from = glm::vec3(nearPoint) / nearPoint.w;
    const glm::vec3 to = glm::vec3(farPoint) / farPoint.w;
    return {
        .origin = from, .direction = glm::normalize(to - from), .length = glm::length(to - from)};
}

} // namespace levain::render
