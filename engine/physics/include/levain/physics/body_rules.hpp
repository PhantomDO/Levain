#pragma once

#include <cmath>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

#include "levain/physics/components.hpp"

namespace levain::physics
{

/// Pourquoi une forme ou une masse ne peut pas donner de corps, ou rien. Jolt les accepterait sans
/// un mot : le port vcpkg le compile **sans ses assertions**, même en Debug, et une masse nulle
/// devient une masse inverse infinie, puis des NaN qui se propagent à tout ce qui touche le corps
/// (règle n°7).
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

} // namespace levain::physics
