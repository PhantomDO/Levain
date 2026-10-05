#pragma once

#include <format>

#include <flecs.h>
#include <glm/glm.hpp>

#include "levain/physics/components.hpp"
#include "levain/scene/components.hpp"
#include "levain/terrain/collision.hpp"
#include "levain/terrain/heightmap.hpp"

/// La vallée de `levain_sandbox --view terrain` dans la physique (M6.2) : celle de la démo, et
/// celle que vérifie `terrain_test.cpp`. Un seul endroit, pour que le test soit la démo.
namespace levain::sandbox
{

/// La hauteur de l'eau du lac, sous le fond plat de la vallée : elle ne remplit que son creux.
inline constexpr float LakeLevel = -1.5f;

/// Le terrain en grille de hauteurs, le lac en volume déclencheur nommé « lac », et 64 caisses
/// lâchées au-dessus de la rive est, d'où elles roulent vers l'eau. `Tags` s'ajoutent à chaque
/// caisse (le sandbox y met celui de ses cubes dessinés).
template <typename... Tags>
void spawnLakeShoreCrates(flecs::world& world, const terrain::Heightmap& heightmap,
                          const terrain::ValleySettings& valley)
{
    world.entity("terrain").set(scene::Transform{}).set(terrain::colliderOf(heightmap));
    // Le lac : un pavé sous la surface de l'eau, inscrit dans son disque.
    const float half = valley.lakeRadius * 0.6f;
    world.entity("lac")
        .set(scene::Transform{
            .position = {valley.lakeCenter.x, LakeLevel - 4.0f, valley.lakeCenter.y}})
        .set(physics::Collider{.shape = physics::Box{{half, 4.0f, half}},
                               .layer = physics::Layer::Sensor});
    // La rive est, là où le fond remonte vers la crête : 8 × 8 caisses, 3 m au-dessus du sol.
    const glm::vec2 shore = valley.lakeCenter + glm::vec2{valley.lakeRadius * 0.8f, 0.0f};
    for (int z = 0; z < 8; ++z)
    {
        for (int x = 0; x < 8; ++x)
        {
            const glm::vec2 spot = shore + glm::vec2{static_cast<float>(x - 4) * 3.0f,
                                                     static_cast<float>(z - 4) * 3.0f};
            const flecs::entity crate =
                world.entity(std::format("crate_{}_{}", x, z).c_str())
                    .set(scene::Transform{
                        .position = {spot.x, terrain::heightAt(heightmap, spot) + 3.0f, spot.y}})
                    .set(physics::Collider{.shape = physics::Box{}})
                    .set(physics::RigidBody{.mass = 20.0f});
            (crate.add<Tags>(), ...);
        }
    }
}

} // namespace levain::sandbox
