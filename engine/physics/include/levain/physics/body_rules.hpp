#pragma once

#include <cmath>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

#include <glm/gtc/quaternion.hpp>

#include "levain/physics/components.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/scene/components.hpp"

namespace levain::physics
{

/// Pourquoi une entité ne peut pas porter de corps physique, ou rien si elle le peut (ADR-0026).
///
/// - **Un corps est une racine** : Jolt place ses corps dans le monde, le `Transform` d'un enfant
///   est dans le repère de son parent. Convertir à chaque pas est la source classique des objets
///   qui tremblent ; un enfant d'un corps le suit, lui, par la hiérarchie.
/// - **Un corps n'a pas d'échelle** : la taille est dans la forme. Une échelle non uniforme
///   déformerait une sphère en ellipsoïde, que Jolt ne sait pas simuler ; Godot demande la même
///   chose de ses formes.
inline std::optional<std::string_view> whyNotABody(const scene::Transform& transform,
                                                   bool hasParent)
{
    if (hasParent)
    {
        return "un corps physique doit être une entité racine, sans parent";
    }
    if (transform.scale != glm::vec3(1.0f))
    {
        return "un corps physique n'a pas d'échelle : la taille se donne dans la forme du Collider";
    }
    // Une rotation nulle ou faite de NaN : la renormaliser en ferait l'identité, ou des NaN, sans
    // un mot. Une rotation seulement un peu longue est, elle, renormalisée (`toJoltRotation`).
    const float length = glm::length(transform.rotation);
    if (!std::isfinite(length) || length < 1e-6f)
    {
        return "un corps physique a une rotation finie et non nulle";
    }
    return std::nullopt;
}

/// Pourquoi une forme ou une masse ne peut pas donner de corps, ou rien. Jolt les accepterait sans
/// un mot en Release, où ses assertions n'existent pas (`JPH_DEBUG` suit `NDEBUG`) : une masse
/// nulle y devient une masse inverse infinie, puis des NaN qui se propagent à tout ce qui touche le
/// corps (règle n°7).
inline std::optional<std::string_view> whyNotThisShape(const Collider& collider,
                                                       const RigidBody* body)
{
    const auto positive = [](float value) { return std::isfinite(value) && value > 0.0f; };
    const bool shapeOk = std::visit(
        [&positive](const auto& shape)
        {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, Box>)
            {
                return positive(shape.halfExtents.x) && positive(shape.halfExtents.y) &&
                       positive(shape.halfExtents.z);
            }
            else if constexpr (std::is_same_v<T, Sphere>)
            {
                return positive(shape.radius);
            }
            else
            {
                // Une capsule de demi-hauteur nulle serait une sphère : Jolt exige plus que 0.
                return positive(shape.halfHeight) && positive(shape.radius);
            }
        },
        collider.shape);
    if (!shapeOk)
    {
        return "une forme physique a des dimensions finies et plus grandes que 0";
    }
    if (body != nullptr && body->motion == Motion::Dynamic && !positive(body->mass))
    {
        return "un corps dynamique a une masse finie et plus grande que 0";
    }
    return std::nullopt;
}

/// Pourquoi une couche ne convient pas au corps, ou rien. Un corps mobile sur `Static` ne
/// toucherait pas le décor, que `Static` ignore : il traverserait le sol sans un bruit.
/// `effectiveLayer` évite ce piège quand la couche est déduite ; donnée explicitement, elle est
/// refusée (ADR-0026).
inline std::optional<std::string_view> whyNotThisLayer(const Collider& collider,
                                                       const RigidBody* body)
{
    if (body != nullptr && effectiveLayer(collider, body) == Layer::Static)
    {
        return "un corps mobile ne peut pas être sur la couche Static : il traverserait le décor";
    }
    return std::nullopt;
}

inline BodyPose poseOf(const scene::Transform& transform)
{
    return {.position = transform.position, .rotation = transform.rotation};
}

} // namespace levain::physics
