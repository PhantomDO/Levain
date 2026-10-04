#include "levain/render/shadows.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include "levain/render/mesh_pass.hpp"
#include "levain/render/shader.hpp"

namespace levain::render
{

namespace
{

/// Ce que la projection du soleil regarde en plus, entre lui et la tranche : un objet hors de la
/// tranche peut y jeter son ombre (un arbre derrière la caméra, une falaise).
// ponytail: une marge fixe de 50 m ; l'ajuster à la scène (ses objets vus du soleil) quand un
// relief plus haut que ça perdra son ombre.
constexpr float CasterMargin = 50.0f;

/// Les constantes d'un dessin dans une cascade, telles que les lit `shaders/shadow.slang`.
struct ShadowConstants
{
    glm::mat4 viewProjection;
    glm::mat4 model;
};

/// Le rayon arrondi au 1/16 supérieur : le flottant du calcul varie d'une image à l'autre, et un
/// rayon qui bouge d'un rien changerait la taille des texels.
float roundedRadius(float radius)
{
    return std::ceil(radius * 16.0f) / 16.0f;
}

} // namespace

std::array<float, CascadeCount + 1> cascadeSplitsOf(const Camera& camera,
                                                    const CascadeSettings& settings)
{
    const float nearDepth = camera.nearPlane;
    const float farDepth = std::min(settings.shadowDistance, camera.farPlane);
    std::array<float, CascadeCount + 1> splits{};
    for (std::uint32_t i = 0; i <= CascadeCount; ++i)
    {
        const float ratio = static_cast<float>(i) / static_cast<float>(CascadeCount);
        const float uniform = nearDepth + ((farDepth - nearDepth) * ratio);
        const float logarithmic = nearDepth * std::pow(farDepth / nearDepth, ratio);
        splits[i] = glm::mix(uniform, logarithmic, settings.splitBlend);
    }
    // Exactement les deux bouts, sans l'écart d'arrondi du calcul.
    splits.front() = nearDepth;
    splits.back() = farDepth;
    return splits;
}

std::array<glm::vec3, 8> frustumSliceCornersOf(const Camera& camera, float aspectRatio,
                                               float nearDepth, float farDepth)
{
    const glm::vec3 forward = glm::normalize(camera.target - camera.position);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
    const glm::vec3 up = glm::cross(right, forward);
    const float tanHalfFov = std::tan(camera.verticalFovRadians * 0.5f);
    std::array<glm::vec3, 8> corners{};
    std::size_t corner = 0;
    for (const float depth : {nearDepth, farDepth})
    {
        const glm::vec3 center = camera.position + (forward * depth);
        const float halfHeight = depth * tanHalfFov;
        const float halfWidth = halfHeight * aspectRatio;
        for (const float x : {-1.0f, 1.0f})
        {
            for (const float y : {-1.0f, 1.0f})
            {
                corners[corner++] = center + (right * (x * halfWidth)) + (up * (y * halfHeight));
            }
        }
    }
    return corners;
}

Cascade cascadeOf(const std::array<glm::vec3, 8>& sliceCorners, float farDepth,
                  glm::vec3 sunDirection, std::uint32_t resolution)
{
    glm::vec3 center{0.0f};
    for (const glm::vec3& corner : sliceCorners)
    {
        center += corner / static_cast<float>(sliceCorners.size());
    }
    float radius = 0.0f;
    for (const glm::vec3& corner : sliceCorners)
    {
        radius = std::max(radius, glm::length(corner - center));
    }
    radius = roundedRadius(radius);

    // Le soleil regarde le centre de la sphère, depuis assez loin pour voir la marge des objets qui
    // projettent leur ombre dans la tranche.
    const glm::vec3 toSun = glm::normalize(sunDirection);
    const glm::vec3 up =
        std::abs(toSun.y) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::mat4 view = glm::lookAtRH(center + (toSun * (radius + CasterMargin)), center, up);
    glm::mat4 projection =
        glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, (2.0f * radius) + CasterMargin);

    // L'origine du monde, projetée, doit tomber sur un texel entier : la projection n'avance alors
    // que par texels, et les bords des ombres ne glissent pas d'une image à l'autre.
    const glm::vec4 origin = projection * view * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const float texelsPerUnit = static_cast<float>(resolution) * 0.5f;
    const glm::vec2 inTexels = glm::vec2{origin} * texelsPerUnit;
    const glm::vec2 offset = (glm::round(inTexels) - inTexels) / texelsPerUnit;
    projection[3][0] += offset.x;
    projection[3][1] += offset.y;

    return Cascade{.viewProjection = projection * view, .farDepth = farDepth};
}

std::array<Cascade, CascadeCount> cascadesOf(const Camera& camera, float aspectRatio,
                                             glm::vec3 sunDirection,
                                             const CascadeSettings& settings)
{
    const std::array<float, CascadeCount + 1> splits = cascadeSplitsOf(camera, settings);
    std::array<Cascade, CascadeCount> cascades{};
    for (std::uint32_t i = 0; i < CascadeCount; ++i)
    {
        cascades[i] =
            cascadeOf(frustumSliceCornersOf(camera, aspectRatio, splits[i], splits[i + 1]),
                      splits[i + 1], sunDirection, settings.resolution);
    }
    return cascades;
}

