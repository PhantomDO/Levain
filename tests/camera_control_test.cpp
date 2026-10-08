#include <cmath>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/constants.hpp>

#include "levain/input/bindings.hpp"
#include "levain/input/state.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"

using levain::scene::clampPitch;
using levain::scene::FpsController;
using levain::scene::FpsInput;
using levain::scene::normalizeOrZero;
using levain::scene::Transform;

namespace
{

/// Une seconde de simulation, en un pas, pour que les vitesses se lisent directement.
constexpr float OneSecond = 1.0f;

Transform movedBy(const FpsInput& input, FpsController controller = {}, float seconds = OneSecond)
{
    Transform transform;
    levain::scene::applyFpsInput(transform, controller, input, seconds);
    return transform;
}

} // namespace

TEST_CASE("normalizeOrZero garde la diagonale à la même vitesse, et supporte le vecteur nul")
{
    CHECK(normalizeOrZero({0.0f, 0.0f, 0.0f}) == glm::vec3{0.0f}); // pas de division par zéro
    CHECK(glm::length(normalizeOrZero({1.0f, 0.0f, 1.0f})) == doctest::Approx(1.0f));
}

TEST_CASE("clampPitch empêche de passer par-dessus la tête")
{
    CHECK(clampPitch(120.0f, -85.0f, 85.0f) == doctest::Approx(85.0f));
    CHECK(clampPitch(-120.0f, -85.0f, 85.0f) == doctest::Approx(-85.0f));
    CHECK(clampPitch(30.0f, -85.0f, 85.0f) == doctest::Approx(30.0f));
}

TEST_CASE("clampPitch remet des bornes inversées dans l'ordre")
{
    // L'inspecteur règle `minPitchDegrees` et `maxPitchDegrees` l'un sans l'autre : 50 puis -50.
    CHECK(clampPitch(10.0f, 50.0f, -50.0f) == doctest::Approx(10.0f));
    CHECK(clampPitch(80.0f, 50.0f, -50.0f) == doctest::Approx(50.0f));
    CHECK(clampPitch(-80.0f, 50.0f, -50.0f) == doctest::Approx(-50.0f));
}

TEST_CASE("shortestYawDelta tourne par le plus court chemin")
{
    using levain::scene::shortestYawDelta;
    // De 350° à 10° : 20° dans le sens positif, pas −340°.
    CHECK(glm::degrees(shortestYawDelta(glm::radians(350.0f), glm::radians(10.0f))) ==
          doctest::Approx(20.0f));
    CHECK(glm::degrees(shortestYawDelta(glm::radians(10.0f), glm::radians(350.0f))) ==
          doctest::Approx(-20.0f));
    CHECK(glm::degrees(shortestYawDelta(0.0f, glm::radians(90.0f))) == doctest::Approx(90.0f));
}

