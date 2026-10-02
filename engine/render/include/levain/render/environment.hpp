#pragma once

// L'environnement de l'éclairage par l'image (IBL, M5.4) : ce que la scène reflète et qui l'éclaire
// de toutes les directions à la fois, le ciel. L'HDRI, une photo à 360° en équirectangulaire,
// devient une cubemap au chargement, avec tous ses niveaux de mip : six faces carrées se lisent
// mieux sur le GPU, sans la déformation aux pôles de l'équirectangulaire.

#include <cstdint>
#include <span>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

namespace levain::render
{

/// Une image HDR en lumière linéaire, quatre flottants par pixel, lignes contiguës, le haut de
/// l'image au zénith. `render` ne connaît pas `assets` (SPECS §7) : l'appelant fait le lien avec
/// `assets::HdrImage`.
struct EnvironmentImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const float> rgba;
};

/// 16 bits flottants par canal : assez pour un ciel, jusqu'à 65504. Les storage textures de
/// WebGPU l'acceptent, et le GPU sait le filtrer partout (le 32 bits demande une extension).
inline constexpr nvrhi::Format EnvironmentFormat = nvrhi::Format::RGBA16_FLOAT;

struct Environment
{
    /// La cubemap, `std::bit_width(taille)` niveaux de mip, jusqu'à 1 × 1 texel par face.
    nvrhi::TextureHandle cube;
};

/// Convertit `image` en une cubemap de `cubeSize` texels de côté (une puissance de deux), et
/// calcule ses mips, sur le GPU : la fonction exécute sa propre command list.
[[nodiscard]] core::Result<Environment> createEnvironment(nvrhi::IDevice& device,
                                                          const EnvironmentImage& image,
                                                          std::uint32_t cubeSize = 512);

/// La direction qui passe par le point `uv` (de −1 à 1) de la face `face` : l'ordre et
/// l'orientation des cubemaps de Vulkan, Direct3D et WebGPU (+X, −X, +Y, −Y, +Z, −Z). La même
/// formule que shaders/environment.slang, pour les tests.
[[nodiscard]] glm::vec3 cubeDirectionOf(std::uint32_t face, glm::vec2 uv);

/// La direction que regarde le point `uv` (de 0 à 1) de l'image équirectangulaire : l'inverse de
/// `equirectUvOf` dans shaders/environment.slang.
[[nodiscard]] glm::vec3 equirectDirectionOf(glm::vec2 uv);

} // namespace levain::render
