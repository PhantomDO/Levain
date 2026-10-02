#pragma once

#include <cstdint>
#include <span>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/mesh.hpp"

namespace levain::render
{

/// Un sommet avant skinning : celui de `MeshVertex`, plus les quatre os qui l'influencent (indices
/// dans le skin) et leurs poids. Doit correspondre à `SourceStride` dans `shaders/skinning.slang`.
struct SkinnedVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent;
    glm::vec3 color;
    glm::vec2 uv;
    glm::u16vec4 joints;
    glm::vec4 weights;
};

static_assert(sizeof(SkinnedVertex) == 84, "disposition lue mot par mot par skinning.slang");
static_assert(sizeof(MeshVertex) == 60, "disposition écrite mot par mot par skinning.slang");

/// Le skinning en compute (ADR-0022) : un seul pipeline, pour tous les meshes skinnés.
struct SkinningPass
{
    nvrhi::ShaderHandle shader;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::ComputePipelineHandle pipeline;
};

[[nodiscard]] core::Result<SkinningPass> createSkinningPass(nvrhi::IDevice& device);

/// Un mesh que déforme une pose : ses sommets d'origine, les matrices de ses os, et `skinned`, le
/// mesh déformé, que la passe des meshes dessine comme n'importe quel autre.
struct SkinnedMesh
{
    nvrhi::BufferHandle source;
    nvrhi::BufferHandle jointMatrices;
    Mesh skinned;
    nvrhi::BindingSetHandle bindings;
    std::uint32_t vertexCount = 0;
    std::uint32_t jointCount = 0;
};

/// Crée les buffers et enregistre l'envoi des sommets et des indices dans `commandList`. Tant que
/// `skinMesh` n'a pas tourné, le mesh déformé est vide.
[[nodiscard]] SkinnedMesh
createSkinnedMesh(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                  const SkinningPass& pass, std::span<const SkinnedVertex> vertices,
                  std::span<const std::uint32_t> indices, std::uint32_t jointCount);

/// Enregistre la déformation de `mesh` par `jointMatrices` (une par os du skin) : l'envoi des
/// matrices, puis un dispatch. À placer avant le dessin, dans la même command list : NVRHI met la
/// barrière entre l'écriture du compute et la lecture des sommets.
void skinMesh(nvrhi::ICommandList& commandList, const SkinningPass& pass, const SkinnedMesh& mesh,
              std::span<const glm::mat4> jointMatrices);

} // namespace levain::render
