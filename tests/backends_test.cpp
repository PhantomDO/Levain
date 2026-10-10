#include <memory>
#include <string>

#include <doctest/doctest.h>

#include "levain/core/error.hpp"
#include "levain/gpu/device.hpp"
#include "levain/platform/window.hpp"

// Le choix du backend (`--gpu`) se lit et se refuse pareil sur toutes les cibles, navigateur
// compris : ce fichier tourne aussi en WebAssembly sous Node (ADR-0023). Il n'ouvre ni fenêtre ni
// GPU.

using levain::core::ErrorCode;
using levain::gpu::Adapter;
using levain::gpu::DefaultBackend;
using levain::gpu::requireAdapterChoosable;
using levain::gpu::requireBackendBuilt;

TEST_CASE("sans --gpu, le backend est celui de la cible, et la cible le construit")
{
#ifdef __EMSCRIPTEN__
    CHECK(DefaultBackend == nvrhi::GraphicsAPI::WEBGPU);
#else
    CHECK(DefaultBackend == nvrhi::GraphicsAPI::VULKAN);
#endif
    CHECK(levain::gpu::DeviceOptions{}.api == DefaultBackend);
    // Un défaut refusé ferait échouer tout lancement sans option.
    CHECK(requireBackendBuilt(DefaultBackend).has_value());
}

TEST_CASE("un backend que la cible ne construit pas est refusé, en disant pourquoi")
{
    CHECK(requireBackendBuilt(nvrhi::GraphicsAPI::WEBGPU).has_value());
    const auto d3d11 = requireBackendBuilt(nvrhi::GraphicsAPI::D3D11);
    REQUIRE_FALSE(d3d11.has_value());
    CHECK(d3d11.error().code == ErrorCode::Unsupported);
#ifdef __EMSCRIPTEN__
    // Le navigateur n'a que WebGPU : Vulkan et Direct3D 12 se lisent (`--gpu`), puis se refusent.
    for (const nvrhi::GraphicsAPI api : {nvrhi::GraphicsAPI::VULKAN, nvrhi::GraphicsAPI::D3D12})
    {
        const auto refused = requireBackendBuilt(api);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message.contains("le seul backend est WebGPU"));
    }
#endif
}

TEST_CASE(
    "le rendu logiciel (WARP) ne se choisit que sous Direct3D 12, et les autres refus le disent")
{
    // Par défaut, le GPU matériel : tout lancement sans option reste accepté.
    CHECK(levain::gpu::DeviceOptions{}.adapter == Adapter::HighPerformance);
    for (const nvrhi::GraphicsAPI api :
         {nvrhi::GraphicsAPI::VULKAN, nvrhi::GraphicsAPI::D3D12, nvrhi::GraphicsAPI::WEBGPU})
    {
        CAPTURE(static_cast<int>(api));
        CHECK(requireAdapterChoosable(api, Adapter::HighPerformance).has_value());
    }
    // Direct3D 12 l'accepte sur toutes les cibles : c'est requireBackendBuilt qui refuse le backend
    // hors de Windows, avec son propre message.
    CHECK(requireAdapterChoosable(nvrhi::GraphicsAPI::D3D12, Adapter::Software).has_value());
    // Sous Vulkan et WebGPU, le pilote n'est pas au moteur : le refus dit qui le choisit.
    for (const nvrhi::GraphicsAPI api : {nvrhi::GraphicsAPI::VULKAN, nvrhi::GraphicsAPI::WEBGPU})
    {
        CAPTURE(static_cast<int>(api));
        const auto refused = requireAdapterChoosable(api, Adapter::Software);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::Unsupported);
        CHECK(refused.error().message.contains("n'existe que sous Direct3D 12"));
        CHECK(refused.error().message.contains("VK_DRIVER_FILES"));
    }
}

namespace
{

/// Ce que le callback de `requestGpuDevice` a reçu. Partagé, pour qu'une réponse qui arriverait
/// après la fin du test (un navigateur, où le device arrive plus tard) n'écrive pas dans une pile
/// disparue.
struct Answer
{
    bool called = false;
    bool succeeded = false;
    ErrorCode code = ErrorCode::InvalidData;
    std::string message;
};

std::shared_ptr<const Answer> requestAndKeepAnswer(nvrhi::GraphicsAPI api)
{
    // Une fenêtre vide : le refus ne la lit pas.
    const levain::platform::Window noWindow;
    auto answer = std::make_shared<Answer>();
    levain::gpu::requestGpuDevice(noWindow, {.api = api},
                                  [answer](levain::core::Result<levain::gpu::GpuDevice> device)
                                  {
                                      answer->called = true;
                                      answer->succeeded = device.has_value();
                                      if (!device)
                                      {
                                          answer->code = device.error().code;
                                          answer->message = device.error().message;
                                      }
                                  });
    return answer;
}

} // namespace

TEST_CASE("requestGpuDevice rend le refus par son callback, avant de revenir et sans fenêtre")
{
    // Dans le navigateur, c'est `?args=--gpu d3d12` de la page, qui lançait WebGPU sans un mot
    // quand le refus n'était pas demandé.
#ifdef __EMSCRIPTEN__
    const auto missing = {nvrhi::GraphicsAPI::VULKAN, nvrhi::GraphicsAPI::D3D12};
#else
    // Direct3D 12 existe dans un build Windows : D3D11, lui, n'est construit nulle part.
    const auto missing = {nvrhi::GraphicsAPI::D3D11};
#endif
    for (const nvrhi::GraphicsAPI api : missing)
    {
        CAPTURE(static_cast<int>(api));
        const std::shared_ptr<const Answer> answer = requestAndKeepAnswer(api);
        CHECK(answer->called);
        CHECK_FALSE(answer->succeeded);
        // Le message de `requireBackendBuilt`, pas un autre échec Unsupported : sous Node, WebGPU
        // lui-même échoue ainsi, sans navigateur, et masquerait un refus qui n'aurait pas eu lieu.
        CHECK(answer->code == ErrorCode::Unsupported);
        CHECK(answer->message == requireBackendBuilt(api).error().message);
    }
}
