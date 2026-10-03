#pragma once

// L'herbe (M5.7) : des brins instanciés sur le GPU, sans buffer d'instances. Le shader tire la
// place de chaque brin de son numéro d'instance, dans la parcelle du terrain qu'il couvre, et le
// garde ou l'écarte selon la carte de densité. Ce fichier en donne les parties CPU : la carte, la
// forme d'un brin, et combien en dessiner par parcelle selon la distance.

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "levain/terrain/heightmap.hpp"

namespace levain::grass
{

struct GrassSettings
{
    float bladesPerSquareMeter = 48.0f; ///< Là où la densité vaut 1, et près de la caméra.
    float fullDensityDistance = 16.0f;  ///< Jusque-là, tous les brins ; au-delà, de moins en moins.
    float range = 64.0f; ///< Au-delà, plus d'herbe : le sol vert de la couche suffit.
};

/// La densité de l'herbe en chaque échantillon du terrain, de 0 à 255 : le poids de sa couche
/// d'herbe (`weightMapOf`), éteint sous `waterLevel` et sur les 30 cm de rivage au-dessus.
[[nodiscard]] std::vector<std::uint8_t> densityMapOf(const terrain::Heightmap& heightmap,
                                                     float waterLevel);

/// Le nombre de brins à dessiner sur une parcelle de `area` m² dont le point le plus proche est à
/// `distance` mètres de la caméra. Au-delà de `fullDensityDistance`, il décroît comme le carré de
/// la distance : autant de brins par pixel d'écran, à peu près, loin comme près. Zéro au-delà de
/// `range`.
[[nodiscard]] std::uint32_t bladeCountOf(float distance, float area, const GrassSettings& settings);

/// La forme d'un brin, de sa base (y = 0) à sa pointe (y = 1), x de −0,5 à 0,5 en travers :
/// `segments` trapèzes qui s'affinent, puis la pointe, un triangle. Le shader l'étire à sa hauteur,
/// le tourne et le courbe sous le vent.
struct BladeGeometry
{
    std::vector<glm::vec2> vertices;
    std::vector<std::uint16_t> indices;
};

[[nodiscard]] BladeGeometry bladeGeometryOf(std::uint32_t segments);

} // namespace levain::grass
