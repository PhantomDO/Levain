#include <string>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/matrix_transform.hpp>

#include "levain/app/camera.hpp"
#include "levain/app/player_input.hpp"
#include "levain/scene/components.hpp"

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

TEST_CASE("le rendu exige une seule caméra, et nomme celles qu'il trouve en trop")
{
    flecs::world world;
    CHECK_FALSE(levain::app::renderCameraOf(world).has_value());
    world.entity("vue").set(levain::app::CameraLens{}).set(levain::scene::WorldTransform{});
    CHECK(levain::app::renderCameraOf(world).has_value());
    world.entity("autre").set(levain::app::CameraLens{}).set(levain::scene::WorldTransform{});
    const auto two = levain::app::renderCameraOf(world);
    REQUIRE_FALSE(two.has_value());
    CHECK(two.error().message.find("vue") != std::string::npos);
    CHECK(two.error().message.find("autre") != std::string::npos);
}

TEST_CASE("un appui reste vu jusqu'au prochain pas de simulation, et une fois seulement")
{
    levain::app::PlayerInput input;
    levain::input::InputState state;
    state.actionsHeld = {false};
    state.actionsHeldPreviously = {false};
    levain::app::takeFrameInput(input, state);
    CHECK_FALSE(levain::app::pressedSinceLastStep(input, 0));
    // L'image de l'appui ne joue aucun pas : l'appui attend.
    state.actionsHeld = {true};
    levain::app::takeFrameInput(input, state);
    levain::app::forgetPressesAfterSteps(input, 0);
    // L'image suivante, l'action est toujours tenue mais n'est plus « appuyée » : l'appui reste.
    state.actionsHeldPreviously = {true};
    levain::app::takeFrameInput(input, state);
    CHECK(levain::app::pressedSinceLastStep(input, 0));
    // Cette image joue deux pas : le premier voit l'appui, puis il est oublié.
    levain::app::forgetPressesAfterSteps(input, 2);
    CHECK_FALSE(levain::app::pressedSinceLastStep(input, 0));
    CHECK_FALSE(levain::app::pressedSinceLastStep(input, 7)); // une action inconnue : non
}
