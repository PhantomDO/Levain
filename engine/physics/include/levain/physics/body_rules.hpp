#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

/// Pourquoi un maillage ne peut pas donner de corps : absent, vide, des indices qui sortent du
/// maillage, ou un sommet qui n'est pas fini. Jolt lirait hors de ses tableaux, ou compresserait un
/// NaN en triangle faux, sans un mot en Release.
inline std::optional<std::string_view> whyNotThisMesh(const MeshShape& shape)
{
    if (shape.mesh == nullptr || shape.mesh->indices.empty() || shape.mesh->indices.size() % 3 != 0)
    {
        return "un maillage physique a des triangles : trois indices par triangle, au moins un";
    }
    const auto vertexCount = static_cast<std::uint32_t>(shape.mesh->vertices.size());
    for (const std::uint32_t index : shape.mesh->indices)
    {
        if (index >= vertexCount)
        {
            return "un maillage physique désigne un sommet qui n'existe pas";
        }
    }
    for (const glm::vec3& vertex : shape.mesh->vertices)
    {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
        {
            return "un maillage physique n'a que des sommets finis";
        }
    }
    return std::nullopt;
}

/// Pourquoi une grille de hauteurs ne peut pas donner de corps, ou rien. Jolt la découpe en blocs
/// de 2 × 2 et en veut au moins deux par côté : « Sample count too low » sous 3 × 3. `FLT_MAX` est
/// sa valeur de trou (`HeightFieldShapeConstants::cNoCollisionValue`) : une hauteur démesurée
/// percerait le terrain sans un mot, alors on la refuse tant que les trous ne sont pas voulus.
inline std::optional<std::string_view> whyNotThisHeightField(const HeightFieldShape& shape)
{
    const HeightField* field = shape.field.get();
    if (field == nullptr || field->size < 3 ||
        field->heights.size() != std::size_t{field->size} * field->size ||
        !std::isfinite(field->spacing) || field->spacing <= 0.0f)
    {
        return "une grille de hauteurs a au moins 3 × 3 échantillons, size × size hauteurs et un "
               "pas positif";
    }
    for (const float height : field->heights)
    {
        if (!std::isfinite(height) || std::abs(height) >= std::numeric_limits<float>::max())
        {
            return "une grille de hauteurs n'a que des hauteurs finies, sans trou";
        }
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
    constexpr std::string_view Dimensions =
        "une forme physique a des dimensions finies et plus grandes que 0";
    const std::optional<std::string_view> shapeReason = std::visit(
        [&](const auto& shape) -> std::optional<std::string_view>
        {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, Box>)
            {
                return positive(shape.halfExtents.x) && positive(shape.halfExtents.y) &&
                               positive(shape.halfExtents.z)
                           ? std::nullopt
                           : std::optional{Dimensions};
            }
            else if constexpr (std::is_same_v<T, Sphere>)
            {
                return positive(shape.radius) ? std::nullopt : std::optional{Dimensions};
            }
            else if constexpr (std::is_same_v<T, Capsule>)
            {
                // Une capsule de demi-hauteur nulle serait une sphère : Jolt exige plus que 0.
                return positive(shape.halfHeight) && positive(shape.radius)
                           ? std::nullopt
                           : std::optional{Dimensions};
            }
            else if constexpr (std::is_same_v<T, MeshShape>)
            {
                return whyNotThisMesh(shape);
            }
            else
            {
                return whyNotThisHeightField(shape);
            }
        },
        collider.shape);
    if (shapeReason.has_value())
    {
        return shapeReason;
    }
    // Là où Jolt les ignorerait en silence (ADR-0027) : il ne calcule ni la masse d'un maillage, ni
    // sa collision contre un autre maillage ou une grille ; une grille ne bouge pas ; un maillage
    // ou une grille n'a ni intérieur ni extérieur, et un capteur de cette forme ne verrait rien de
    // ce qu'il contient.
    const bool mesh = std::holds_alternative<MeshShape>(collider.shape);
    const bool field = std::holds_alternative<HeightFieldShape>(collider.shape);
    if (mesh && body != nullptr && body->motion == Motion::Dynamic)
    {
        return "un maillage ne peut pas être dynamique : Jolt ne sait pas calculer sa masse";
    }
    if (field && body != nullptr)
    {
        return "une grille de hauteurs est du décor : elle n'a pas de RigidBody";
    }
    if ((mesh || field) && collider.layer == Layer::Sensor)
    {
        return "un maillage ou une grille ne peut pas être un volume déclencheur : il n'a pas "
               "d'intérieur";
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
