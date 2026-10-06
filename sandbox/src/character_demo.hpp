#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "levain/animation/animator.hpp"
#include "levain/character/walk.hpp"
#include "levain/physics/character.hpp"
#include "levain/physics/components.hpp"
#include "levain/scene/components.hpp"

/// La scène de `levain_sandbox --view character` (M6.3, ADR-0028) : le renard dans Sponza, un
/// escalier, deux rampes et des caisses à pousser. Les hauteurs viennent d'une carte de Sponza
/// relevée par rayons, sur sa collision simplifiée.
namespace levain::sandbox
{

/// Le sol des galeries et de l'atrium de Sponza, et celui de la tranchée large d'un mètre qui longe
/// chaque mur extérieur, 0,9 m plus bas : le rebord que l'escalier permet de remonter. Sponza n'a
/// pas d'escalier à lui.
inline constexpr float GalleryFloor = -0.02f;
inline constexpr float TrenchFloor = -0.92f;
/// Le milieu de la tranchée sud, entre le rebord (z = −5,2) et le mur (z = −6,26).
inline constexpr float TrenchCenterZ = -5.73f;

/// L'escalier : six marches de 15 cm, de 30 cm de profondeur, de la tranchée jusqu'au niveau des
/// galeries, puis un palier. Chaque marche est un cube d'un mètre, le seul solide que le sandbox
/// dessine, enfoncé dans le sol de la tranchée : seuls ses 15 cm de plus que le précédent se
/// voient.
inline constexpr int StairSteps = 6;
inline constexpr float StairRise = 0.15f;
inline constexpr float StairTread = 0.3f;
inline constexpr float StairStartX = -2.5f; ///< La première contremarche.
inline constexpr int LandingCubes = 2;      ///< Après la dernière marche, à sa hauteur.

/// La marche du renard : les réglages par défaut du plugin. Le joueur les porte, et l'animation
/// y règle ses vitesses de marche et de course.
inline constexpr character::Walker FoxWalker{};

/// Où le renard commence : au fond de la tranchée, 1,5 m avant la première marche, face à elle.
inline constexpr glm::vec3 PlayerStart{StairStartX - 1.5f, TrenchFloor, TrenchCenterZ};

/// La capsule du renard : 0,8 m de haut, pour un renard de 0,79 m à l'échelle 0,01 (Fox mesure
/// 155 × 79 unités). Elle ne couvre ni son museau ni sa queue : c'est le compromis habituel d'un
/// quadrupède sur une capsule debout.
inline constexpr physics::CharacterController FoxController{
    .shape = {.halfHeight = 0.1f, .radius = 0.3f}};

/// Un cube de décor d'un mètre, statique, avec les étiquettes `Tags` (celle des cubes dessinés).
template <typename... Tags>
flecs::entity spawnBlock(flecs::world& world, const std::string& name, const glm::vec3& center,
                         const glm::quat& rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f})
{
    flecs::entity block = world.entity(name.c_str())
                              .set(scene::Transform{.position = center, .rotation = rotation})
                              .set(physics::Collider{.shape = physics::Box{}});
    (block.add<Tags>(), ...);
    return block;
}

/// Le bord gauche (−x) du cube n° `step`, compté de 1 : sa contremarche pour une marche, tous les
/// 30 cm ; les cubes du palier, eux, se suivent d'un mètre après la dernière marche.
inline float stairLeftEdge(int step)
{
    const int stair = std::min(step, StairSteps);
    const int landing = std::max(step - StairSteps, 0);
    return StairStartX + StairTread * static_cast<float>(stair - 1) + static_cast<float>(landing);
}

/// L'escalier dans la tranchée, le long du mur, et son palier.
template <typename... Tags> void spawnStairs(flecs::world& world)
{
    for (int step = 1; step <= StairSteps + LandingCubes; ++step)
    {
        const int level = std::min(step, StairSteps);
        const float left = stairLeftEdge(step);
        const float top = TrenchFloor + StairRise * static_cast<float>(level);
        spawnBlock<Tags...>(world, std::format("stair_{}", step),
                            {left + 0.5f, top - 0.5f, TrenchCenterZ});
    }
}

