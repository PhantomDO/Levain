// Jouer et Arrêter sans fenêtre ni GPU (ADR-0037, morceau 4) : l'ordre des étapes de `startPlay` et
// de `stopPlay` sur un vrai monde (scène, assets, physique), les refus qui laissent le monde comme
// il était, et ce que Play garde hors du monde. Le critère dans l'éditeur, lui, passe par le banc
// d'essai (`levain_editor_play`).

#include <bit>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/app/models.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/editor/play.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"

using levain::editor::PlaySession;
using levain::editor::PlayTarget;
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
    std::map<levain::assets::AssetId, levain::app::ModelGpu> models;
    levain::app::AnimationClock clock;
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

    [[nodiscard]] PlayTarget target()
    {
        return {world, ranges, fixedStep, models, clock, stepsPlayed};
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

bool sameBits(const Transform& a, const Transform& b)
{
    const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    return bits(a.position.x) == bits(b.position.x) && bits(a.position.y) == bits(b.position.y) &&
           bits(a.position.z) == bits(b.position.z) && bits(a.rotation.x) == bits(b.rotation.x) &&
           bits(a.rotation.y) == bits(b.rotation.y) && bits(a.rotation.z) == bits(b.rotation.z) &&
           bits(a.rotation.w) == bits(b.rotation.w);
}

std::vector<Transform> transformsOf(const std::vector<flecs::entity>& entities)
{
    std::vector<Transform> out;
    out.reserve(entities.size());
    for (const flecs::entity& entity : entities)
    {
        out.push_back(entity.get<Transform>());
    }
    return out;
}

} // namespace

TEST_CASE("jouer 120 pas puis arrêter rend la scène identique, et le dit")
{
    Bench bench;
    const std::vector<flecs::entity> crates = stack(bench.world, 12);
    const std::vector<Transform> before = transformsOf(crates);

    auto session = levain::editor::startPlay(bench.target());
    REQUIRE(session.has_value());
    bench.play(120);
    // Le jeu a bien déplacé quelque chose, sans quoi « identique » ne prouverait rien.
    CHECK_FALSE(sameBits(crates[11].get<Transform>(), before[11]));
    CHECK(levain::physics::bodyCount(bench.world.get<levain::physics::PhysicsWorld>()) > 0);

    const levain::editor::StopReport report = levain::editor::stopPlay(bench.target(), *session);

    CHECK_FALSE(report.refused);
    CHECK(report.steps == 120); // exige des pas : un Play de zéro pas ne prouverait rien
    CHECK(report.differences == 0);
    CHECK(levain::scene::differencesFrom(bench.world, session->snapshot).empty());
    const std::vector<Transform> after = transformsOf(crates);
    for (std::size_t i = 0; i < crates.size(); ++i)
    {
        CAPTURE(i);
        CHECK(sameBits(after[i], before[i]));
    }
    CHECK(
        levain::editor::stopLineOf(report).starts_with("editor.stop steps=120 differences=0 ms="));
}

TEST_CASE("après Stop, l'Édition n'a ni handle ni corps de Jolt, et le Play suivant les refait")
{
    Bench bench;
    const std::vector<flecs::entity> crates = stack(bench.world, 4);

    auto first = levain::editor::startPlay(bench.target());
    REQUIRE(first.has_value());
    const void* const joltOfThePlay = bench.world.get<levain::physics::PhysicsWorld>().state.get();
    bench.play(10);
    CHECK(levain::physics::bodyCount(bench.world.get<levain::physics::PhysicsWorld>()) > 0);
    REQUIRE(levain::editor::stopPlay(bench.target(), *first).differences == 0);
    // Un monde Jolt neuf, non celui du jeu dont on a seulement retiré les corps : ses identifiants
    // de corps, son état interne repartent de zéro (ADR-0037, décision 4).
    CHECK(bench.world.get<levain::physics::PhysicsWorld>().state.get() != joltOfThePlay);

    // L'invariant de l'Édition (ADR-0037, décision 3) : aucun corps, aucun handle, `BodyDirty`
    // partout où un collider attend le prochain pas.
    CHECK(levain::physics::bodyCount(bench.world.get<levain::physics::PhysicsWorld>()) == 0);
    CHECK(bench.world.count<levain::physics::BodyHandle>() == 0);
    for (const flecs::entity& crate : crates)
    {
        CHECK(crate.has<levain::physics::BodyDirty>());
    }
}

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
