#include "levain/terrain/heightmap.hpp"

#include <algorithm>
#include <cmath>

namespace levain::terrain
{

namespace
{

/// Un nombre de [0, 1) tiré des coordonnées entières d'un point de la grille du bruit et de la
/// graine : toujours le même pour les mêmes entrées (un hachage, pas un générateur).
float latticeValueOf(std::int32_t x, std::int32_t z, std::uint32_t seed)
{
    std::uint32_t h = seed;
    h ^= static_cast<std::uint32_t>(x) * 0x27d4eb2du;
    h ^= static_cast<std::uint32_t>(z) * 0x165667b1u;
    h = (h ^ (h >> 15u)) * 0x85ebca6bu;
    h = (h ^ (h >> 13u)) * 0xc2b2ae35u;
    h ^= h >> 16u;
    return static_cast<float>(h >> 8u) / static_cast<float>(1u << 24u);
}

/// Le bruit de valeur : les valeurs de la grille, interpolées en douceur (smoothstep) entre ses
/// points. De −1 à 1.
float valueNoiseAt(glm::vec2 point, std::uint32_t seed)
{
    const glm::vec2 cell = glm::floor(point);
    const glm::vec2 local = point - cell;
    const glm::vec2 t = local * local * (3.0f - 2.0f * local);
    const auto x = static_cast<std::int32_t>(cell.x);
    const auto z = static_cast<std::int32_t>(cell.y);
    const float a = latticeValueOf(x, z, seed);
    const float b = latticeValueOf(x + 1, z, seed);
    const float c = latticeValueOf(x, z + 1, seed);
    const float d = latticeValueOf(x + 1, z + 1, seed);
    return (glm::mix(glm::mix(a, b, t.x), glm::mix(c, d, t.x), t.y) * 2.0f) - 1.0f;
}

/// Cinq octaves de bruit, chacune deux fois plus fine et deux fois plus faible : de grandes bosses,
/// et des petites dessus. Environ de −1 à 1.
float fractalNoiseAt(glm::vec2 point, std::uint32_t seed)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    for (std::uint32_t octave = 0; octave < 5; ++octave)
    {
        sum += amplitude * valueNoiseAt(point, seed + octave);
        point *= 2.0f;
        amplitude *= 0.5f;
    }
    return sum / 0.97f; // 0,5 + 0,25 + … : la somme des amplitudes
}

/// L'échantillon (x, z), le bord répété au-delà.
float sampleAt(const Heightmap& heightmap, std::int64_t x, std::int64_t z)
{
    const auto last = static_cast<std::int64_t>(heightmap.size) - 1;
    const auto column = static_cast<std::size_t>(std::clamp<std::int64_t>(x, 0, last));
    const auto row = static_cast<std::size_t>(std::clamp<std::int64_t>(z, 0, last));
    return heightmap.heights[(row * heightmap.size) + column];
}

} // namespace

Heightmap valleyOf(const ValleySettings& settings)
{
    Heightmap heightmap{.size = settings.size, .spacing = settings.spacing, .heights = {}};
    heightmap.heights.resize(std::size_t{settings.size} * settings.size);
    const float half = extentOf(heightmap) / 2.0f;
    for (std::uint32_t z = 0; z < settings.size; ++z)
    {
        for (std::uint32_t x = 0; x < settings.size; ++x)
        {
            const glm::vec2 position = glm::vec2{x, z} * settings.spacing;
            // La cuvette : plate au centre, qui monte vers les crêtes, à 1 du centre au milieu
            // d'un bord. Le relief, faible au fond, se creuse sur les versants et les crêtes.
            // Un rayon déformé par un bruit à grande échelle : une vallée aux bords irréguliers, et
            // non un cercle.
            const float radius = (glm::length(position - half) / half) +
                                 (0.18f * fractalNoiseAt(position / (2.5f * settings.featureSize),
                                                         settings.seed + 99));
            const float bowl = glm::smoothstep(0.3f, 0.8f, radius) * settings.rimHeight;
            const float relief = fractalNoiseAt(position / settings.featureSize, settings.seed) *
                                 settings.roughness * (0.3f + glm::smoothstep(0.2f, 0.9f, radius));
            // Le lac : un creux arrondi, le plus profond au centre, qui rejoint le fond en
            // douceur à son rayon.
            const float lake =
                settings.lakeDepth *
                (1.0f - glm::smoothstep(0.0f, settings.lakeRadius,
                                        glm::distance(position, settings.lakeCenter)));
            heightmap.heights[(std::size_t{z} * settings.size) + x] = bowl + relief - lake;
        }
    }
    return heightmap;
}

float heightAt(const Heightmap& heightmap, glm::vec2 position)
{
    const glm::vec2 sample = position / heightmap.spacing;
    const glm::vec2 cell = glm::floor(sample);
    const glm::vec2 t = sample - cell;
    const auto x = static_cast<std::int64_t>(cell.x);
    const auto z = static_cast<std::int64_t>(cell.y);
    return glm::mix(glm::mix(sampleAt(heightmap, x, z), sampleAt(heightmap, x + 1, z), t.x),
                    glm::mix(sampleAt(heightmap, x, z + 1), sampleAt(heightmap, x + 1, z + 1), t.x),
                    t.y);
}

glm::vec3 normalAt(const Heightmap& heightmap, glm::vec2 position)
{
    const float step = heightmap.spacing;
    const float slopeX = (heightAt(heightmap, position + glm::vec2{step, 0.0f}) -
                          heightAt(heightmap, position - glm::vec2{step, 0.0f})) /
                         (2.0f * step);
    const float slopeZ = (heightAt(heightmap, position + glm::vec2{0.0f, step}) -
                          heightAt(heightmap, position - glm::vec2{0.0f, step})) /
                         (2.0f * step);
    return glm::normalize(glm::vec3{-slopeX, 1.0f, -slopeZ});
}

float extentOf(const Heightmap& heightmap)
{
    return static_cast<float>(heightmap.size - 1) * heightmap.spacing;
}

} // namespace levain::terrain
