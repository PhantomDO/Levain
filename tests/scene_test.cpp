#include <cmath>
#include <string>
#include <string_view>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/constants.hpp>

#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/motion.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/transform.hpp"

using levain::scene::FixedStep;
using levain::scene::PreviousTransform;
using levain::scene::Transform;
using levain::scene::Velocity;
using levain::scene::worldPosition;
using levain::scene::WorldTransform;

TEST_CASE("applyVelocity avance la position de la vitesse multipliée par la durée")
{
    Transform transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    levain::scene::applyVelocity(transform, Velocity{.linear = {2.0f, 0.0f, -4.0f}}, 0.5f);

    CHECK(transform.position.x == doctest::Approx(2.0f));
    CHECK(transform.position.y == doctest::Approx(2.0f));
    CHECK(transform.position.z == doctest::Approx(1.0f));
}

TEST_CASE("le système ApplyVelocity déplace les entités à chaque tour du monde")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity moving =
        world.entity().set(Transform{}).set(Velocity{.linear = {4.0f, 0.0f, 0.0f}});
    const flecs::entity still = world.entity().set(Transform{});

    FixedStep step;
    for (int frame = 0; frame < 15; ++frame) // 15 images de 1/60 s, donc 15 pas
    {
        levain::scene::advanceWorld(world, step, 1.0f / 60.0f);
    }

    CHECK(moving.get<Transform>().position.x == doctest::Approx(1.0f));
    CHECK(still.get<Transform>().position.x == doctest::Approx(0.0f)); // sans Velocity, rien
}

TEST_CASE("les systèmes de simulation ne tournent pas dans le pipeline du rendu")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity system = world.lookup("levain::scene::SceneModule::ApplyVelocity");
    const flecs::entity moving =
        world.entity().set(Transform{}).set(Velocity{.linear = {4.0f, 0.0f, 0.0f}});

    REQUIRE(system.is_valid());
    CHECK(system.has<levain::scene::Simulation>());

    // Une passe de rendu seule ne simule rien : sans ça, la simulation suivrait le rythme des
    // images, et le critère de M3.3 tomberait.
    world.progress(1.0f);
    CHECK(moving.get<Transform>().position.x == doctest::Approx(0.0f));
}

TEST_CASE("les phases de simulation tournent dans leur ordre, pas dans celui de leur déclaration")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    std::string order;

    // Déclarés à l'envers : c'est ce qui arrive à un plugin gameplay, qui importe la physique
    // avant de déclarer ses systèmes (ADR-0026).
    world.system("AfterPhysics")
        .kind<levain::scene::PostPhysics>()
        .run([&order](flecs::iter&) { order += "post "; });
    world.system("Step").kind<levain::scene::Physics>().run([&order](flecs::iter&)
                                                            { order += "physics "; });
    world.system("Gameplay")
        .kind<levain::scene::Simulation>()
        .run([&order](flecs::iter&) { order += "gameplay "; });

    FixedStep step;
    levain::scene::advanceWorld(world, step, 1.0f / 60.0f);
    CHECK(order == "gameplay physics post ");

    // Le pipeline du rendu n'en exécute aucune : elles ne portent pas `flecs::Phase`.
    order.clear();
    world.progress(1.0f / 60.0f);
    CHECK(order.empty());
}

TEST_CASE("les champs des composants se lisent et s'écrivent en JSON, comme dans l'explorer")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    Transform transform;
    transform.position = {2.5f, 0.0f, 0.0f};
    const flecs::entity entity = world.entity("cube").set(transform);

    // L'explorer lit le monde en JSON (addon REST) : sans la réflexion, il ne verrait qu'un bloc
    // d'octets.
    const flecs::string json = entity.to_json();
    CHECK(std::string_view{json.c_str()}.find(R"("position":{"x":2.5)") != std::string_view::npos);

    // Et il écrit de la même façon : c'est ce que fait l'explorer quand on modifie un champ.
    entity.set_json<Transform>(R"({"position":{"x":7, "y":1, "z":0}})");
    CHECK(entity.get<Transform>().position.x == doctest::Approx(7.0f));
    CHECK(entity.get<Transform>().position.y == doctest::Approx(1.0f));
    CHECK(entity.get<Transform>().rotation.w ==
          doctest::Approx(1.0f)); // champs absents : inchangés
}

TEST_CASE("localMatrix compose l'échelle, la rotation puis la translation")
{
    Transform transform;
    transform.position = {10.0f, 0.0f, 0.0f};
    transform.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
    transform.scale = {2.0f, 2.0f, 2.0f};

    // Un quart de tour autour de Y met l'axe X (mis à l'échelle) sur -Z, et la translation reste
    // celle du parent : c'est l'ordre échelle, rotation, translation.
    const glm::vec4 point =
        levain::scene::localMatrix(transform) * glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
    CHECK(point.x == doctest::Approx(10.0f));
    CHECK(point.z == doctest::Approx(-2.0f));
}

