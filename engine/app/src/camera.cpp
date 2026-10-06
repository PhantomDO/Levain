#include "levain/app/camera.hpp"

#include <format>
#include <optional>
#include <string>

#include "levain/scene/components.hpp"

namespace levain::app
{

render::Camera cameraFrom(const CameraLens& lens, const glm::mat4& world)
{
    const glm::vec3 position{world[3]};
    return {.position = position,
            .target = position + glm::vec3(glm::mat3(world) * glm::vec3{0.0f, 0.0f, -1.0f}),
            .verticalFovRadians = glm::radians(lens.verticalFovDegrees),
            .nearPlane = lens.nearPlane,
            .farPlane = lens.farPlane};
}

core::Result<render::Camera>
renderCameraOf(const flecs::query<const CameraLens, const scene::WorldTransform>& cameras)
{
    std::optional<render::Camera> found;
    int count = 0;
    cameras.each(
        [&found, &count](const CameraLens& lens, const scene::WorldTransform& world)
        {
            found = cameraFrom(lens, world.matrix);
            ++count;
        });
    if (count == 1)
    {
        return *found;
    }
    if (count == 0)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "aucune caméra : aucune entité ne porte de CameraLens et de "
                               "WorldTransform");
    }
    // Plusieurs : le chemin rare, où l'on prend le temps de les nommer.
    std::string names;
    cameras.each(
        [&names](flecs::entity camera, const CameraLens&, const scene::WorldTransform&)
        { names += std::format("{}{}", names.empty() ? "" : ", ", camera.path().c_str()); });
    return core::makeError(core::ErrorCode::InvalidData,
                           std::format("{} caméras ({}) : une seule entité doit porter un "
                                       "CameraLens",
                                       count, names));
}

} // namespace levain::app
