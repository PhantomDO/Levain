#include "levain/render/light_clusters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>

#include "levain/render/shader.hpp"

namespace levain::render
{

namespace
{

/// Les clusters d'un groupe de threads : `numthreads` dans `shaders/light_clusters.slang`.
constexpr std::uint32_t ClustersPerGroup = 64;

/// Les tris qu'une command list peut enregistrer : un par vue (la caméra, plus tard les ombres).
constexpr std::uint32_t MaxSortsPerCommandList = 4;

/// Les constantes du tri, telles que les lit `shaders/light_clusters.slang`.
struct ClusterConstants
{
    glm::mat4 view;
    glm::mat4 inverseProjection;
    glm::uvec3 grid;
    std::uint32_t lightCount;
    float nearPlane;
    float farPlane;
    glm::vec2 padding;
};

static_assert(sizeof(ClusterConstants) == 160, "disposition lue par light_clusters.slang");

/// La direction, dans le repère de la caméra, du rayon qui passe par le point (`ndc`) de l'écran,
/// mise à l'échelle pour que z vaille −1 : un point à la profondeur d est `rayOf(ndc) * d`.
/// Toute profondeur de l'espace de découpe convient (0,5 ici) : en perspective, le rayon passe par
/// l'œil.
glm::vec3 rayOf(glm::vec2 ndc, const glm::mat4& inverseProjection)
{
    const glm::vec4 point = inverseProjection * glm::vec4{ndc, 0.5f, 1.0f};
    const glm::vec3 view = glm::vec3{point} / point.w;
    return view / -view.z;
}

} // namespace

ClusterView clusterViewOf(const Camera& camera, float aspectRatio)
{
    return ClusterView{.view = viewOf(camera),
                       .projection = projectionOf(camera, aspectRatio),
                       .nearPlane = camera.nearPlane,
                       .farPlane = camera.farPlane};
}

std::uint32_t clusterCountOf(const ClusterGrid& grid)
{
    return grid.x * grid.y * grid.z;
}

std::uint32_t clusterIndexOf(const ClusterGrid& grid, glm::uvec3 cell)
{
    return cell.x + (grid.x * (cell.y + (grid.y * cell.z)));
}

float sliceDepthOf(const ClusterGrid& grid, const ClusterView& view, std::uint32_t slice)
{
    return view.nearPlane * std::pow(view.farPlane / view.nearPlane,
                                     static_cast<float>(slice) / static_cast<float>(grid.z));
}

ClusterBox clusterBoxOf(const ClusterGrid& grid, const ClusterView& view, glm::uvec3 cell)
{
    // Les quatre coins de la case à l'écran, en coordonnées normalisées (−1 à 1), puis leurs
    // rayons prolongés jusqu'aux deux profondeurs de la tranche.
    const glm::vec2 cells{static_cast<float>(grid.x), static_cast<float>(grid.y)};
    const glm::vec2 ndcMin = (glm::vec2{cell.x, cell.y} / cells * 2.0f) - 1.0f;
    const glm::vec2 ndcMax = (glm::vec2{cell.x + 1, cell.y + 1} / cells * 2.0f) - 1.0f;
    const glm::mat4 inverseProjection = glm::inverse(view.projection);
    const std::array<glm::vec2, 4> corners{ndcMin, glm::vec2{ndcMax.x, ndcMin.y},
                                           glm::vec2{ndcMin.x, ndcMax.y}, ndcMax};
    const std::array<float, 2> depths{sliceDepthOf(grid, view, cell.z),
                                      sliceDepthOf(grid, view, cell.z + 1)};
    ClusterBox box{.min = glm::vec3{std::numeric_limits<float>::max()},
                   .max = glm::vec3{std::numeric_limits<float>::lowest()}};
    for (const glm::vec2 corner : corners)
    {
        const glm::vec3 ray = rayOf(corner, inverseProjection);
        for (const float depth : depths)
        {
            box.min = glm::min(box.min, ray * depth);
            box.max = glm::max(box.max, ray * depth);
        }
    }
    return box;
}

bool sphereTouchesBox(glm::vec3 center, float radius, const ClusterBox& box)
{
    const glm::vec3 closest = glm::clamp(center, box.min, box.max);
    const glm::vec3 offset = center - closest;
    return glm::dot(offset, offset) <= radius * radius;
}

std::vector<std::vector<std::uint32_t>> lightsPerClusterOf(const ClusterGrid& grid,
                                                           const ClusterView& view,
                                                           std::span<const PointLight> lights)
{
    std::vector<std::vector<std::uint32_t>> result(clusterCountOf(grid));
    for (std::uint32_t z = 0; z < grid.z; ++z)
    {
        for (std::uint32_t y = 0; y < grid.y; ++y)
        {
            for (std::uint32_t x = 0; x < grid.x; ++x)
            {
                const glm::uvec3 cell{x, y, z};
                const ClusterBox box = clusterBoxOf(grid, view, cell);
                for (std::uint32_t light = 0; light < lights.size(); ++light)
                {
                    const glm::vec3 center =
                        glm::vec3{view.view * glm::vec4{lights[light].position, 1.0f}};
                    if (sphereTouchesBox(center, lights[light].range, box))
                    {
                        result[clusterIndexOf(grid, cell)].push_back(light);
                    }
                }
            }
        }
    }
    return result;
}

core::Result<LightClusterPass> createLightClusterPass(nvrhi::IDevice& device,
                                                      const ClusterGrid& grid)
{
    auto shader = loadShader(device, "light_clusters.computeMain", nvrhi::ShaderType::Compute);
    if (!shader)
    {
        return std::unexpected(shader.error());
    }
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                           nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
                           nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
                           nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1)};

    LightClusterPass pass;
    pass.grid = grid;
    pass.shader = std::move(*shader);
    pass.layout = device.createBindingLayout(layoutDesc);
    pass.pipeline = device.createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(pass.shader).addBindingLayout(pass.layout));
    // Volatile, comme les constantes de scène (mesh_pass.cpp) : quelques tris par command list au
    // plus (une par vue).
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(ClusterConstants))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(MaxSortsPerCommandList)
                                             .setDebugName("clusters : constantes"));
    pass.lights = device.createBuffer(nvrhi::BufferDesc()
                                          .setByteSize(MaxPointLights * sizeof(PointLight))
                                          .setStructStride(sizeof(PointLight))
                                          .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                          .setKeepInitialState(true)
                                          .setDebugName("clusters : lumières"));
    const std::uint32_t clusters = clusterCountOf(grid);
    // Les deux listes servent au compute (écriture), puis au shader d'éclairage (lecture) : NVRHI
    // place la barrière entre les deux (suivi automatique des états).
    const auto listDesc = [](std::uint64_t size, const char* name)
    {
        return nvrhi::BufferDesc()
            .setByteSize(size)
            .setStructStride(sizeof(std::uint32_t))
            .setCanHaveUAVs(true)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName(name);
    };
    pass.lightCounts = device.createBuffer(
        listDesc(std::uint64_t{clusters} * sizeof(std::uint32_t), "clusters : comptes"));
    pass.lightIndices = device.createBuffer(
        listDesc(std::uint64_t{clusters} * MaxLightsPerCluster * sizeof(std::uint32_t),
                 "clusters : indices des lumières"));
    if (!pass.layout || !pass.pipeline || !pass.constants || !pass.lights || !pass.lightCounts ||
        !pass.lightIndices)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "passe de tri des lumières refusée par NVRHI");
    }
    pass.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, pass.lights))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, pass.lightCounts))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(1, pass.lightIndices)),
        pass.layout);
    return pass;
}

core::Result<void> assignLightsToClusters(nvrhi::ICommandList& commandList,
                                          const LightClusterPass& pass,
                                          std::span<const PointLight> lights,
                                          const ClusterView& view)
{
    if (lights.size() > MaxPointLights)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} lumières ponctuelles, {} au plus (ADR-0024)",
                                           lights.size(), MaxPointLights));
    }
    const ClusterConstants constants{
        .view = view.view,
        .inverseProjection = glm::inverse(view.projection),
        .grid = {pass.grid.x, pass.grid.y, pass.grid.z},
        .lightCount = static_cast<std::uint32_t>(lights.size()),
        .nearPlane = view.nearPlane,
        .farPlane = view.farPlane,
        .padding = {},
    };
    commandList.writeBuffer(pass.constants, &constants, sizeof(constants));
    if (!lights.empty())
    {
        commandList.writeBuffer(pass.lights, lights.data(), lights.size_bytes());
    }
    nvrhi::ComputeState state;
    state.pipeline = pass.pipeline;
    state.addBindingSet(pass.bindings);
    commandList.setComputeState(state);
    commandList.dispatch((clusterCountOf(pass.grid) + ClustersPerGroup - 1) / ClustersPerGroup);
    return {};
}

} // namespace levain::render
