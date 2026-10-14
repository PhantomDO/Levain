#pragma once

#include <cmath>

namespace levain::core
{

/// Linéaire vers sRGB, la fonction exacte de l'IEC 61966-2-1 (le seuil 0,0031308, puis la puissance
/// 1/2,4), pour une composante de [0, 1]. C'est la copie CPU de `srgbFromLinear` de
/// shaders/tonemap.slang, l'inverse de `linearFromSrgb` de shaders/ui.slang : elle sert à écrire ce
/// qu'une image UNORM doit contenir (l'effacement de la présentation, les attendus des tests).
[[nodiscard]] inline float srgbFromLinear(float linear)
{
    return linear <= 0.0031308f ? linear * 12.92f
                                : (1.055f * std::pow(linear, 1.0f / 2.4f)) - 0.055f;
}

} // namespace levain::core
