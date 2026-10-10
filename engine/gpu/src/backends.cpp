// Les noms des backends et ceux que ce build contient : compilé partout, navigateur compris, pour
// que la lecture de `--gpu` (engine/app) soit la même sur toutes les cibles.

#include <optional>
#include <string_view>

#include "levain/core/error.hpp"
#include "levain/gpu/device.hpp"

namespace levain::gpu
{

std::optional<nvrhi::GraphicsAPI> graphicsApiNamed(std::string_view name)
{
    if (name == "vulkan")
    {
        return nvrhi::GraphicsAPI::VULKAN;
    }
    if (name == "d3d12")
    {
        return nvrhi::GraphicsAPI::D3D12;
    }
    if (name == "webgpu")
    {
        return nvrhi::GraphicsAPI::WEBGPU;
    }
    return std::nullopt;
}

platform::GraphicsSurface surfaceFor(nvrhi::GraphicsAPI api)
{
    return api == nvrhi::GraphicsAPI::VULKAN ? platform::GraphicsSurface::Vulkan
                                             : platform::GraphicsSurface::None;
}

namespace
{

#ifdef __EMSCRIPTEN__
constexpr bool HasNativeBackends = false;
#else
constexpr bool HasNativeBackends = true;
#endif

#ifdef _WIN32
constexpr bool IsWindows = true;
#else
constexpr bool IsWindows = false;
#endif

} // namespace

core::Result<void> requireBackendBuilt(nvrhi::GraphicsAPI api)
{
    if (api == nvrhi::GraphicsAPI::WEBGPU)
    {
        return {};
    }
    if (!HasNativeBackends)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "dans le navigateur, le seul backend est WebGPU (ADR-0023)");
    }
    if (api == nvrhi::GraphicsAPI::VULKAN)
    {
        return {};
    }
    if (api != nvrhi::GraphicsAPI::D3D12)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "backend inconnu du moteur : vulkan, d3d12 ou webgpu");
    }
    if (!IsWindows)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "Direct3D 12 n'existe que dans un build Windows (preset "
                               "windows-debug ou windows-release, ADR-0035) : ici, --gpu vulkan "
                               "ou webgpu");
    }
    return {};
}

core::Result<void> requireAdapterChoosable(nvrhi::GraphicsAPI api, Adapter adapter)
{
    if (adapter == Adapter::HighPerformance || api == nvrhi::GraphicsAPI::D3D12)
    {
        return {};
    }
    return core::makeError(
        core::ErrorCode::Unsupported,
        "le rendu logiciel (WARP) n'existe que sous Direct3D 12 : sous Vulkan, le "
        "chargeur choisit le pilote (VK_DRIVER_FILES pour lavapipe), et sous "
        "WebGPU, Dawn ou le navigateur choisit l'adaptateur");
}

} // namespace levain::gpu