TEST_CASE("la caméra avance dans la direction où elle regarde")
{
    // Lacet nul : l'avant est -Z. Une seconde à 12 unités par seconde.
    const Transform forward = movedBy({.move = {0.0f, 1.0f}});
    CHECK(forward.position.z == doctest::Approx(-12.0f));
    CHECK(forward.position.x == doctest::Approx(0.0f));

    // Un quart de tour à gauche : l'avant devient -X.
    FpsController turned;
    turned.yawDegrees = 90.0f;
    const Transform left = movedBy({.move = {0.0f, 1.0f}}, turned);
    CHECK(left.position.x == doctest::Approx(-12.0f));
    CHECK(left.position.z == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("la diagonale n'avance pas plus vite que la ligne droite")
{
    const Transform straight = movedBy({.move = {0.0f, 1.0f}});
    const Transform diagonal = movedBy({.move = {1.0f, 1.0f}});

    CHECK(glm::length(diagonal.position) == doctest::Approx(glm::length(straight.position)));
}

TEST_CASE("le tangage ne fait pas décoller la caméra, mais oriente le regard")
{
    FpsController looking;
    looking.pitchDegrees = 45.0f;
    const Transform moved = movedBy({.move = {0.0f, 1.0f}}, looking);

    CHECK(moved.position.y == doctest::Approx(0.0f)); // on avance à plat
    CHECK(levain::scene::forwardOf(looking).y == doctest::Approx(std::sqrt(2.0f) / 2.0f));
}

TEST_CASE("sprint multiplie la vitesse, et le regard tourne à la vitesse demandée")
{
    const Transform walking = movedBy({.move = {0.0f, 1.0f}});
    const Transform running = movedBy({.move = {0.0f, 1.0f}, .sprint = true});
    CHECK(glm::length(running.position) == doctest::Approx(4.0f * glm::length(walking.position)));

    FpsController controller;
    Transform transform;
    // 90 unités de regard pendant une seconde, à 1 degré par unité : un quart de tour à droite.
    levain::scene::applyFpsInput(transform, controller, {.look = {90.0f, 0.0f}}, OneSecond);
    CHECK(controller.yawDegrees == doctest::Approx(-90.0f)); // le lacet croît vers la gauche
}

TEST_CASE("le système de caméra tourne dans le pipeline de simulation, à pas fixe")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity camera = world.entity("camera").set(Transform{}).set(FpsController{});
    levain::scene::FixedStep step;

    // L'application pose les intentions du joueur ; la caméra les suit au pas suivant.
    world.set<FpsInput>({.move = {0.0f, 1.0f}});
    levain::scene::advanceWorld(world, step, 1.0f / 60.0f);

    // Un pas de 1/60 s à 12 unités par seconde : 0,2 unité.
    CHECK(camera.get<Transform>().position.z == doctest::Approx(-0.2f));

    // Et rien ne bouge quand le joueur relâche tout, même si les images continuent.
    world.set<FpsInput>({});
    levain::scene::advanceWorld(world, step, 1.0f / 60.0f);
    CHECK(camera.get<Transform>().position.z == doctest::Approx(-0.2f));
}

TEST_CASE("la même caméra se pilote au clavier et à la manette, sans rien changer d'autre")
{
    // Le critère de #67, de bout en bout : des événements bruts, un fichier de liaisons, et la
    // caméra. Seule la source des événements change entre les deux moitiés du test.
    const auto bindings = levain::input::parseBindings(
        "axis move_forward = key:W, pad:lefty:-1\naxis look_right = pad:rightx:90\n");
    REQUIRE(bindings.has_value());
    const int moveForward = levain::input::axisIndex(bindings.value(), "move_forward").value_or(-1);

    const auto cameraAfter = [&bindings, moveForward](const levain::platform::InputEvent& event)
    {
        flecs::world world;
        world.import<levain::scene::SceneModule>();
        const flecs::entity camera = world.entity().set(Transform{}).set(FpsController{});
        levain::input::InputState input = levain::input::makeInputState(bindings.value());
        levain::scene::FixedStep step;

        levain::input::updateInput(input, bindings.value(), {&event, 1}, 1.0f / 60.0f);
        world.set<FpsInput>({.move = {0.0f, levain::input::axisValue(input, moveForward)}});
        levain::scene::advanceWorld(world, step, 1.0f / 60.0f);
        return camera.get<Transform>().position;
    };

    const glm::vec3 byKeyboard = cameraAfter({.type = levain::platform::InputEventType::ButtonDown,
                                              .device = levain::platform::InputDevice::Keyboard,
                                              .code = 26}); // « W »
    const glm::vec3 byGamepad = cameraAfter({.type = levain::platform::InputEventType::AxisMotion,
                                             .device = levain::platform::InputDevice::Gamepad,
                                             .code = 1, // « lefty », poussé à fond vers l'avant
                                             .value = -1.0f});

    CHECK(byKeyboard.z == doctest::Approx(-0.2f));
    CHECK(byGamepad.z == doctest::Approx(byKeyboard.z));
}
