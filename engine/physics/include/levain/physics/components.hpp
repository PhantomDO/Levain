#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include <glm/glm.hpp>

#include "levain/physics/layers.hpp"

namespace levain::physics
{

/// Une boîte, donnée par ses **demi**-dimensions : `{0.5, 0.5, 0.5}` est un cube d'un mètre, comme
/// la `Box` de Jolt et l'`extents` d'Unity (Unity prend aussi la `size`, deux fois plus grande).
struct Box
{
    glm::vec3 halfExtents{0.5f};
};

struct Sphere
{
    float radius = 0.5f;
};

/// Une capsule debout, selon l'axe Y : un cylindre de `2 × halfHeight` coiffé de deux
/// demi-sphères. Sa hauteur totale est donc `2 × (halfHeight + radius)` : c'est le piège, Unity
/// donnant au contraire la hauteur totale.
struct Capsule
{
    float halfHeight = 0.5f;
    float radius = 0.5f;
};

using Shape = std::variant<Box, Sphere, Capsule>;

/// La forme d'une entité pour la physique. **Seul, il en fait un corps statique** : le décor ne
/// déclare que sa forme (ADR-0026, comme le collider sans `Rigidbody` d'Unity). Un `RigidBody` en
/// plus le rend mobile.
///
/// La taille est dans la forme, jamais dans le `Transform` : un corps est refusé si son échelle
/// n'est pas 1 (ADR-0026).
struct Collider
{
    Shape shape = Box{};
    /// Vide, la couche suit le mouvement : `Static` sans `RigidBody`, `Dynamic` avec
    /// (`effectiveLayer`). `Sensor` en fait un volume déclencheur, `Debris` un objet que le
    /// personnage traverse.
    std::optional<Layer> layer{};
};

enum class Motion : std::uint8_t
{
    Dynamic,   ///< La simulation le déplace : gravité, chocs. Jolt fait autorité sur sa position.
    Kinematic, ///< Le jeu le déplace, par son `Transform` ; il pousse sans être poussé.
};

/// Le frottement d'un corps qui n'en dit pas plus, décor compris. Le défaut de Jolt, 0,2, fait
/// glisser une caisse posée sur une pente douce ; celui d'Unity est 0,6. Deux frottements se
/// combinent par leur moyenne géométrique : un sol laissé à 0,2 sous une caisse à 0,5 donnerait
/// 0,32.
inline constexpr float DefaultFriction = 0.5f;

/// Ce qui rend un `Collider` mobile.
struct RigidBody
{
    Motion motion = Motion::Dynamic;
    float mass = 1.0f;                ///< En kilogrammes, plus que 0. Ignorée pour un cinématique.
    float friction = DefaultFriction; ///< Entre 0 (la glace) et 1.
    float restitution = 0.0f;         ///< Le rebond : 0 n'en a aucun, 1 rend toute l'énergie.
};

/// L'identifiant du corps Jolt de l'entité, **posé par le module**, jamais à la main. Opaque :
/// sa valeur ne sert qu'à `physics_world.hpp`. 0 désigne un vrai corps chez Jolt : l'absence de
/// corps est la valeur maximale, celle de son `BodyID` invalide.
struct BodyHandle
{
    static constexpr std::uint32_t None = 0xffffffffu;
    std::uint32_t value = None;
};

/// La couche d'un corps : celle que son `Collider` demande, sinon celle de son mouvement. Le piège
/// qu'elle évite : une couche `Static` par défaut, qu'un corps dynamique garderait sans que
/// personne ne la change, et qui traverserait alors le décor.
constexpr Layer effectiveLayer(const Collider& collider, const RigidBody* body)
{
    if (collider.layer.has_value())
    {
        return *collider.layer;
    }
    return body != nullptr ? Layer::Dynamic : Layer::Static;
}

} // namespace levain::physics
