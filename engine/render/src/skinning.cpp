#include "levain/render/skinning.hpp"

#include <cstdint>

#include "levain/core/assert.hpp"
#include "levain/render/shader.hpp"

namespace levain::render
{

namespace
{

/// Le nombre de sommets d'un groupe de threads : `numthreads` dans `shaders/skinning.slang`.
constexpr std::uint32_t VerticesPerGroup = 64;

} // namespace

core::Result<SkinningPass> createSkinningPass(nvrhi::IDevice& device)
{
    auto shader = loadShader(device, "skinning.computeMain", nvrhi::ShaderType::Compute);
    if (!shader)
    {
        return std::unexpected(shader.error());
    }
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
                           nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
                           nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)};
    nvrhi::BindingLayoutHandle layout = device.createBindingLayout(layoutDesc);
    nvrhi::ComputePipelineHandle pipeline = device.createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(*shader).addBindingLayout(layout));
    if (!layout || !pipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData, "passe de skinning refusée par NVRHI");
    }
    return SkinningPass{
        .shader = std::move(*shader), .layout = std::move(layout), .pipeline = std::move(pipeline)};
}

SkinnedMesh createSkinnedMesh(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                              const SkinningPass& pass, std::span<const SkinnedVertex> vertices,
                              std::span<const std::uint32_t> indices, std::uint32_t jointCount)
{
    SkinnedMesh mesh;
    mesh.vertexCount = static_cast<std::uint32_t>(vertices.size());
    mesh.jointCount = jointCount;
    // Les états initiaux sont ceux où chaque buffer passe le plus clair de son temps ; NVRHI place
    // les transitions entre compute et dessin (suivi automatique des états, « Resource State
    // Tracking » dans la documentation de NVRHI).
    mesh.source = device.createBuffer(nvrhi::BufferDesc()
                                          .setByteSize(vertices.size_bytes())
                                          .setStructStride(sizeof(std::uint32_t))
                                          .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                          .setKeepInitialState(true)
                                          .setDebugName("skinning : sommets d'origine"));
    mesh.jointMatrices =
        device.createBuffer(nvrhi::BufferDesc()
                                .setByteSize(jointCount * sizeof(glm::mat4))
                                .setStructStride(sizeof(glm::mat4))
                                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                .setKeepInitialState(true)
                                .setDebugName("skinning : matrices des os"));
    mesh.skinned = {
        .vertexBuffer =
            device.createBuffer(nvrhi::BufferDesc()
                                    .setByteSize(vertices.size() * sizeof(MeshVertex))
                                    .setIsVertexBuffer(true)
                                    .setCanHaveUAVs(true)
                                    .setStructStride(sizeof(std::uint32_t))
                                    .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                    .setKeepInitialState(true)
                                    .setDebugName("skinning : sommets déformés")),
        .indexBuffer = device.createBuffer(nvrhi::BufferDesc()
                                               .setByteSize(indices.size_bytes())
                                               .setIsIndexBuffer(true)
                                               .setInitialState(nvrhi::ResourceStates::IndexBuffer)
                                               .setKeepInitialState(true)
                                               .setDebugName("skinning : indices")),
        .indexCount = static_cast<std::uint32_t>(indices.size()),
        // Sans boîte : l'animation la déformerait, et le culling écarterait un bras levé.
        .bounds = std::nullopt,
    };
    mesh.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, mesh.source))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, mesh.jointMatrices))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, mesh.skinned.vertexBuffer)),
        pass.layout);
    commandList.writeBuffer(mesh.source, vertices.data(), vertices.size_bytes());
    commandList.writeBuffer(mesh.skinned.indexBuffer, indices.data(), indices.size_bytes());
    return mesh;
}

void skinMesh(nvrhi::ICommandList& commandList, const SkinningPass& pass, const SkinnedMesh& mesh,
              std::span<const glm::mat4> jointMatrices)
{
    LEVAIN_ASSERT(jointMatrices.size() == mesh.jointCount, "une matrice par os du skin");
    commandList.writeBuffer(mesh.jointMatrices, jointMatrices.data(), jointMatrices.size_bytes());
    nvrhi::ComputeState state;
    state.pipeline = pass.pipeline;
    state.addBindingSet(mesh.bindings);
    commandList.setComputeState(state);
    commandList.dispatch((mesh.vertexCount + VerticesPerGroup - 1) / VerticesPerGroup);
}

} // namespace levain::render
