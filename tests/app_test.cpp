#include <limits>
#include <string>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/matrix_transform.hpp>

#include "levain/app/camera.hpp"
#include "levain/app/player_input.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"

TEST_CASE("la caméra du rendu regarde vers −Z de sa matrice monde, avec son objectif")
{
    const glm::mat4 world = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}) *
                            glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), {0.0f, 1.0f, 0.0f});
    const levain::render::Camera camera = levain::app::cameraFrom(
        {.verticalFovDegrees = 45.0f, .nearPlane = 0.2f, .farPlane = 500.0f}, world);
    CHECK(camera.position.x == doctest::Approx(1.0f));
    CHECK(camera.position.z == doctest::Approx(3.0f));
    // Un quart de tour à gauche : −Z devient −X.
    CHECK(camera.target.x == doctest::Approx(0.0f));
    CHECK(camera.target.z == doctest::Approx(3.0f));
    CHECK(camera.verticalFovRadians == doctest::Approx(glm::radians(45.0f)));
    CHECK(camera.nearPlane == 0.2f);
}

TEST_CASE("un plan lointain qui ne passe pas le proche est ramené au-delà, jamais projeté tel quel")
{
    using levain::app::farBeyondNear;
    CHECK(farBeyondNear(0.5f, 1000.0f) == 1000.0f);
    // Égal, inférieur, nul, négatif ou NaN : l'inspecteur laisse tout taper.
    CHECK(farBeyondNear(0.5f, 0.5f) == 1.0f);
    CHECK(farBeyondNear(0.5f, 0.1f) == 1.0f);
    CHECK(farBeyondNear(0.5f, 0.0f) == 1.0f);
    CHECK(farBeyondNear(0.5f, -3.0f) == 1.0f);
    CHECK(farBeyondNear(0.5f, std::numeric_limits<float>::quiet_NaN()) == 1.0f);

    // La caméra du rendu l'applique : sa projection n'est pas dégénérée à far = near.
    const levain::render::Camera camera = levain::app::cameraFrom(
        {.verticalFovDegrees = 60.0f, .nearPlane = 0.5f, .farPlane = 0.5f}, glm::mat4{1.0f});
    CHECK(camera.farPlane == 1.0f);
}

TEST_CASE("le rendu exige une seule caméra, et nomme celles qu'il trouve en trop")
{
    flecs::world world;
    const auto cameras =
        world.query<const levain::app::CameraLens, const levain::scene::WorldTransform>();
    CHECK_FALSE(levain::app::renderCameraOf(cameras).has_value());
    world.entity("vue").set(levain::app::CameraLens{}).set(levain::scene::WorldTransform{});
    CHECK(levain::app::renderCameraOf(cameras).has_value());
    world.entity("autre").set(levain::app::CameraLens{}).set(levain::scene::WorldTransform{});
    const auto two = levain::app::renderCameraOf(cameras);
    REQUIRE_FALSE(two.has_value());
    CHECK(two.error().message.find("vue") != std::string::npos);
    CHECK(two.error().message.find("autre") != std::string::npos);
}

TEST_CASE("une caméra imposée se passe des entités, et sans elle le refus reste")
{
    flecs::world world;
    const auto cameras =
        world.query<const levain::app::CameraLens, const levain::scene::WorldTransform>();
    const levain::render::Camera imposed{.position = {1.0f, 2.0f, 3.0f},
                                         .target = {0.0f, 0.0f, 0.0f},
                                         .verticalFovRadians = 1.0f,
                                         .nearPlane = 0.25f,
                                         .farPlane = 400.0f};

    // Zéro CameraLens : la caméra imposée est celle du rendu, sans consulter les entités.
    const auto alone = levain::app::renderCameraOr(imposed, cameras);
    REQUIRE(alone.has_value());
    CHECK(alone->position == imposed.position);
    CHECK(alone->farPlane == 400.0f);
    // Plusieurs CameraLens : elle ne s'arrête pas non plus, ce que `renderCameraOf` refuse.
    world.entity("vue").set(levain::app::CameraLens{}).set(levain::scene::WorldTransform{});
    world.entity("autre").set(levain::app::CameraLens{}).set(levain::scene::WorldTransform{});
    CHECK_FALSE(levain::app::renderCameraOf(cameras).has_value());
    const auto among = levain::app::renderCameraOr(imposed, cameras);
    REQUIRE(among.has_value());
    CHECK(among->position == imposed.position);

    // Un plan lointain imposé qui ne passe pas le proche est ramené au-delà, comme celui d'un
    // objectif.
    auto degenerate = imposed;
    degenerate.farPlane = degenerate.nearPlane;
    CHECK(levain::app::renderCameraOr(degenerate, cameras)->farPlane == 0.5f);

    // Sans caméra imposée, rien ne change : plusieurs CameraLens (ici), comme zéro, sont refusés.
    const auto refused = levain::app::renderCameraOr(std::nullopt, cameras);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("vue") != std::string::npos);
    flecs::world empty;
    const auto none =
        empty.query<const levain::app::CameraLens, const levain::scene::WorldTransform>();
    CHECK_FALSE(levain::app::renderCameraOr(std::nullopt, none).has_value());
}

TEST_CASE("un appui est vu par un seul pas, même quand l'image en joue deux")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    world.set<levain::app::PlayerInput>({});
    levain::app::forgetPressesAtEachStep(world);
    // Un système du jeu, au pas fixe, qui compte les pas où il voit l'appui.
    int seen = 0;
    world.system("CountPresses")
        .kind<levain::scene::Simulation>()
        .run(
            [&seen](flecs::iter& it)
            {
                if (levain::app::pressedSinceLastStep(it.world().get<levain::app::PlayerInput>(),
                                                      0))
                {
                    ++seen;
                }
            });
    levain::scene::FixedStep step;
    levain::input::InputState state;
    state.actionsHeld = {false};
    state.actionsHeldPreviously = {false};

    // L'image de l'appui ne joue aucun pas : l'appui attend.
    state.actionsHeld = {true};
    levain::app::takeFrameInput(world.get_mut<levain::app::PlayerInput>(), state);
    CHECK(levain::scene::advanceWorld(world, step, 0.0f) == 0);
    CHECK(seen == 0);
    // L'image suivante, l'action est tenue sans être « appuyée », et l'image joue deux pas : le
    // premier voit l'appui, le second non.
    state.actionsHeldPreviously = {true};
    levain::app::takeFrameInput(world.get_mut<levain::app::PlayerInput>(), state);
    CHECK(levain::scene::advanceWorld(world, step, 2.0f * step.stepSeconds) == 2);
    CHECK(seen == 1);
    CHECK_FALSE(levain::app::pressedSinceLastStep(world.get<levain::app::PlayerInput>(), 7));
}
