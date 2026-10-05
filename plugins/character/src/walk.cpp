#include "levain/character/walk.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "levain/physics/physics.hpp"
#include "levain/scene/scene.hpp"

namespace levain::character
{

namespace
{

/// La direction demandée, ramenée à une longueur de 1 au plus : une manette en diagonale rend
/// (1, 1), qui ferait courir 41 % plus vite en biais.
glm::vec2 clampToUnit(const glm::vec2& direction)
{
    const float length = glm::length(direction);
    return length > 1.0f ? direction / length : direction;
}

/// Rapproche `current` de `target` de `step` au plus, en ligne droite.
glm::vec2 approach(const glm::vec2& current, const glm::vec2& target, float step)
{
    const glm::vec2 gap = target - current;
    const float distance = glm::length(gap);
    return distance <= step ? target : current + gap * (step / distance);
}

/// Il s'éloigne du sol : plus de 10 cm/s vers le haut par rapport à lui, la marge de l'exemple de
/// Jolt (`CharacterVirtualTest::HandleInput`). Juste après un saut, le sol est encore sous lui.
bool leavingGround(const glm::vec3& previous, const glm::vec3& ground)
{
    return previous.y - ground.y >= 0.1f;
}

/// La vitesse verticale qu'il garde en l'air : celle qu'il demandait, sauf s'il montait et que le
/// moteur l'a arrêté (un plafond, une arche). Sans ce piège, il reste collé sous le plafond tant
/// que la gravité n'a pas épuisé son élan : 0,47 s mesurées sous un plafond à 2 m.
float bumpedHead(float wanted, float effective)
{
    return wanted > 0.0f ? std::min(wanted, std::max(effective, 0.0f)) : wanted;
}

/// L'angle autour de Y qui fait regarder l'avant (−z) dans `direction`, sur le plan horizontal.
float yawOf(const glm::vec2& direction)
{
    return std::atan2(-direction.x, -direction.y);
}

/// L'angle autour de Y d'une rotation : celui de son avant, projeté sur le plan horizontal.
float yawOf(const glm::quat& rotation)
{
    const glm::vec3 forward = rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    return yawOf(glm::vec2{forward.x, forward.z});
}

} // namespace

glm::vec3 walkVelocity(const Walker& walker, const WalkInput& input,
                       const physics::CharacterState& state, const glm::vec3& previous,
                       float seconds)
{
    const glm::vec3 ground = state.ground.velocity;
    // Au sol, et pas en train de s'en éloigner : sans ce second test, il pourrait sauter encore au
    // pas qui suit un saut, le sol étant encore sous lui (l'exemple de Jolt fait le même).
    const bool grounded = physics::isWalking(state.ground) && !leavingGround(previous, ground);

    float vertical = grounded ? ground.y : bumpedHead(previous.y, state.velocity.y);
    if (grounded && input.jump)
    {
        vertical += walker.jumpSpeed;
    }
    vertical -= walker.gravity * seconds;

    // Par rapport au sol : sur une plateforme, « immobile » veut dire « à la vitesse de la
    // plateforme ».
    const glm::vec2 carried = grounded ? glm::vec2{ground.x, ground.z} : glm::vec2{0.0f};
    const glm::vec2 relative = glm::vec2{previous.x, previous.z} - carried;
    const glm::vec2 direction = clampToUnit(input.direction);
    // En l'air sans rien demander, il garde son élan : pas de freinage, comme le
    // `BrakingDecelerationFalling` nul d'Unreal. Sans ça, il perdrait en un demi-saut la vitesse
    // d'une plateforme dont il vient de sauter.
    if (!grounded && glm::length(direction) < 1e-3f)
    {
        return {previous.x, vertical, previous.z};
    }
    const float speed = input.run ? walker.runSpeed : walker.walkSpeed;
    const float accelerate = walker.acceleration * (grounded ? 1.0f : walker.airControl) * seconds;
    const glm::vec2 horizontal = approach(relative, direction * speed, accelerate) + carried;
    return {horizontal.x, vertical, horizontal.y};
}

glm::quat turnTowards(const Walker& walker, const glm::quat& rotation, const glm::vec2& direction,
                      float seconds)
{
    if (glm::length(direction) < 1e-3f)
    {
        return rotation;
    }
    const float current = yawOf(rotation);
    // L'écart ramené entre −π et π : le plus court chemin.
    const float gap = std::remainder(yawOf(direction) - current, glm::two_pi<float>());
    const float step = glm::radians(walker.turnDegreesPerSecond) * seconds;
    const float turned = current + std::clamp(gap, -step, step);
    return glm::angleAxis(turned, glm::vec3{0.0f, 1.0f, 0.0f});
}

animation::CharacterMotion motionOf(const physics::CharacterState& state)
{
    const glm::vec3 relative = state.velocity - state.ground.velocity;
    return {.speed = glm::length(glm::vec2{relative.x, relative.z}),
            .grounded = physics::isWalking(state.ground),
            .verticalSpeed = state.velocity.y,
            .swimming = false,
            .gliding = false};
}

void stepWalk(const Walker& walker, WalkInput& input, const physics::CharacterState& state,
              physics::CharacterVelocity& velocity, scene::Transform& transform, float seconds)
{
    velocity.value = walkVelocity(walker, input, state, velocity.value, seconds);
    // Par référence, sans `set` : un `set<Transform>` téléporterait le personnage (ADR-0028).
    transform.rotation = turnTowards(walker, transform.rotation, input.direction, seconds);
    input.jump = false;
}

WalkModule::WalkModule(flecs::world& world)
{
    world.module<WalkModule>();
    world.import<physics::PhysicsModule>();

    // La glu : une instruction par système (ADR-0011). La marche dans la phase Simulation, avant le
    // pas de physique qui la jouera ; elle lit l'état du pas précédent.
    world
        .system<WalkInput, const Walker, const physics::CharacterState, physics::CharacterVelocity,
                scene::Transform>("Walk")
        .kind<scene::Simulation>()
        .each([](flecs::iter& it, std::size_t, WalkInput& input, const Walker& walker,
                 const physics::CharacterState& state, physics::CharacterVelocity& velocity,
                 scene::Transform& transform)
              { stepWalk(walker, input, state, velocity, transform, it.delta_time()); });
    world.system<animation::CharacterMotion, const physics::CharacterState>("WalkMotion")
        .kind<scene::Simulation>()
        .each([](animation::CharacterMotion& motion, const physics::CharacterState& state)
              { motion = motionOf(state); });
}

} // namespace levain::character