core::Result<ShadowPass> createShadowPass(nvrhi::IDevice& device, std::uint32_t resolution)
{
    auto vertexShader = loadShader(device, "shadow.vertexMain", nvrhi::ShaderType::Vertex);
    if (!vertexShader)
    {
        return std::unexpected(vertexShader.error());
    }
    // Les sommets de la passe des meshes : seule la position sert, puis la pose de l'instance.
    const std::array<nvrhi::VertexAttributeDesc, 3> attributes{
        nvrhi::VertexAttributeDesc()
            .setName("POSITION")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(MeshVertex, position))
            .setElementStride(sizeof(MeshVertex)),
        nvrhi::VertexAttributeDesc()
            .setName("INSTANCE_POSITION")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setBufferIndex(1)
            .setOffset(offsetof(InstancePose, position))
            .setElementStride(sizeof(InstancePose))
            .setIsInstanced(true),
        nvrhi::VertexAttributeDesc()
            .setName("INSTANCE_ROTATION")
            .setFormat(nvrhi::Format::RGBA32_FLOAT)
            .setBufferIndex(1)
            .setOffset(offsetof(InstancePose, rotation))
            .setElementStride(sizeof(InstancePose))
            .setIsInstanced(true),
    };
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Vertex;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)};

    ShadowPass pass;
    pass.resolution = resolution;
    pass.vertexShader = std::move(*vertexShader);
    pass.inputLayout =
        device.createInputLayout(attributes.data(), attributes.size(), pass.vertexShader);
    pass.layout = device.createBindingLayout(layoutDesc);
    // Un dessin par objet et par cascade : autant de versions que la passe des meshes en permet.
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(ShadowConstants))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(MaxMeshDrawsPerCommandList)
                                             .setDebugName("constantes des ombres"));
    // Dessiné par la passe, lu par l'éclairage : NVRHI place la transition (suivi des états).
    pass.atlas = device.createTexture(nvrhi::TextureDesc()
                                          .setWidth(2 * resolution)
                                          .setHeight(2 * resolution)
                                          .setFormat(ShadowFormat)
                                          .setIsRenderTarget(true)
                                          .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                          .setKeepInitialState(true)
                                          .setDebugName("atlas des ombres"));
    pass.framebuffer =
        device.createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(pass.atlas));
    pass.sampler =
        device.createSampler(nvrhi::SamplerDesc()
                                 .setAllFilters(true)
                                 .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
                                 .setReductionType(nvrhi::SamplerReductionType::Comparison));

    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.inputLayout = pass.inputLayout;
    pipelineDesc.VS = pass.vertexShader;
    pipelineDesc.addBindingLayout(pass.layout);
    pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
    pipelineDesc.renderState.depthStencilState.depthWriteEnable = true;
    pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
    // Les deux faces : un rideau ou un feuillage n'a qu'une face, mais une ombre. Le biais, selon
    // la pente vue du soleil, évite qu'une surface s'ombre elle-même par l'arrondi de sa profondeur
    // (l'« acné » des ombres).
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pipelineDesc.renderState.rasterState.slopeScaledDepthBias = 2.0f;
    // Non nul, même petit : sous Vulkan, NVRHI n'active le biais que si sa part constante l'est
    // (vulkan-graphics.cpp, setDepthBiasEnable), et la pente seule serait ignorée.
    pipelineDesc.renderState.rasterState.depthBias = 1;
    pass.pipeline =
        device.createGraphicsPipeline(pipelineDesc, pass.framebuffer->getFramebufferInfo());
    if (!pass.inputLayout || !pass.layout || !pass.constants || !pass.atlas || !pass.framebuffer ||
        !pass.sampler || !pass.pipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData, "passe d'ombres refusée par NVRHI");
    }
    pass.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants)),
        pass.layout);
    return pass;
}

glm::uvec2 atlasCellOf(std::uint32_t cascade)
{
    return {cascade % 2, cascade / 2};
}

void clearShadows(nvrhi::ICommandList& commandList, const ShadowPass& pass)
{
    commandList.clearDepthStencilTexture(pass.atlas, nvrhi::AllSubresources, true, 1.0f, false, 0);
}

void drawShadowCaster(nvrhi::ICommandList& commandList, const ShadowPass& pass,
                      std::uint32_t cascade, const Cascade& view, const Mesh& mesh,
                      const Instances& instances, const glm::mat4& model)
{
    const ShadowConstants constants{.viewProjection = view.viewProjection, .model = model};
    commandList.writeBuffer(pass.constants, &constants, sizeof(constants));

    // Le quart de l'atlas de la cascade.
    const glm::uvec2 cell = atlasCellOf(cascade) * pass.resolution;
    const auto size = static_cast<float>(pass.resolution);
    nvrhi::GraphicsState state;
    state.pipeline = pass.pipeline;
    state.framebuffer = pass.framebuffer;
    state.viewport.addViewportAndScissorRect(
        nvrhi::Viewport(static_cast<float>(cell.x), static_cast<float>(cell.x) + size,
                        static_cast<float>(cell.y), static_cast<float>(cell.y) + size, 0.0f, 1.0f));
    state.addBindingSet(pass.bindings);
    state.addVertexBuffer(
        nvrhi::VertexBufferBinding().setBuffer(mesh.vertexBuffer).setSlot(0).setOffset(0));
    state.addVertexBuffer(
        nvrhi::VertexBufferBinding().setBuffer(instances.poses).setSlot(1).setOffset(0));
    state.setIndexBuffer(nvrhi::IndexBufferBinding()
                             .setBuffer(mesh.indexBuffer)
                             .setFormat(nvrhi::Format::R32_UINT)
                             .setOffset(0));
    commandList.setGraphicsState(state);
    nvrhi::DrawArguments arguments;
    arguments.vertexCount = mesh.indexCount;
    arguments.instanceCount = instances.count;
    commandList.drawIndexed(arguments);
}

} // namespace levain::render