TEST_CASE("un enfant suit son parent déplacé")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity parent =
        world.entity("parent").set(Transform{.position = {1.0f, 0.0f, 0.0f}});
    // La hiérarchie passe par le composant flecs::Parent, jamais par child_of (ADR-0015).
    const flecs::entity child = world.entity(flecs::Parent{parent}, "enfant")
                                    .set(Transform{.position = {0.0f, 2.0f, 0.0f}});

    FixedStep step;
    levain::scene::advanceWorld(world, step, 0.0f);
    CHECK(worldPosition(child.get<WorldTransform>()).x == doctest::Approx(1.0f));
    CHECK(worldPosition(child.get<WorldTransform>()).y == doctest::Approx(2.0f));

    parent.set(Transform{.position = {5.0f, 0.0f, 0.0f}});
    levain::scene::advanceWorld(world, step, 0.0f);
    CHECK(worldPosition(child.get<WorldTransform>()).x == doctest::Approx(5.0f));
    CHECK(worldPosition(child.get<WorldTransform>()).y == doctest::Approx(2.0f));

    // Le Transform de l'enfant, lui, n'a pas bougé : il est dans le repère de son parent.
    CHECK(child.get<Transform>().position.x == doctest::Approx(0.0f));
}

TEST_CASE("un enfant tourne et grandit avec son parent")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity parent = world.entity().set(
        Transform{.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f}),
                  .scale = {3.0f, 3.0f, 3.0f}});
    const flecs::entity child =
        world.entity(flecs::Parent{parent}, nullptr).set(Transform{.position = {1.0f, 0.0f, 0.0f}});

    FixedStep step;
    levain::scene::advanceWorld(world, step, 0.0f);

    // Un mètre devant un parent tourné d'un quart de tour et trois fois plus grand : trois mètres
    // sur -Z.
    CHECK(worldPosition(child.get<WorldTransform>()).x == doctest::Approx(0.0f));
    CHECK(worldPosition(child.get<WorldTransform>()).z == doctest::Approx(-3.0f));
}

TEST_CASE("un petit-enfant suit, même si la hiérarchie est bâtie dans le désordre")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const Transform oneOnX{.position = {1.0f, 0.0f, 0.0f}};
    // L'ordre de création met la table du petit-enfant dans le cache de la requête avant celle de
    // son parent : sans le tri par profondeur (EcsQueryGroupByOrdered), le petit-enfant est calculé
    // avec la matrice de la frame précédente et se retrouve à x = 1 au lieu de 3.
    const flecs::entity child = world.entity().set(oneOnX);
    const flecs::entity grandChild = world.entity(flecs::Parent{child}, nullptr).set(oneOnX);
    const flecs::entity root = world.entity().set(oneOnX);
    child.set(flecs::Parent{root});

    FixedStep step;
    levain::scene::advanceWorld(world, step, 0.0f);

    CHECK(worldPosition(grandChild.get<WorldTransform>()).x == doctest::Approx(3.0f));
}

TEST_CASE("le WorldTransform vient avec le Transform, et se lit en JSON")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity entity = world.entity().set(Transform{.position = {2.0f, 0.0f, 0.0f}});

    // Le trait With : personne n'ajoute WorldTransform à la main.
    REQUIRE(entity.has<WorldTransform>());
    FixedStep step;
    levain::scene::advanceWorld(world, step, 0.0f);

    // Et l'explorer le voit, comme les autres composants (réflexion de la matrice).
    const flecs::string json = entity.to_json();
    CHECK(std::string_view{json.c_str()}.find(R"("matrix":[)") != std::string_view::npos);
}

TEST_CASE("planSteps découpe le temps des images en pas entiers")
{
    FixedStep step;

    SUBCASE("une image plus courte qu'un pas n'en déclenche aucun")
    {
        const levain::scene::StepPlan plan = levain::scene::planSteps(step, 1.0f / 144.0f);
        CHECK(plan.steps == 0);
        CHECK(plan.alpha == doctest::Approx(60.0f / 144.0f)); // le reste, en fraction de pas
    }

    SUBCASE("une image de 30 images/s en déclenche deux")
    {
        CHECK(levain::scene::planSteps(step, 1.0f / 30.0f).steps == 2);
    }

    SUBCASE("le reste s'accumule d'une image à l'autre")
    {
        CHECK(levain::scene::planSteps(step, 0.01f).steps == 0);
        CHECK(levain::scene::planSteps(step, 0.01f).steps ==
              1); // 0,02 s : un pas, et 0,0033 de reste
    }

    SUBCASE("une image très lente ne déclenche pas plus que le plafond")
    {
        // Le garde-fou : 1 s d'un coup ne demande pas 60 pas, sinon l'image suivante serait plus
        // lente encore (ADR-0016).
        const levain::scene::StepPlan plan = levain::scene::planSteps(step, 1.0f);
        CHECK(plan.steps == step.maxStepsPerFrame);
        CHECK(step.accumulator < step.stepSeconds); // le temps en trop est abandonné
    }
}

