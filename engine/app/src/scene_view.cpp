#include "levain/app/scene_view.hpp"

#include <algorithm>

#include "levain/gpu/device.hpp"

namespace levain::app
{

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

} // namespace levain::app
