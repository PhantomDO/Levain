#include "levain/app/scene_view.hpp"

#include <algorithm>
#include <utility>

#include "levain/core/log.hpp"
#include "levain/gpu/device.hpp"

namespace levain::app
{

namespace
{

/// La taille d'une texture, ou rien pour une texture nulle.
SceneSize sizeOf(const nvrhi::TextureHandle& texture)
{
    return texture
               ? SceneSize{.width = texture->getDesc().width, .height = texture->getDesc().height}
               : SceneSize{};
}

/// Remplace la texture de la scène par une neuve à `size`, et retire l'ancienne : son identifiant
/// attend la fin de l'image (`releaseRetiredScenes`). Si la neuve ne se crée pas ou que l'UI la
/// refuse, l'ancienne reste, et l'échec est journalisé (règle n°7) : la Vue montrera l'image
/// d'avant plutôt que rien.
void replaceSceneTexture(SceneView& view, nvrhi::IDevice& device, ui::UiPass& pass,
                         nvrhi::Format format, const SceneSize& size)
{
    nvrhi::TextureHandle fresh = createSceneTexture(device, format, size);
    if (!fresh)
    {
        core::log("app", core::LogLevel::Error, "la scène de la Vue ({} × {}) : texture refusée",
                  size.width, size.height);
        return;
    }
    const auto id = ui::registerUiTexture(device, pass, fresh);
    if (!id)
    {
        core::log("app", core::LogLevel::Error, "la scène de la Vue ({} × {}) : {}", size.width,
                  size.height, id.error().message);
        return;
    }
    if (view.imageId != 0)
    {
        view.retired.push_back(view.imageId);
    }
    view.texture = std::move(fresh);
    view.imageId = *id;
    ++view.created;
}

} // namespace

bool shouldRecreateScene(SizeSettling& settling, const SceneSize& current,
                         const SceneSize& requested, int stableFrames)
{
    settling.framesAtLast =
        requested == settling.last ? std::min(settling.framesAtLast + 1, stableFrames) : 1;
    settling.last = requested;
    if (requested.empty() || requested == current)
    {
        return false;
    }
    return current.empty() || settling.framesAtLast >= stableFrames;
}

nvrhi::TextureHandle createSceneTexture(nvrhi::IDevice& device, nvrhi::Format format,
                                        const SceneSize& size)
{
    // La valeur d'effacement est défensive, comme le veut l'ADR-0036 : rien n'efface aujourd'hui
    // cette texture (le tonemapping la réécrit en entier), mais Direct3D 12 veut connaître la
    // valeur d'une cible à sa création, et sa couche de debug avertit sinon à chaque effacement
    // qu'un morceau futur y ferait (D3D12_MESSAGE_ID 820, comme `ensureHdrTarget`). Le noir opaque
    // : la sortie du tonemapping l'est aussi (alpha 1).
    return device.createTexture(nvrhi::TextureDesc()
                                    .setWidth(size.width)
                                    .setHeight(size.height)
                                    .setFormat(format)
                                    .setIsRenderTarget(true)
                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                    .setKeepInitialState(true)
                                    .setClearValue(nvrhi::Color{0.0f, 0.0f, 0.0f, 1.0f})
                                    .setDebugName("scène de la Vue"));
}

std::optional<ImTextureID> requestSceneTexture(SceneView& view, nvrhi::IDevice& device,
                                               ui::UiPass& pass, nvrhi::Format format,
                                               const SceneSize& size)
{
    view.requested = size;
    if (shouldRecreateScene(view.settling, sizeOf(view.texture), size, StableFramesBeforeResize))
    {
        replaceSceneTexture(view, device, pass, format, size);
    }
    if (size.empty() || view.imageId == 0)
    {
        return std::nullopt;
    }
    return view.imageId;
}

void beginSceneRequest(SceneView& view)
{
    view.requested.reset();
}

void settleSceneOutput(SceneView& view)
{
    view.output = sceneOutputFor(view.requested, view.imageId != 0);
}

void releaseRetiredScenes(SceneView& view, ui::UiPass& pass)
{
    for (const ImTextureID id : view.retired)
    {
        ui::releaseUiTexture(pass, id);
    }
    view.retired.clear();
}

void clearSwapchain(nvrhi::ICommandList& commandList, nvrhi::ITexture& swapchainImage)
{
    commandList.clearTextureFloat(
        &swapchainImage, nvrhi::AllSubresources,
        nvrhi::Color{gpu::PresentClearLevel, gpu::PresentClearLevel, gpu::PresentClearLevel, 1.0f});
}

SceneExtent sceneExtentOf(SceneOutput output, const ScreenRect& scene, float windowWidth,
                          float windowHeight)
{
    if (output == SceneOutput::Window)
    {
        return windowWidth > 0.0f && windowHeight > 0.0f ? SceneExtent{windowWidth, windowHeight}
                                                         : SceneExtent{};
    }
    return output == SceneOutput::Nothing || scene.width <= 0.0f || scene.height <= 0.0f
               ? SceneExtent{}
               : SceneExtent{scene.width, scene.height};
}

std::optional<ScenePixel> scenePixelAt(SceneOutput output, const ScreenRect& scene,
                                       float windowWidth, float windowHeight, float cursorX,
                                       float cursorY)
{
    if (output == SceneOutput::Window)
    {
        return windowWidth > 0.0f && windowHeight > 0.0f
                   ? std::optional<ScenePixel>{{cursorX, cursorY, windowWidth, windowHeight}}
                   : std::nullopt;
    }
    if (output == SceneOutput::Nothing || scene.width <= 0.0f || scene.height <= 0.0f ||
        !rectContains(scene, cursorX, cursorY))
    {
        return std::nullopt;
    }
    return ScenePixel{cursorX - scene.x, cursorY - scene.y, scene.width, scene.height};
}

} // namespace levain::app
