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
#include "levain/core/log_ring.hpp"
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

    const levain::editor::StopReport report =
        levain::editor::stopPlay(bench.target(), *session, "play");

    CHECK_FALSE(report.refused);
    CHECK(report.steps == 120);   // exige des pas : un Play de zéro pas ne prouverait rien
    CHECK(report.from == "play"); // le mode quitté est celui que l'appelant a donné
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
    REQUIRE(levain::editor::stopPlay(bench.target(), *first, "play").differences == 0);
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

TEST_CASE("jouer deux fois de suite : les mêmes identifiants, et la physique rejoue au bit près")
{
    Bench bench;
    const std::vector<flecs::entity> crates = stack(bench.world, 12);
    const std::int32_t entitiesBefore = bench.world.count<Transform>();

    auto first = levain::editor::startPlay(bench.target());
    REQUIRE(first.has_value());
    bench.play(90);
    const std::vector<Transform> firstRun = transformsOf(crates);
    REQUIRE(levain::editor::stopPlay(bench.target(), *first, "play").differences == 0);

    auto second = levain::editor::startPlay(bench.target());
    REQUIRE(second.has_value());
    bench.play(90);
    const std::vector<Transform> secondRun = transformsOf(crates);
    REQUIRE(levain::editor::stopPlay(bench.target(), *second, "play").differences == 0);

    CHECK(bench.world.count<Transform>() == entitiesBefore); // aucune croissance
    for (std::size_t i = 0; i < crates.size(); ++i)
    {
        CAPTURE(i);
        CHECK(bench.world.is_alive(crates[i].id())); // le même identifiant, génération comprise
        CHECK(sameBits(firstRun[i], secondRun[i]));
    }
}

TEST_CASE("Stop rend aussi l'accumulateur du pas fixe")
{
    Bench bench;
    stack(bench.world, 1);
    bench.fixedStep.accumulator = 0.4f * Step;

    auto session = levain::editor::startPlay(bench.target());
    REQUIRE(session.has_value());
    bench.fixedStep.accumulator = 0.9f * Step; // le jeu l'a fait avancer
    bench.play(3);
    REQUIRE_FALSE(levain::editor::stopPlay(bench.target(), *session, "play").refused);

    CHECK(bench.fixedStep.accumulator == 0.4f * Step);
}

TEST_CASE("une entité née en jeu part à Stop, une détruite en jeu revient avec son identifiant")
{
    Bench bench;
    const std::vector<flecs::entity> crates = stack(bench.world, 3);
    const flecs::entity_t doomed = crates[1].id();

    auto session = levain::editor::startPlay(bench.target());
    REQUIRE(session.has_value());
    const flecs::entity born = bench.world.entity("née").set(Transform{});
    CHECK(levain::scene::bornInPlay(bench.world, born));
    crates[1].destruct();
    bench.play(5);
    const levain::editor::StopReport report =
        levain::editor::stopPlay(bench.target(), *session, "play");

    CHECK(report.differences == 0);
    CHECK_FALSE(bench.world.is_alive(born.id()));
    CHECK(bench.world.is_alive(doomed));
    CHECK(bench.world.lookup("née") == flecs::entity{});
}

TEST_CASE("un Play refusé laisse le monde, la physique et les plages comme ils étaient")
{
    Bench bench;
    stack(bench.world, 2);
    // Des assets déjà retenus : deux Play sans Stop entre eux. `holdAssetsInUse` refuse.
    REQUIRE_FALSE(levain::assets::holdAssetsInUse(bench.world.get_mut<levain::assets::AssetUsage>())
                      .has_value());
    const void* const jolt = bench.world.get<levain::physics::PhysicsWorld>().state.get();

    const auto refused = levain::editor::startPlay(bench.target());

    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("retenus") != std::string::npos);
    // Le monde Jolt est le même, et les nouvelles entités prennent encore la plage de l'édition.
    CHECK(bench.world.get<levain::physics::PhysicsWorld>().state.get() == jolt);
    CHECK_FALSE(levain::scene::bornInPlay(bench.world, bench.world.entity()));
    levain::assets::releaseHeldAssets(bench.world.get_mut<levain::assets::AssetUsage>());
}

TEST_CASE("Stop en mode différé est refusé : on reste en jeu, rien n'a changé")
{
    Bench bench;
    const std::vector<flecs::entity> crates = stack(bench.world, 2);
    auto session = levain::editor::startPlay(bench.target());
    REQUIRE(session.has_value());
    bench.play(10);
    const Transform moved = crates[1].get<Transform>();

    bench.world.defer_begin();
    const levain::editor::StopReport refused =
        levain::editor::stopPlay(bench.target(), *session, "simulate");
    bench.world.defer_end();

    CHECK(refused.refused);
    CHECK_FALSE(refused.refusal.empty());
    CHECK(refused.from == "simulate"); // un Stop refusé dit aussi d'où il partait
    CHECK(sameBits(crates[1].get<Transform>(), moved));
    // Hors du mode différé, le même Stop passe.
    CHECK_FALSE(levain::editor::stopPlay(bench.target(), *session, "play").refused);
}

