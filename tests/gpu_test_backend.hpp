#pragma once

#include <optional>
#include <string_view>

#include <nvrhi/nvrhi.h>

#include "levain/gpu/device.hpp"

// Le backend que nomme la ligne de commande des programmes GPU de tests (smoke_render,
// light_clusters, environment, ui_gpu, app_script) : le même vocabulaire pour les cinq, au lieu de
// cinq lectures de `--gpu`.

namespace levain::tests
{

/// Un backend et l'adaptateur où il dessine.
struct TestBackend
{
    nvrhi::GraphicsAPI api;
    gpu::Adapter adapter;
};

/// « vulkan », « d3d12 » (le GPU), « d3d12-warp » (le rendu logiciel de Windows : le runner de la
/// CI n'a pas de GPU, #19) ou « webgpu ». Vide pour un autre nom.
[[nodiscard]] inline std::optional<TestBackend> testBackendNamed(std::string_view name)
{
    if (name == "d3d12-warp")
    {
        return TestBackend{.api = nvrhi::GraphicsAPI::D3D12, .adapter = gpu::Adapter::Software};
    }
    if (const auto api = gpu::graphicsApiNamed(name))
    {
        return TestBackend{.api = *api, .adapter = gpu::Adapter::HighPerformance};
    }
    return std::nullopt;
}

/// Les options du device d'un test : la validation toujours exigée (règle n°4).
[[nodiscard]] inline gpu::DeviceOptions testDeviceOptions(const TestBackend& backend)
{
    return {.enableValidation = true, .api = backend.api, .adapter = backend.adapter};
}

} // namespace levain::tests
