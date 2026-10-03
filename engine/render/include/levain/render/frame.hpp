#pragma once

// Les ressources de l'image, partagées par toutes les passes qui éclairent une surface : les meshes
// et les passes des plugins, comme le terrain (ADR-0025). Le binding set de space0 (ADR-0013) : les
// constantes de scène (une version par dessin) et de la frame, les lumières triées par cluster,
// l'atlas des ombres et l'environnement de l'IBL. Un contrat avec shaders/lighting.slang, et donc
// avec les plugins.

#include <array>
#include <cstdint>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/environment.hpp"
#include "levain/render/light_clusters.hpp"
#include "levain/render/shadows.hpp"

namespace levain::render
{

/// Combien de dessins une command list peut enregistrer, toutes passes confondues
/// (`setSceneConstants`). Chaque dessin écrit ses constantes dans une nouvelle version du buffer
/// volatil, et NVRHI refuse d'en dépasser le nombre prévu : c'est Sponza (105 dessins) qui l'a
/// montré, en Debug seulement, la validation de NVRHI étant éteinte en Release.
// ponytail: 4 096 versions de 128 octets, 512 Kio réservés. Passer la matrice du modèle en push
// constants quand la passe sera refaite pour le PBR (M5.1) : plus aucune limite par dessin.
inline constexpr std::uint32_t MaxMeshDrawsPerCommandList = 4096;

/// Les constantes du shader. Doit correspondre à `SceneConstants` dans `shaders/lighting.slang`.
struct SceneConstants
{
    glm::mat4 viewProjection;
    glm::mat4 model;
};

/// L'éclairage d'une image : la caméra (pour les reflets, et pour retrouver le cluster d'un pixel),
/// le soleil, et le ciel (l'environnement de `createFrameBindings`), multiplié par
/// `environmentIntensity`.
struct FrameLighting
{
    ClusterView view;
    glm::vec3 cameraPosition{0.0f};
    Sun sun;
    float environmentIntensity = 1.0f;
    /// Les cascades des ombres du soleil (`cascadesOf`), que la passe d'ombres a dessinées.
    std::array<Cascade, CascadeCount> cascades{};
};

struct FrameBindings
{
    nvrhi::BindingLayoutHandle layout; ///< space0, le premier layout de chaque pipeline éclairé.
    nvrhi::BufferHandle sceneConstants;
    nvrhi::BufferHandle frameConstants;
    nvrhi::BindingSetHandle bindings;
};

/// Les ressources de l'image, créées une fois : elles lisent les lumières triées par `lights`
/// (ADR-0024), l'atlas des ombres de `shadows` (M5.3) et l'éclairage par l'image de `environment`
/// (M5.4).
[[nodiscard]] core::Result<FrameBindings> createFrameBindings(nvrhi::IDevice& device,
                                                              const LightClusterPass& lights,
                                                              const ShadowPass& shadows,
                                                              const Environment& environment);

/// Enregistre l'éclairage de l'image, une fois par command list, avant les dessins : après
/// `assignLightsToClusters`, dont il reprend la grille.
void setFrameLighting(nvrhi::ICommandList& commandList, const FrameBindings& frame,
                      const LightClusterPass& lights, const ShadowPass& shadows,
                      const FrameLighting& lighting);

/// Enregistre les constantes du prochain dessin, dans une nouvelle version du buffer volatil.
void setSceneConstants(nvrhi::ICommandList& commandList, const FrameBindings& frame,
                       const SceneConstants& constants);

} // namespace levain::render
