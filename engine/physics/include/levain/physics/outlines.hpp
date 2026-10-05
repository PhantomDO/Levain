#pragma once

// Les contours des formes de collision (M6.2, ADR-0027) : les segments qui les dessinent, pour les
// lignes de debug. Des fonctions pures, sans Jolt : la physique ne connaît pas le rendu, et
// l'application relie les deux.

#include <vector>

#include <glm/glm.hpp>

#include "levain/physics/components.hpp"
#include "levain/physics/physics_world.hpp"

namespace levain::physics
{

/// Un segment du contour d'une forme, dans le monde.
struct Segment
{
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
};

/// Les segments d'un cercle ou d'un arc complet : assez pour qu'une sphère d'un mètre paraisse
/// ronde, peu pour que mille corps restent légers.
inline constexpr int CircleSegments = 24;

/// Ajoute à `out` le contour de `collider` placé à `pose` : les 12 arêtes d'une boîte, trois grands
/// cercles pour une sphère, deux cercles, quatre génératrices et deux arcs par bout pour une
/// capsule, les arêtes de chaque triangle d'un maillage, et le seul bord d'une grille de hauteurs
/// (ses 263 000 hauteurs feraient près de 790 000 segments, arêtes et diagonales).
void appendOutline(const Collider& collider, const BodyPose& pose, std::vector<Segment>& out);

} // namespace levain::physics
