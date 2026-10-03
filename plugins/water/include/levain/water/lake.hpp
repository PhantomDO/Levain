#pragma once

// L'eau (M5.7) : un lac calme. Sa surface est plate, et ce sont des normales qui l'animent : deux
// lectures d'une même carte de vaguelettes, qui glissent dans deux directions.

#include <cstdint>

#include <glm/glm.hpp>

#include "levain/assets/image.hpp"

namespace levain::water
{

/// Un disque d'eau plate à la hauteur `level`, en mètres. Son fond est le terrain : l'eau se
/// dessine sur le carré qui contient le disque, et le terrain qui dépasse de l'eau la cache.
struct Lake
{
    glm::vec2 center{0.0f};
    float radius = 0.0f;
    float level = 0.0f;
};

/// La carte de normales des vaguelettes, `size` × `size` (une puissance de deux) : un bruit
/// fractal dont on prend la pente, qui se répète sans couture d'un bord à l'autre. Une normale en
/// espace tangent par pixel : x dans le rouge, y dans le vert, le haut dans le bleu, de 0 à 255
/// pour −1 à 1. Ses mips se calculent sur les octets (`ImageEncoding::Linear`).
[[nodiscard]] assets::Image rippleNormalMapOf(std::uint32_t size, std::uint32_t seed);

} // namespace levain::water