/// Une rampe de trois cubes inclinés de `degrees`, qui monte vers +x depuis `foot`, sur le sol
/// des galeries : l'un sur l'autre le long de la pente, enfoncés pour que leur dessus parte du sol.
template <typename... Tags>
void spawnRamp(flecs::world& world, const std::string& name, const glm::vec3& foot, float degrees)
{
    const float angle = glm::radians(degrees);
    const glm::quat rotation = glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f});
    const glm::vec3 along = rotation * glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 up = rotation * glm::vec3{0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 3; ++i)
    {
        // Le coin haut de l'arrière du premier cube sur `foot`, puis un mètre de plus par cube.
        const glm::vec3 center = foot + along * (0.5f + static_cast<float>(i)) - up * 0.5f;
        spawnBlock<Tags...>(world, std::format("{}_{}", name, i), center, rotation);
    }
}

/// Les caisses à pousser, dans l'atrium : trois de 10 kg, qui suivent le renard, et une de 50 kg,
/// au-delà de ce que sa force de poussée déplace (ADR-0028).
template <typename... Tags> void spawnPushables(flecs::world& world)
{
    constexpr std::array<float, 4> Masses{10.0f, 10.0f, 10.0f, 50.0f};
    for (std::size_t i = 0; i < Masses.size(); ++i)
    {
        flecs::entity crate =
            world.entity(std::format("pushable_{}", i).c_str())
                .set(scene::Transform{
                    .position = {-2.0f + 1.5f * static_cast<float>(i), GalleryFloor + 0.5f, 0.0f}})
                .set(physics::Collider{.shape = physics::Box{}})
                .set(physics::RigidBody{.mass = Masses[i]});
        (crate.add<Tags>(), ...);
    }
}

/// Tout le décor ajouté de la démo : l'escalier, une rampe de 30° qu'il monte et une de 55° qu'il
/// ne gravit pas, dans la galerie sud, et les caisses.
template <typename... Tags> void spawnCharacterDemo(flecs::world& world)
{
    spawnStairs<Tags...>(world);
    spawnRamp<Tags...>(world, "ramp_30", {2.0f, GalleryFloor, -3.5f}, 30.0f);
    spawnRamp<Tags...>(world, "ramp_55", {6.0f, GalleryFloor, -3.5f}, 55.0f);
    spawnPushables<Tags...>(world);
}

/// Le joueur : une racine sans échelle, le personnage (ADR-0028). Le modèle du renard en sera un
/// enfant, avec son échelle. Il regarde vers +x, comme la caméra qui le suit : de dos, pas de
/// profil, avant le premier pas.
inline flecs::entity spawnPlayer(flecs::world& world, const glm::vec3& feet)
{
    return world.entity("player")
        .set(scene::Transform{
            .position = feet,
            .rotation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f})})
        .set(FoxController)
        .set(FoxWalker)
        .set(character::WalkInput{})
        .set(animation::CharacterMotion{});
}

/// La caméra qui suit le joueur à distance fixe, sans collision (la vraie est M6.4) : 3,5 m en
/// arrière dans l'axe de la tranchée (−x), 1,6 m au-dessus, le regard sur son dos. Dans l'axe +z,
/// elle tombait derrière les rideaux de la galerie.
inline scene::Transform followCamera(const glm::vec3& player)
{
    const glm::vec3 eye = player + glm::vec3{-3.5f, 1.6f, 0.0f};
    const glm::vec3 target = player + glm::vec3{0.0f, 0.5f, 0.0f};
    return {.position = eye,
            .rotation = glm::quatLookAt(glm::normalize(target - eye), glm::vec3{0.0f, 1.0f, 0.0f})};
}

/// La direction de marche dans le monde, depuis les axes du clavier ou de la manette : la caméra
/// regarde vers +x, « avant » est donc +x, et « droite » +z.
inline glm::vec2 walkDirectionOf(float right, float forward)
{
    return {forward, right};
}

} // namespace levain::sandbox
