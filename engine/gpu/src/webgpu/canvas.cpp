// La surface d'un canvas HTML (ADR-0023) : navigateur seulement.

#include <cstdint>
#include <string>
#include <string_view>

#include "backend.hpp"
#include "conversions.hpp"

#include "levain/gpu/webgpu.hpp"

namespace levain::gpu
{

struct WebGpuCanvas
{
    wgpu::Surface surface;
    wgpu::Device device;
    wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
    nvrhi::Format format = nvrhi::Format::UNKNOWN; ///< La vue sRGB de `surfaceFormat`.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    nvrhi::TextureHandle current; ///< L'image de la frame en cours.
};

void WebGpuCanvasDeleter::operator()(WebGpuCanvas* canvas) const noexcept
{
    delete canvas;
}

core::Result<WebGpuCanvasHandle> createWebGpuCanvas(nvrhi::IDevice& device,
                                                    std::string_view selector, bool srgb)
{
    // Les objets WebGPU sous le device NVRHI, validation comprise (getNativeObject).
    const wgpu::Instance instance{
        static_cast<WGPUInstance>(device.getNativeObject(webgpu::object_types::Instance))};
    const wgpu::Adapter adapter{
        static_cast<WGPUAdapter>(device.getNativeObject(webgpu::object_types::Adapter))};
    if (!instance || !adapter)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "canvas : le device n'est pas un device WebGPU");
    }
    const std::string text{selector};
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector source{};
    source.selector = {text.data(), text.size()};
    wgpu::SurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &source;

    WebGpuCanvasHandle canvas{new WebGpuCanvas{}};
    canvas->surface = instance.CreateSurface(&surfaceDesc);
    canvas->device =
        wgpu::Device{static_cast<WGPUDevice>(device.getNativeObject(webgpu::object_types::Device))};

    // Le format que préfère le navigateur (BGRA8 sur PC, parfois RGBA8 sur mobile), vu en sRGB.
    wgpu::SurfaceCapabilities capabilities;
    canvas->surface.GetCapabilities(adapter, &capabilities);
    canvas->surfaceFormat =
        capabilities.formatCount > 0 ? capabilities.formats[0] : wgpu::TextureFormat::BGRA8Unorm;
    const bool rgba = canvas->surfaceFormat == wgpu::TextureFormat::RGBA8Unorm;
    if (srgb)
    {
        canvas->format = rgba ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::SBGRA8_UNORM;
    }
    else
    {
        canvas->format = rgba ? nvrhi::Format::RGBA8_UNORM : nvrhi::Format::BGRA8_UNORM;
    }
    return canvas;
}

nvrhi::Format canvasFormat(const WebGpuCanvas& canvas)
{
    return canvas.format;
}

nvrhi::ITexture* beginCanvasFrame(WebGpuCanvas& canvas, std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return nullptr;
    }
    if (width != canvas.width || height != canvas.height)
    {
        // Le canvas n'est jamais en sRGB : on le configure avec une vue sRGB (viewFormats), où
        // le moteur dessine, pour que les couleurs soient celles de la swapchain Vulkan.
        const wgpu::TextureFormat view =
            webgpu::textureFormatOf(canvas.format).value_or(canvas.surfaceFormat);
        wgpu::SurfaceConfiguration config{};
        config.device = canvas.device;
        config.format = canvas.surfaceFormat;
        config.usage = wgpu::TextureUsage::RenderAttachment;
        config.viewFormatCount = 1;
        config.viewFormats = &view;
        config.width = width;
        config.height = height;
        config.presentMode = wgpu::PresentMode::Fifo;
        config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
        canvas.surface.Configure(&config);
        canvas.width = width;
        canvas.height = height;
    }
    wgpu::SurfaceTexture surfaceTexture;
    canvas.surface.GetCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        return nullptr;
    }
    canvas.current = nvrhi::TextureHandle::Create(new webgpu::Texture{nvrhi::TextureDesc()
                                                                          .setWidth(width)
                                                                          .setHeight(height)
                                                                          .setFormat(canvas.format)
                                                                          .setIsRenderTarget(true)
                                                                          .setDebugName("canvas"),
                                                                      surfaceTexture.texture});
    return canvas.current;
}

} // namespace levain::gpu
