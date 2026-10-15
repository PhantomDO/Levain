// Jouer deux fois la même scène donne les mêmes résultats, au bit près (ADR-0037, décision 4) : les
// corps et les personnages se construisent et avancent dans l'ordre des index, non des tables. Le
// déterminisme de Jolt : https://jrouwe.github.io/JoltPhysics/#deterministic-simulation

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/quaternion.hpp>

#include "levain/physics/character.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"

using levain::physics::Box;
using levain::physics::CharacterController;
using levain::physics::CharacterVelocity;
using levain::physics::Collider;
using levain::physics::Layer;
using levain::physics::RigidBody;
using levain::scene::Transform;

namespace
{

constexpr float Step = 1.0f / 60.0f;

/// Une étiquette posée puis retirée : l'entité change de table et revient, en **fin** de la table
/// d'origine. Son index, lui, n'a pas bougé : c'est la différence que le test exploite.
struct Detour
{
};

void advance(flecs::world& world, int steps)
{
    levain::scene::FixedStep step;
    for (int i = 0; i < steps; ++i)
    {
        levain::scene::advanceWorld(world, step, Step);
    }
}

flecs::world groundWorld()
{
    flecs::world world;
    world.import<levain::physics::PhysicsModule>();
    world.entity("Ground")
        .set(Transform{.position = {0.0f, -0.5f, 0.0f}})
        .set(Collider{.shape = Box{{50.0f, 0.5f, 50.0f}}});
    return world;
}

void goThroughAnotherTable(flecs::entity entity)
{
    entity.add<Detour>();
    entity.remove<Detour>();
}

/// Le même bit pour chaque flottant du Transform : pas de tolérance, c'est la promesse.
bool sameBits(const Transform& a, const Transform& b)
{
    const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    return bits(a.position.x) == bits(b.position.x) && bits(a.position.y) == bits(b.position.y) &&
           bits(a.position.z) == bits(b.position.z) && bits(a.rotation.x) == bits(b.rotation.x) &&
           bits(a.rotation.y) == bits(b.rotation.y) && bits(a.rotation.z) == bits(b.rotation.z) &&
           bits(a.rotation.w) == bits(b.rotation.w);
}

/// Un tas de caisses lâchées en désordre sur le sol. `detourOf` : l'indice de celle que l'on
/// renvoie en fin de table avant de jouer, ou -1.
std::vector<flecs::entity> dropCrates(flecs::world& world, int detourOf)
{
    std::vector<flecs::entity> crates;
    for (int i = 0; i < 12; ++i)
    {
        const float jitter = 0.07f * static_cast<float>(i % 5);
        crates.push_back(
            world.entity()
                .set(Transform{.position = {jitter, 0.6f + 1.1f * static_cast<float>(i), -jitter}})
                .set(Collider{.shape = Box{{0.5f, 0.5f, 0.5f}}})
                .set(RigidBody{}));
    }
    if (detourOf >= 0)
    {
        goThroughAnotherTable(crates[static_cast<std::size_t>(detourOf)]);
    }
    return crates;
}

} // namespace

TEST_CASE("inIdOrder range par index, jamais par identifiant entier")
{
    // La génération est dans les bits hauts : une entité réveillée à la génération 5 passerait
    // avant toutes les autres si l'on triait l'identifiant entier.
    const flecs::entity_t lowIndexNewGeneration = std::uint64_t{5} * 0x100000000ull + 7u;
    const flecs::entity_t highIndexOldGeneration = 9u;
    CHECK(levain::physics::indexOfEntity(lowIndexNewGeneration) == 7u);
    CHECK(levain::physics::inIdOrder(lowIndexNewGeneration, nullptr, highIndexOldGeneration,
                                     nullptr) < 0);
    CHECK(levain::physics::inIdOrder(highIndexOldGeneration, nullptr, lowIndexNewGeneration,
                                     nullptr) > 0);
    CHECK(levain::physics::inIdOrder(lowIndexNewGeneration, nullptr, lowIndexNewGeneration,
                                     nullptr) == 0);
}

TEST_CASE("deux mondes dont l'ordre des tables diffère rejouent la même pile, au bit près")
{
    flecs::world straight = groundWorld();
    flecs::world detoured = groundWorld();
    const auto a = dropCrates(straight, -1);
    // Une caisse du milieu de la pile revient en fin de table : construite en dernier si l'on suit
    // les tables, et ses contacts n'ont plus le même ordre.
    const auto b = dropCrates(detoured, 5);
    advance(straight, 240);
    advance(detoured, 240);

    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CAPTURE(i);
        REQUIRE(a[i].id() == b[i].id());
        CHECK(sameBits(a[i].get<Transform>(), b[i].get<Transform>()));
    }
    // La pile est retombée en colonne sur le sol (lâchée de 13,7 m, la douzième se pose vers
    // 11,4 m) : le test ne compare pas deux scènes immobiles.
    CHECK(a[11].get<Transform>().position.y > 10.5f);
    CHECK(a[11].get<Transform>().position.y < 12.0f);
    CHECK(a[0].get<Transform>().position.y < 0.6f);
}

