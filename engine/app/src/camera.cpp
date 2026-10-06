#include "levain/app/camera.hpp"

#include <format>
#include <vector>

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

core::Result<render::Camera> renderCameraOf(const flecs::world& world)
{
    std::vector<flecs::entity> cameras;
    world.each([&cameras](flecs::entity entity, const CameraLens&, const scene::WorldTransform&)
               { cameras.push_back(entity); });
    if (cameras.size() == 1)
    {
        const flecs::entity camera = cameras.front();
        return cameraFrom(camera.get<CameraLens>(), camera.get<scene::WorldTransform>().matrix);
    }
    if (cameras.empty())
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "aucune caméra : aucune entité ne porte de CameraLens et de "
                               "WorldTransform");
    }
    std::string names;
    for (const flecs::entity& camera : cameras)
    {
        names += std::format("{}{}", names.empty() ? "" : ", ", camera.path().c_str());
    }
    return core::makeError(core::ErrorCode::InvalidData,
                           std::format("{} caméras ({}) : une seule entité doit porter un "
                                       "CameraLens",
                                       cameras.size(), names));
}

} // namespace levain::app
