#pragma once

// Le mélange des textures du terrain (M5.6) : trois couches, l'herbe, un sol rocailleux et la
// roche, dosées en chaque point par une carte de poids. Ici, les poids viennent du relief : l'herbe
// sur le plat, la roche dans les pentes et sur les crêtes. L'éditeur les peindra à la main (M7.6).

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>

#include "levain/terrain/heightmap.hpp"

namespace levain::terrain
{

/// Les poids de l'herbe, du sol rocailleux et de la roche, de somme 1, pour une surface de pente
/// `slope` (1 − la composante verticale de sa normale : 0 à plat, 1 à la verticale) et de hauteur
/// relative `elevation` (0 au fond de la vallée, 1 aux crêtes).
[[nodiscard]] glm::vec3 layerWeightsOf(float slope, float elevation);

/// Les poids en chaque échantillon du terrain, en RGBA 8 bits (le quatrième canal à 0) : la carte
/// de poids que lit le shader.
[[nodiscard]] std::vector<glm::u8vec4> weightMapOf(const Heightmap& heightmap);

} // namespace levain::terrain
