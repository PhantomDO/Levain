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

/// Les tailles des textures de l'IBL. Petites : l'irradiance varie lentement avec la direction,
/// et les mips spéculaires les plus rugueux sont flous. `SpecularSize` et `SpecularMips` doivent
/// valoir leurs homonymes de shaders/environment.slang.
inline constexpr std::uint32_t IrradianceSize = 32;
inline constexpr std::uint32_t SpecularSize = 128;
inline constexpr std::uint32_t SpecularMips = 6;
inline constexpr std::uint32_t BrdfLutSize = 128;

/// L'éclairage par l'image, en trois parties : ce qu'un matériau mat reçoit (`irradiance`), ce
/// qu'un matériau brillant reflète (`specular`), et la part que la BRDF en renvoie (`brdfLut`).
/// Le mesh shader assemble les trois (la « split sum » de Karis).
struct Environment
{
    /// Le ciel, `std::bit_width(taille)` niveaux de mip, jusqu'à 1 × 1 texel par face.
    nvrhi::TextureHandle cube;
    /// L'irradiance divisée par π, par direction de la normale.
    nvrhi::TextureHandle irradiance;
    /// Le reflet du ciel par direction, flouté par la rugosité : 0 au premier mip, 1 au dernier.
    nvrhi::TextureHandle specular;
    /// En x le cosinus de vue, en y la rugosité ; en rouge et vert, l'échelle et le biais de F0.
    nvrhi::TextureHandle brdfLut;
};

/// Convertit `image` en une cubemap de `cubeSize` texels de côté (une puissance de deux), calcule
/// ses mips, puis les convolutions de l'IBL, sur le GPU : la fonction exécute sa propre command
/// list.
[[nodiscard]] core::Result<Environment> createEnvironment(nvrhi::IDevice& device,
                                                          const EnvironmentImage& image,
                                                          std::uint32_t cubeSize = 512);

/// Un ciel uniforme de luminance `radiance` dans toutes les directions : l'ambiance d'avant l'IBL,
/// pour une scène sans HDRI (les tests de fumée, le navigateur).
[[nodiscard]] core::Result<Environment> createUniformEnvironment(nvrhi::IDevice& device,
                                                                 glm::vec3 radiance);

/// La direction qui passe par le point `uv` (de −1 à 1) de la face `face` : l'ordre et
/// l'orientation des cubemaps de Vulkan, Direct3D et WebGPU (+X, −X, +Y, −Y, +Z, −Z). La même
/// formule que shaders/environment.slang, pour les tests.
[[nodiscard]] glm::vec3 cubeDirectionOf(std::uint32_t face, glm::vec2 uv);

/// La direction que regarde le point `uv` (de 0 à 1) de l'image équirectangulaire : l'inverse de
/// `equirectUvOf` dans shaders/environment.slang.
[[nodiscard]] glm::vec3 equirectDirectionOf(glm::vec2 uv);

} // namespace levain::render