/// La position d'une entité après `ticks` pas de simulation, en avançant le monde par images de
/// `frameSeconds`. C'est le critère de M3.3 : le résultat ne doit pas dépendre de la cadence.
namespace
{
glm::vec3 simulate(float frameSeconds, int ticks)
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity entity = world.entity()
                                     .set(Transform{.position = {1.0f, 2.0f, 3.0f}})
                                     .set(Velocity{.linear = {0.3f, -9.81f, 0.7f}});
    FixedStep step;
    for (int done = 0; done < ticks;)
    {
        done += levain::scene::advanceWorld(world, step, frameSeconds);
    }
    return entity.get<Transform>().position;
}
} // namespace

TEST_CASE("la simulation donne le même état au bit près, quelle que soit la cadence du rendu")
{
    constexpr int Ticks = 120;
    const glm::vec3 at30 = simulate(1.0f / 30.0f, Ticks);
    const glm::vec3 at60 = simulate(1.0f / 60.0f, Ticks);
    const glm::vec3 at144 = simulate(1.0f / 144.0f, Ticks);

    CHECK(at30 == at60); // au bit près : pas d'Approx ici, c'est tout l'intérêt du pas fixe
    CHECK(at30 == at144);
    CHECK(at30.y == doctest::Approx(2.0f - 9.81f * 2.0f)); // 120 pas = 2 s de chute
}

TEST_CASE("le rendu affiche l'entre-deux des pas de simulation")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity moving =
        world.entity().set(Transform{}).set(Velocity{.linear = {6.0f, 0.0f, 0.0f}});
    const flecs::entity decor = world.entity().set(Transform{.position = {9.0f, 0.0f, 0.0f}});

    // Ce que la simulation déplace porte son état précédent ; le décor, non (ADR-0016).
    CHECK(moving.has<PreviousTransform>());
    CHECK_FALSE(decor.has<PreviousTransform>());

    FixedStep step;
    levain::scene::advanceWorld(world, step, 1.0f / 60.0f);  // un pas : 0,1 unité parcourue
    levain::scene::advanceWorld(world, step, 1.0f / 120.0f); // une demi-image : aucun pas

    CHECK(world.get<levain::scene::RenderAlpha>().value == doctest::Approx(0.5f));
    CHECK(moving.get<Transform>().position.x == doctest::Approx(0.1f)); // la simulation est devant
    CHECK(worldPosition(moving.get<WorldTransform>()).x ==
          doctest::Approx(0.05f)); // le rendu, à mi-chemin
    CHECK(worldPosition(decor.get<WorldTransform>()).x == doctest::Approx(9.0f));
}

TEST_CASE("une entité qui naît ou qu'on téléporte ne traîne pas son ancienne position")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    FixedStep step;
    const flecs::entity moving =
        world.entity().set(Transform{.position = {5.0f, 0.0f, 0.0f}}).set(Velocity{});

    // Aucun pas de simulation n'a encore tourné : sans l'observateur, l'état précédent serait
    // l'origine du monde et l'entité s'afficherait là-bas.
    levain::scene::advanceWorld(world, step, 0.0f);
    CHECK(worldPosition(moving.get<WorldTransform>()).x == doctest::Approx(5.0f));

    moving.set(Transform{.position = {-40.0f, 0.0f, 0.0f}}); // téléportation
    levain::scene::advanceWorld(world, step, 0.0f);
    CHECK(worldPosition(moving.get<WorldTransform>()).x == doctest::Approx(-40.0f));
}

TEST_CASE("nlerpShortestPath prend le chemin court entre deux rotations opposées")
{
    const glm::quat start = glm::angleAxis(0.0f, glm::vec3{0.0f, 1.0f, 0.0f});
    // Le même quart de tour, écrit avec le signe opposé : la même rotation, l'autre chemin.
    const glm::quat quarter = -glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});

    const glm::quat half = levain::scene::nlerpShortestPath(start, quarter, 0.5f);
    const glm::vec3 turned = half * glm::vec3{1.0f, 0.0f, 0.0f};

    // À mi-chemin d'un quart de tour : 45°, donc x et -z égaux. Sans le test de signe, l'objet
    // partirait à 135° dans l'autre sens.
    CHECK(turned.x == doctest::Approx(std::sqrt(2.0f) / 2.0f));
    CHECK(turned.z == doctest::Approx(-std::sqrt(2.0f) / 2.0f));
}

TEST_CASE("un monde qui n'avance que son rendu compose quand même ses matrices")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity entity = world.entity().set(Transform{.position = {3.0f, 0.0f, 0.0f}});

    // Sans facteur d'interpolation dans le monde, la requête du système ne correspondrait à aucune
    // table et le rendu se tairait : le module le pose à l'import.
    CHECK(world.has<levain::scene::RenderAlpha>());
    world.progress(1.0f / 60.0f);
    CHECK(worldPosition(entity.get<WorldTransform>()).x == doctest::Approx(3.0f));
}
