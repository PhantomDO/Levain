// Jouer et Arrêter sans fenêtre ni GPU (ADR-0037, morceau 4), sur un vrai monde (scène, assets,
// physique). Ce premier test garde ce que Play demande à la physique : l'instantané ne copie pas
// les handles de Jolt. `startPlay` et `stopPlay` viennent ensuite, avec leurs tests.

#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/physics.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"

using levain::physics::Box;
using levain::physics::Collider;
using levain::physics::RigidBody;
using levain::scene::Transform;

namespace
{

constexpr float Step = 1.0f / 60.0f;

/// Le monde d'`App` en petit : les plages d'identifiants d'abord (aucune entité détruite), puis la
/// scène, les assets et, si on la veut, la physique.
struct Bench
{
    flecs::world world;
    levain::scene::IdRanges ranges;
    levain::scene::FixedStep fixedStep;
    int stepsPlayed = 0;

    explicit Bench(bool withPhysics = true)
    {
        ranges = levain::scene::createIdRanges(world).value();
        world.import<levain::scene::SceneModule>();
        world.import<levain::assets::AssetsModule>();
        if (withPhysics)
        {
            world.import<levain::physics::PhysicsModule>();
        }
    }

    /// Ce que fait `App` en jeu : des pas fixes, et le compte des pas.
    void play(int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            stepsPlayed += levain::scene::advanceWorld(world, fixedStep, Step);
        }
    }
};

/// Un sol et une pile de caisses, comme la vue physics du sandbox en petit.
std::vector<flecs::entity> stack(flecs::world& world, int crates)
{
    world.entity("sol")
        .set(Transform{.position = {0.0f, -0.5f, 0.0f}})
        .set(Collider{.shape = Box{{50.0f, 0.5f, 50.0f}}});
    std::vector<flecs::entity> made;
    for (int i = 0; i < crates; ++i)
    {
        const float jitter = 0.07f * static_cast<float>(i % 5);
        made.push_back(
            world.entity()
                .set(Transform{.position = {jitter, 0.6f + 1.1f * static_cast<float>(i), -jitter}})
                .set(Collider{.shape = Box{{0.5f, 0.5f, 0.5f}}})
                .set(RigidBody{}));
    }
    return made;
}

} // namespace

TEST_CASE("l'instantané ne copie pas les handles de Jolt : ils sont Transient")
{
    Bench bench;
    stack(bench.world, 3);
    bench.play(2); // les corps existent : chaque caisse porte un BodyHandle
    REQUIRE(bench.world.count<levain::physics::BodyHandle>() > 0);

    const auto snapshot = levain::scene::takeSnapshot(bench.world);

    REQUIRE(snapshot.has_value());
    REQUIRE_FALSE(snapshot->entities.empty());
    for (const levain::scene::EntityRecord& record : snapshot->entities)
    {
        for (const flecs::entity_t id : record.type)
        {
            CHECK(id != bench.world.id<levain::physics::BodyHandle>());
        }
    }
}
