#pragma once

// Le relief du terrain (M5.6) : une grille de hauteurs régulière, un échantillon par mètre, et ce
// qu'on en lit en tout point : la hauteur, la normale. La vallée de *Rando* est générée par le code
// en attendant d'être sculptée dans l'éditeur (M7.6).

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace levain::terrain
{

/// Des hauteurs en mètres, `size` × `size` échantillons espacés de `spacing` mètres, rangés ligne
/// par ligne : x croît le long d'une ligne, z d'une ligne à la suivante. L'échantillon (0, 0) est à
/// l'origine du terrain, et le terrain couvre `(size − 1) × spacing` mètres de côté.
struct Heightmap
{
    std::uint32_t size = 0;
    float spacing = 1.0f;
    std::vector<float> heights;
};

/// Une vallée : une cuvette dont le fond est à 0, bordée de crêtes, et un relief fractal
/// par-dessus.
struct ValleySettings
{
    std::uint32_t size = 513; ///< 513 échantillons à 1 m : 512 m de côté.
    float spacing = 1.0f;
    float rimHeight = 80.0f;    ///< La hauteur des crêtes au-dessus du fond.
    float roughness = 12.0f;    ///< L'amplitude du relief fractal, en mètres.
    float featureSize = 120.0f; ///< La taille des plus grandes bosses du relief, en mètres.
    std::uint32_t seed = 2026;  ///< Une autre graine, une autre vallée.
};

/// La vallée de `settings`, la même à chaque appel pour la même graine.
[[nodiscard]] Heightmap valleyOf(const ValleySettings& settings);

/// La hauteur au point (x, z) du terrain, interpolée entre les quatre échantillons voisins ;
/// au-delà du bord, celle du bord.
[[nodiscard]] float heightAt(const Heightmap& heightmap, glm::vec2 position);

/// La normale au point (x, z), tirée des pentes entre échantillons voisins.
[[nodiscard]] glm::vec3 normalAt(const Heightmap& heightmap, glm::vec2 position);

/// Le côté du terrain, en mètres.
[[nodiscard]] float extentOf(const Heightmap& heightmap);

} // namespace levain::terrain
