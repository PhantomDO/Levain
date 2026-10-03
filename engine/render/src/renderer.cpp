#include "levain/render/renderer.hpp"

#include <utility>

#include "levain/core/log.hpp"

namespace levain::render
{

namespace
{

/// Les passes de `RendererPassNames`, par leur nom.
enum class TimedPass : std::uint8_t
{
    Clusters,
    Shadows,
    Opaque,
    Sky,
    Transparent,
    Tonemap,
};

/// Commence la mesure de `pass`, et compte celle d'une image précédente, lisible maintenant
/// (gpu_timer.hpp).
void beginPass(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, Renderer& renderer,
               TimedPass pass)
{
    const auto index = static_cast<std::size_t>(pass);
    if (const auto ms = beginGpuTimer(device, commandList, renderer.timers[index]))
    {
        renderer.passTimes[index].totalMs += *ms;
        ++renderer.passTimes[index].samples;
    }
}

void endPass(nvrhi::ICommandList& commandList, Renderer& renderer, TimedPass pass)
{
    endGpuTimer(commandList, renderer.timers[static_cast<std::size_t>(pass)]);
}

/// Appelle les fonctions d'une étape, et la chronomètre si elle en a.
void runTimedStage(nvrhi::IDevice& device, Renderer& renderer, RenderStage stage, TimedPass pass,
                   const StageContext& context)
{
    if (renderer.stages.entries[static_cast<std::size_t>(stage)].empty())
    {
        return;
    }
    beginPass(device, context.commandList, renderer, pass);
    runStage(renderer.stages, stage, context);
    endPass(context.commandList, renderer, pass);
}

} // namespace

nvrhi::FramebufferInfo sceneTargetInfo()
{
    return nvrhi::FramebufferInfo().addColorFormat(HdrFormat).setDepthFormat(DepthFormat);
}

core::Result<Renderer> createRenderer(nvrhi::IDevice& device, nvrhi::Format outputFormat,
                                      Environment environment, bool drawSky)
{
    auto clusters = createLightClusterPass(device);
    if (!clusters)
    {
        return std::unexpected(clusters.error());
    }
    const CascadeSettings cascadeSettings;
    auto shadows = createShadowPass(device, cascadeSettings.resolution);
    if (!shadows)
    {
        return std::unexpected(shadows.error());
    }
    auto frame = createFrameBindings(device, *clusters, *shadows, environment);
    if (!frame)
    {
        return std::unexpected(frame.error());
    }
    auto meshPass = createMeshPass(device, sceneTargetInfo(), *frame);
    if (!meshPass)
    {
        return std::unexpected(meshPass.error());
    }
    std::optional<SkyPass> sky;
    if (drawSky)
    {
        auto pass = createSkyPass(device, sceneTargetInfo(), environment);
        if (!pass)
        {
            return std::unexpected(pass.error());
        }
        sky = std::move(*pass);
    }
    auto tonemap = createTonemapPass(device, nvrhi::FramebufferInfo().addColorFormat(outputFormat));
    if (!tonemap)
    {
        return std::unexpected(tonemap.error());
    }
    Renderer renderer{.clusters = std::move(*clusters),
                      .cascadeSettings = cascadeSettings,
                      .shadows = std::move(*shadows),
                      .environment = std::move(environment),
                      .frame = std::move(*frame),
                      .meshPass = std::move(*meshPass),
                      .sky = std::move(sky),
                      .tonemap = std::move(*tonemap),
                      .hdr = {},
                      .depth = {},
                      .stages = {},
                      .timers = {},
                      .passTimes = {}};
    for (GpuTimer& timer : renderer.timers)
    {
        timer = createGpuTimer(device);
    }
    return renderer;
}

void renderFrame(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, Renderer& renderer,
                 const FrameView& view, nvrhi::ITexture& output)
{
    const nvrhi::TextureDesc& size = output.getDesc();
    nvrhi::ITexture* depth = ensureDepthTexture(device, renderer.depth, size.width, size.height);
    // La scène se dessine dans l'image HDR, où la lumière n'est pas coupée à 1 ; le tonemapping la
    // ramène ensuite dans la sortie (M5.2).
    nvrhi::ITexture* hdr =
        ensureHdrTarget(device, renderer.tonemap, renderer.hdr, size.width, size.height);
    // ponytail: framebuffers recréés à chaque image. C'est léger avec le rendu dynamique de Vulkan
    // 1.3 (NVRHI ne crée pas de VkFramebuffer) ; un cache par image si un profil le montre.
    const nvrhi::FramebufferHandle scene = device.createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(hdr).setDepthAttachment(depth));
    const nvrhi::FramebufferHandle screen =
        device.createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(&output));

    const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);
    const glm::mat4 viewProjection = viewProjectionOf(view.camera, aspect);
    const FrameLighting lighting{
        .view = clusterViewOf(view.camera, aspect),
        .cameraPosition = view.camera.position,
        .sun = view.sun,
        .environmentIntensity = view.environmentIntensity,
        .cascades = cascadesOf(view.camera, aspect, view.sun.direction, renderer.cascadeSettings)};

    commandList.clearTextureFloat(
        hdr, nvrhi::AllSubresources,
        nvrhi::Color{view.background.r, view.background.g, view.background.b, view.background.a});
    // 1 : la profondeur la plus lointaine, que tout ce qu'on dessine vient remplacer.
    commandList.clearDepthStencilTexture(depth, nvrhi::AllSubresources, true, 1.0f, false, 0);

    // Les lumières ponctuelles triées par cluster, puis l'éclairage de l'image, avant les dessins
    // qui les lisent (ADR-0024).
    beginPass(device, commandList, renderer, TimedPass::Clusters);
    if (auto assigned =
            assignLightsToClusters(commandList, renderer.clusters, view.lights, lighting.view);
        !assigned)
    {
        core::log("render", core::LogLevel::Error, "{}", assigned.error().message);
    }
    endPass(commandList, renderer, TimedPass::Clusters);
    setFrameLighting(commandList, renderer.frame, renderer.clusters, renderer.shadows, lighting);

    // Les ombres : chaque cascade, vue du soleil (M5.3), même sans rien d'inscrit, pour que l'atlas
    // soit effacé.
    beginPass(device, commandList, renderer, TimedPass::Shadows);
    clearShadows(commandList, renderer.shadows);
    for (std::uint32_t cascade = 0; cascade < CascadeCount; ++cascade)
    {
        const Cascade& cascadeView = lighting.cascades[cascade];
        runStage(renderer.stages, RenderStage::ShadowCasters,
                 StageContext{.device = device,
                              .commandList = commandList,
                              .target = *renderer.shadows.framebuffer,
                              .frame = renderer.frame,
                              .viewProjection = cascadeView.viewProjection,
                              .frustum = frustumOf(cascadeView.viewProjection),
                              .shadows = renderer.shadows,
                              .cascade = cascade,
                              .cascadeView = &cascadeView,
                              .seconds = view.seconds});
    }
    endPass(commandList, renderer, TimedPass::Shadows);

    const StageContext sceneContext{.device = device,
                                    .commandList = commandList,
                                    .target = *scene,
                                    .frame = renderer.frame,
                                    .viewProjection = viewProjection,
                                    .frustum = frustumOf(viewProjection),
                                    .shadows = renderer.shadows,
                                    .cascade = 0,
                                    .cascadeView = nullptr,
                                    .seconds = view.seconds};
    runTimedStage(device, renderer, RenderStage::Opaque, TimedPass::Opaque, sceneContext);
    if (renderer.sky)
    {
        beginPass(device, commandList, renderer, TimedPass::Sky);
        drawSky(commandList, *renderer.sky, *scene, view.camera, aspect, view.environmentIntensity);
        endPass(commandList, renderer, TimedPass::Sky);
    }
    runTimedStage(device, renderer, RenderStage::Transparent, TimedPass::Transparent, sceneContext);

    beginPass(device, commandList, renderer, TimedPass::Tonemap);
    tonemap(commandList, renderer.tonemap, renderer.hdr, *screen, view.tonemap);
    endPass(commandList, renderer, TimedPass::Tonemap);
}

} // namespace levain::render
