#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/mesh.hpp"

namespace levain::render
{

/// Le format du depth buffer : 32 bits flottants, disponible partout.
inline constexpr nvrhi::Format DepthFormat = nvrhi::Format::D32;

/// Combien de `drawMesh` une command list peut enregistrer. Chaque dessin écrit ses constantes dans
/// une nouvelle version du buffer volatil, et NVRHI refuse d'en dépasser le nombre prévu : c'est
/// Sponza (105 dessins) qui l'a montré, en Debug seulement, la validation de NVRHI étant éteinte en
/// Release.
// ponytail: 4 096 versions de 128 octets, 512 Kio réservés. Passer la matrice du modèle en push
// constants quand la passe sera refaite pour le PBR (M5.1) : plus aucune limite par dessin.
inline constexpr std::uint32_t MaxMeshDrawsPerCommandList = 4096;

/// Les constantes du shader. Doit correspondre à `SceneConstants` dans `shaders/mesh.slang`.
struct SceneConstants
{
    glm::mat4 viewProjection;
    glm::mat4 model;
};

/// Les facteurs d'un matériau metallic-roughness, avec les défauts de glTF. Doit correspondre à
/// `MaterialConstants` dans `shaders/mesh.slang`.
struct MaterialConstants
{
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float padding = 0.0f;
};

static_assert(sizeof(MaterialConstants) == 32, "disposition lue par shaders/mesh.slang");

/// Les textures d'un matériau. La couleur de base est en sRGB ; rugosité-métal et normal map sont
/// des données, à créer dans un format UNORM : le GPU ne doit pas les « délinéariser ».
struct MaterialTextures
{
    nvrhi::ITexture* baseColor = nullptr;
    nvrhi::ITexture* metallicRoughness = nullptr;
    nvrhi::ITexture* normal = nullptr;
};

/// Des textures 1 × 1 qui ne changent rien, pour un matériau qui n'a pas les siennes : du blanc
/// (couleur, et rugosité-métal, que ses facteurs règlent seuls) et la normale « tout droit ».
struct MaterialDefaults
{
    nvrhi::TextureHandle white;
    nvrhi::TextureHandle whiteData;
    nvrhi::TextureHandle flatNormal;
};

[[nodiscard]] MaterialDefaults createMaterialDefaults(nvrhi::IDevice& device,
                                                      nvrhi::ICommandList& commandList);

/// Les textures de `textures`, et celles de `defaults` là où il en manque.
[[nodiscard]] MaterialTextures withDefaults(MaterialTextures textures,
                                            const MaterialDefaults& defaults);

/// Dessine des meshes indexés avec un depth buffer. Tout est créé une fois ; chaque dessin ne fait
/// qu'écrire les constantes et enregistrer un draw.
struct MeshPass
{
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle frameLayout;
    nvrhi::BindingLayoutHandle materialLayout;
    nvrhi::BufferHandle sceneConstants;
    nvrhi::BindingSetHandle frameBindings;
    nvrhi::GraphicsPipelineHandle pipeline;
};

/// Crée la passe pour des framebuffers de ce format, couleur et profondeur (`DepthFormat`).
[[nodiscard]] core::Result<MeshPass> createMeshPass(nvrhi::IDevice& device,
                                                    const nvrhi::FramebufferInfo& target);

/// Le nom des sources de la passe dans `shaders/`, sans extension : le hot-reload recrée la passe
/// quand ce fichier change (ADR-0014).
inline constexpr const char* MeshPassShaderFile = "mesh";

/// Recharge les shaders compilés de la passe et recrée son pipeline, sans toucher aux layouts, aux
/// buffers ni aux binding sets. En cas d'échec, `pass` reste tel quel (#46).
[[nodiscard]] core::Result<void> reloadMeshPassShaders(nvrhi::IDevice& device, MeshPass& pass,
                                                       const nvrhi::FramebufferInfo& target);

/// Le depth buffer à la taille de l'image où l'on dessine : recréé seulement quand elle change.
[[nodiscard]] nvrhi::ITexture* ensureDepthTexture(nvrhi::IDevice& device,
                                                  nvrhi::TextureHandle& depth, std::uint32_t width,
                                                  std::uint32_t height);

/// Le binding set d'un matériau (`space2`, ADR-0013) : ses constantes, ses trois textures (aucune
/// absente : `withDefaults`) et son sampler. Créé une fois par matériau, pas à chaque dessin ; ses
/// constantes s'envoient par `commandList`.
[[nodiscard]] nvrhi::BindingSetHandle
createMaterialBindings(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                       const MeshPass& pass, const MaterialConstants& constants,
                       const MaterialTextures& textures, nvrhi::ISampler& sampler);

/// Enregistre le dessin de toutes les `instances` de `mesh`, en un seul appel, dans `framebuffer`,
/// qui doit avoir un depth buffer.
void drawMesh(nvrhi::ICommandList& commandList, const MeshPass& pass,
              nvrhi::IFramebuffer& framebuffer, const Mesh& mesh, const Instances& instances,
              nvrhi::IBindingSet& material, const SceneConstants& constants);

} // namespace levain::render
