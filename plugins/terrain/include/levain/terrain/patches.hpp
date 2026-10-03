#pragma once

// Le terrain se dessine par parcelles carrées, de `PatchQuads` mètres de côté au niveau de détail
// 0. Chaque niveau au-delà divise par deux le nombre de sommets d'une parcelle (un sommet tous les
// 2, 4, 8 m) : loin de la caméra, une parcelle n'occupe que quelques pixels, et ses sommets ne
// servent à rien. Les fonctions de ce fichier choisissent le niveau de chaque parcelle, et lui
// donnent sa boîte pour le frustum culling.

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "levain/render/culling.hpp"
#include "levain/terrain/heightmap.hpp"

namespace levain::terrain
{

/// Le côté d'une parcelle, en intervalles entre échantillons.
inline constexpr std::uint32_t PatchQuads = 32;
/// Le niveau le plus grossier : 32 / 2⁴ = 2 intervalles par côté, 9 sommets par parcelle.
inline constexpr std::uint32_t MaxLod = 4;

struct Patch
{
    glm::uvec2 cell{0}; ///< Sa place dans la grille des parcelles.
    std::uint32_t lod = 0;
};

/// Le niveau de détail à `distance` mètres de la caméra : 0 jusqu'à `lod0Distance`, puis un niveau
/// de plus à chaque doublement de la distance, jusqu'à `MaxLod`.
[[nodiscard]] std::uint32_t lodOf(float distance, float lod0Distance);

/// Le nombre de parcelles sur un côté du terrain.
[[nodiscard]] std::uint32_t patchesPerSide(const Heightmap& heightmap);

/// Toutes les parcelles du terrain, chacune au niveau de sa distance à `camera`.
[[nodiscard]] std::vector<Patch> patchesFor(const Heightmap& heightmap, glm::vec3 camera,
                                            float lod0Distance);

/// La boîte d'une parcelle, entre la plus basse et la plus haute de ses hauteurs.
[[nodiscard]] render::Box patchBoundsOf(const Heightmap& heightmap, glm::uvec2 cell);

} // namespace levain::terrain