TEST_CASE("huit personnages qui poussent une caisse : l'ordre des tables ne change rien")
{
    const auto play = [](bool detour)
    {
        flecs::world world = groundWorld();
        const flecs::entity crate = world.entity()
                                        .set(Transform{.position = {0.0f, 0.5f, 0.0f}})
                                        .set(Collider{.shape = Box{{0.5f, 0.5f, 0.5f}}})
                                        .set(RigidBody{.mass = 7.3f});
        // Huit personnages en couronne, qui arrivent ensemble sur la caisse : la
        // somme de leurs poussées sur la caisse se fait dans l'ordre où ils avancent.
        std::vector<flecs::entity> walkers;
        for (int i = 0; i < 8; ++i)
        {
            const float angle = 0.9f * static_cast<float>(i) + 0.2f;
            const float radius = 1.6f;
            walkers.push_back(world.entity()
                                  .set(Transform{.position = {radius * std::cos(angle), 0.01f,
                                                              radius * std::sin(angle)}})
                                  .set(CharacterController{.maxPushForce = 1.0e5f}));
        }
        // Avant leur construction. Ici l'ordre de construction ne se voit pas (la planche, plus
        // bas, le montre) : seul l'ordre où ils avancent compte, et il change juste après.
        if (detour)
        {
            goThroughAnotherTable(walkers[3]);
        }
        advance(world, 1);
        // Une fois construits, un autre repart en fin de table : l'ordre où ils avancent change.
        if (detour)
        {
            goThroughAnotherTable(walkers[5]);
        }
        // Chacun marche vers la caisse, vitesse posée une fois (elle persiste).
        float pace = 2.0f;
        for (flecs::entity walker : walkers)
        {
            const glm::vec3 toCrate = -glm::normalize(walker.get<Transform>().position) * pace;
            walker.get_mut<CharacterVelocity>().value = {toCrate.x, 0.0f, toCrate.z};
            pace += 0.17f;
        }
        advance(world, 120);
        std::vector<Transform> result{crate.get<Transform>()};
        for (flecs::entity walker : walkers)
        {
            result.push_back(walker.get<Transform>());
        }
        return result;
    };

    const std::vector<Transform> straight = play(false);
    const std::vector<Transform> detoured = play(true);
    REQUIRE(straight.size() == detoured.size());
    for (std::size_t i = 0; i < straight.size(); ++i)
    {
        CAPTURE(i);
        CHECK(sameBits(straight[i], detoured[i]));
    }
    // La caisse a été poussée : sinon l'ordre des personnages n'aurait rien à changer.
    CHECK(glm::length(glm::vec2{straight[0].position.x, straight[0].position.z}) > 0.1f);
}

TEST_CASE("une planche posée sur quatre personnages : l'ordre de construction ne change rien")
{
    // Les personnages ont chacun un corps intérieur, et Jolt trie les contacts par identifiant de
    // corps : l'ordre où BuildCharacters les construit se voit dès qu'ils portent le même poids.
    const auto play = [](bool detour)
    {
        flecs::world world = groundWorld();
        const flecs::entity slab =
            world.entity()
                .set(Transform{.position = {0.05f, 2.2f, 0.03f},
                               .rotation = glm::angleAxis(0.05f, glm::vec3{0.0f, 0.0f, 1.0f})})
                .set(Collider{.shape = Box{{0.9f, 0.08f, 0.9f}}})
                .set(RigidBody{.mass = 400.0f});
        std::vector<flecs::entity> walkers;
        for (int i = 0; i < 4; ++i)
        {
            const float x = (i % 2 == 0 ? -0.45f : 0.45f) + 0.01f * static_cast<float>(i);
            walkers.push_back(world.entity()
                                  .set(Transform{.position = {x, 0.01f, i < 2 ? -0.45f : 0.45f}})
                                  .set(CharacterController{}));
        }
        if (detour) // avant leur construction : ils sont construits dans un autre ordre de table
        {
            goThroughAnotherTable(walkers[0]);
            goThroughAnotherTable(walkers[2]);
        }
        advance(world, 240);
        std::vector<Transform> result{slab.get<Transform>()};
        for (flecs::entity walker : walkers)
        {
            result.push_back(walker.get<Transform>());
        }
        return result;
    };

    const std::vector<Transform> straight = play(false);
    const std::vector<Transform> detoured = play(true);
    REQUIRE(straight.size() == detoured.size());
    for (std::size_t i = 0; i < straight.size(); ++i)
    {
        CAPTURE(i);
        CHECK(sameBits(straight[i], detoured[i]));
    }
    CHECK(straight[0].position.y > 1.0f); // posée sur les têtes, pas tombée au sol
}
