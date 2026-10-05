#include "levain/terrain/collision.hpp"

namespace levain::terrain
{

std::shared_ptr<const physics::HeightField> heightFieldOf(const Heightmap& heightmap)
{
    return std::make_shared<const physics::HeightField>(physics::HeightField{
        .size = heightmap.size, .spacing = heightmap.spacing, .heights = heightmap.heights});
}

physics::Collider colliderOf(const Heightmap& heightmap)
{
    return {.shape = physics::HeightFieldShape{heightFieldOf(heightmap)},
            .layer = physics::Layer::Static};
}

} // namespace levain::terrain