TEST_CASE("sans module de physique, Jouer et Arrêter ne créent pas de monde Jolt")
{
    Bench bench(false);
    bench.world.entity("a").set(Transform{});

    auto session = levain::editor::startPlay(bench.target());
    REQUIRE(session.has_value());
    bench.play(3);
    const levain::editor::StopReport report =
        levain::editor::stopPlay(bench.target(), *session, "play");

    CHECK(report.differences == 0);
    CHECK_FALSE(bench.world.has<levain::physics::PhysicsWorld>());
}

TEST_CASE("la ligne de la CI est sous son nom stable, des mots sans langue puis la phrase")
{
    levain::editor::StopReport report;
    report.steps = 120;
    report.differences = 0;
    report.milliseconds = 3.25;
    report.from = "simulate";

    const std::string line = levain::editor::stopLineOf(report);

    CHECK(line.starts_with("editor.stop steps=120 differences=0 ms="));
    // Le mode quitté, après les millisecondes : la CI sait si Stop sort de Jouer ou de Simuler.
    CHECK(line.contains("ms=3.2 from=simulate -- "));
    report.from = "play";
    CHECK(levain::editor::stopLineOf(report).contains(" from=play -- "));
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

TEST_CASE("Stop en mode différé est refusé aussi sans module de physique, la session reste")
{
    Bench bench(false);
    const flecs::entity a = bench.world.entity("a").set(Transform{});
    auto session = levain::editor::startPlay(bench.target());
    REQUIRE(session.has_value());
    const flecs::entity born = bench.world.entity("née").set(Transform{});

    bench.world.defer_begin();
    const levain::editor::StopReport refused =
        levain::editor::stopPlay(bench.target(), *session, "play");
    bench.world.defer_end();

    CHECK(refused.refused);
    CHECK_FALSE(refused.refusal.empty());
    CHECK(bench.world.is_alive(born.id())); // rien n'a été détruit
    // La session sert encore : le même Stop, hors du mode différé, rend la scène.
    const levain::editor::StopReport stopped =
        levain::editor::stopPlay(bench.target(), *session, "play");
    CHECK_FALSE(stopped.refused);
    CHECK(stopped.differences == 0);
    CHECK(bench.world.is_alive(a.id()));
    CHECK_FALSE(bench.world.is_alive(born.id()));
}

TEST_CASE(
    "un monde Jolt refusé à Play rend les assets : l'Édition est intacte, le Play suivant part")
{
    Bench bench;
    stack(bench.world, 2);
    // Un handle resté posé en Édition (un bug de l'appelant) : `freshPhysicsWorld` refuse, après
    // que les assets ont été retenus.
    const flecs::entity stale = bench.world.entity("resté").set(levain::physics::BodyHandle{});

    const auto refused = levain::editor::startPlay(bench.target());

    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("BodyHandle") != std::string::npos);
    CHECK_FALSE(levain::scene::bornInPlay(bench.world, bench.world.entity())); // plage de l'édition
    // Les assets sont rendus : après la correction, Play n'est pas refusé « déjà retenus ».
    stale.destruct();
    auto again = levain::editor::startPlay(bench.target());
    CHECK(again.has_value());
    if (again)
    {
        auto session = std::move(*again);
        CHECK(levain::editor::stopPlay(bench.target(), session, "play").differences == 0);
    }
}

TEST_CASE(
    "la phrase de la ligne de la CI accorde « différence » comme le français : 0 et 1 au singulier")
{
    levain::editor::StopReport report;
    const auto phraseWith = [&report](std::size_t differences)
    {
        report.differences = differences;
        return levain::editor::stopLineOf(report);
    };

    CHECK(phraseWith(0).find("0 différence,") != std::string::npos);
    CHECK(phraseWith(1).find("1 différence,") != std::string::npos);
    CHECK(phraseWith(2).find("2 différences,") != std::string::npos);
    // Les mots stables, eux, ne changent pas.
    CHECK(phraseWith(2).starts_with("editor.stop steps=0 differences=2 ms="));
}

TEST_CASE("logStop dit toujours pourquoi la restauration a échoué, même sans différence comptée")
{
    const levain::core::LogCapture capture = levain::core::captureLog(16);
    levain::editor::StopReport report;
    report.restoreError = "garde de test : la liste des enfants n'a pas la bonne taille";

    levain::editor::logStop(report);

    bool said = false;
    for (const auto& entry : levain::core::snapshotOf(*capture.ring).entries)
    {
        said = said || (entry.level == levain::core::LogLevel::Error &&
                        entry.text.find("garde de test") != std::string::npos);
    }
    CHECK(said);
}
