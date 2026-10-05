#include "levain/water/lake.hpp"

#include <cmath>

namespace levain::water
{

namespace
{

/// Le nombre de cellules du bruit le plus large sur un côté de la carte. Les octaves suivantes en
/// ont deux, quatre, huit fois plus.
constexpr std::int32_t BasePeriod = 8;
constexpr std::uint32_t Octaves = 4;
/// La pente des vaguelettes : de combien la normale penche pour une pente unité du bruit, mesurée
/// sur un côté de la carte. Jusqu'à 20° environ.
constexpr float RippleSlope = 0.02f;

/// Un nombre de [0, 1) tiré des coordonnées entières d'un point de la grille du bruit et de la
/// graine, comme celui du relief du terrain (heightmap.cpp).
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

/// L'indice ramené dans [0, period) : la grille se referme sur elle-même, et le bruit se répète.
std::int32_t wrapped(std::int32_t index, std::int32_t period)
{
    return ((index % period) + period) % period;
}

/// Le bruit de valeur sur une grille de `period` × `period` cellules qui se répète : le point
/// (period, z) retombe sur (0, z). De −1 à 1.
float periodicNoiseAt(glm::vec2 point, std::int32_t period, std::uint32_t seed)
{
    const glm::vec2 cell = glm::floor(point);
    const glm::vec2 local = point - cell;
    const glm::vec2 t = local * local * (3.0f - 2.0f * local);
    const auto x = static_cast<std::int32_t>(cell.x);
    const auto z = static_cast<std::int32_t>(cell.y);
    const auto valueAt = [&](std::int32_t dx, std::int32_t dz)
    { return latticeValueOf(wrapped(x + dx, period), wrapped(z + dz, period), seed); };
    return (glm::mix(glm::mix(valueAt(0, 0), valueAt(1, 0), t.x),
                     glm::mix(valueAt(0, 1), valueAt(1, 1), t.x), t.y) *
            2.0f) -
           1.0f;
}

/// La hauteur des vaguelettes au point `uv` de la carte, de 0 à 1 sur un côté, et périodique.
float rippleHeightAt(glm::vec2 uv, std::uint32_t seed)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    std::int32_t period = BasePeriod;
    for (std::uint32_t octave = 0; octave < Octaves; ++octave)
    {
        sum += amplitude * periodicNoiseAt(uv * static_cast<float>(period), period, seed + octave);
        period *= 2;
        amplitude *= 0.5f;
    }
    return sum;
}

std::uint8_t encodedOf(float component)
{
    return static_cast<std::uint8_t>(std::lround((component * 0.5f + 0.5f) * 255.0f));
}

} // namespace

assets::Image rippleNormalMapOf(std::uint32_t size, std::uint32_t seed)
{
    assets::Image image{.width = size, .height = size, .rgba = {}};
    image.rgba.reserve(std::size_t{size} * size * 4);
    const float texel = 1.0f / static_cast<float>(size);
    for (std::uint32_t v = 0; v < size; ++v)
    {
        for (std::uint32_t u = 0; u < size; ++u)
        {
            const glm::vec2 uv = glm::vec2{u, v} * texel;
            // Les pentes entre les pixels voisins, au-delà du bord comprises : le bruit est
            // périodique, la pente aussi.
            const float slopeU = (rippleHeightAt(uv + glm::vec2{texel, 0.0f}, seed) -
                                  rippleHeightAt(uv - glm::vec2{texel, 0.0f}, seed)) /
                                 (2.0f * texel);
            const float slopeV = (rippleHeightAt(uv + glm::vec2{0.0f, texel}, seed) -
                                  rippleHeightAt(uv - glm::vec2{0.0f, texel}, seed)) /
                                 (2.0f * texel);
            const glm::vec3 normal =
                glm::normalize(glm::vec3{-slopeU * RippleSlope, -slopeV * RippleSlope, 1.0f});
            image.rgba.insert(image.rgba.end(),
                              {encodedOf(normal.x), encodedOf(normal.y), encodedOf(normal.z), 255});
        }
    }
    return image;
}

} // namespace levain::water
