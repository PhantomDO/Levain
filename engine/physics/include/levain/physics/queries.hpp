#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>

#include <glm/glm.hpp>

#include "levain/physics/layers.hpp"
#include "levain/physics/physics_world.hpp"

namespace levain::physics
{

/// Les couches qu'une requête voit, un bit par couche, dans l'ordre de l'enum `Layer`. Le
/// `LayerMask` d'Unity, les canaux d'une trace d'Unreal.
using LayerMask = std::uint32_t;

constexpr LayerMask maskOf(std::initializer_list<Layer> layers)
{
    LayerMask mask = 0;
    for (const Layer layer : layers)
    {
        mask |= LayerMask{1} << static_cast<std::uint32_t>(layer);
    }
    return mask;
}

/// Ce qui arrête un rayon : tout, sauf les volumes déclencheurs. Une sélection à la souris ne doit
/// pas s'arrêter sur l'eau qu'on regarde à travers (ADR-0027).
inline constexpr LayerMask SolidLayers =
    maskOf({Layer::Static, Layer::Dynamic, Layer::Character, Layer::Debris});

/// Un rayon : son origine, sa direction (normalisée par la requête), et jusqu'où il va.
struct Ray
{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float maxDistance = 1000.0f;
};

/// Le premier corps touché : son entité (celle donnée à `createBody`), le point touché, la normale
/// de la surface en ce point, et la distance depuis l'origine.
struct RayHit
{
    std::uint64_t entity = 0;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
};

/// Les requêtes voient tous les corps créés, même avant le premier pas, chacun à la pose du dernier
/// pas (ou à celle de sa création, de sa téléportation).

/// Le premier corps d'une couche de `mask` que le rayon rencontre, ou rien. Seule compte une
/// surface que le rayon traverse **en entrant**, comme le `Physics.Raycast` d'Unity : un rayon
/// parti de l'intérieur d'un corps ne voit pas ce corps, et le dessous d'un terrain ou d'un
/// maillage n'arrête rien.
[[nodiscard]] std::optional<RayHit> raycast(const PhysicsWorld& world, const Ray& ray,
                                            LayerMask mask = SolidLayers);

/// Comme `raycast`, mais c'est une sphère de rayon `radius` qui avance le long du rayon : ce que
/// la caméra de M6.4 lancera pour ne pas traverser la roche. `point` est le point de contact,
/// `distance` celle que le centre a parcourue, `normal` celle du contact (sur une arête, entre les
/// deux faces). Une sphère qui touche déjà un corps au départ le touche à la distance 0 si elle
/// avance vers lui, et ne le voit pas si elle s'en éloigne : la caméra peut se dégager d'un mur.
[[nodiscard]] std::optional<RayHit> sphereCast(const PhysicsWorld& world, const Ray& ray,
                                               float radius, LayerMask mask = SolidLayers);

} // namespace levain::physics
