// Sonde : compilée sous Linux par clang-cl, lancée sous Windows. Crée un device D3D12 sur le GPU,
// avec la couche de debug si elle est installée, et affiche ce qu'elle trouve.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <print>
#include <string>

using Microsoft::WRL::ComPtr;

int main()
{
    std::println("__cplusplus = {}, _MSVC_LANG = {}, clang {}", __cplusplus, _MSVC_LANG, __clang_version__);

    ComPtr<ID3D12Debug> debug;
    const bool hasDebugLayer = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if (hasDebugLayer)
    {
        debug->EnableDebugLayer();
    }
    std::println("couche de debug D3D12 : {}", hasDebugLayer ? "présente" : "ABSENTE (Graphics Tools)");

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
    {
        std::println("CreateDXGIFactory2 a échoué");
        return 1;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                         IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        std::wstring wname{desc.Description};
        std::string name(wname.begin(), wname.end());

        ComPtr<ID3D12Device> device;
        const bool ok = SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
        std::println("adaptateur {} : {} ({} Mo), device D3D12 12_0 : {}", i, name,
                     desc.DedicatedVideoMemory / (1024 * 1024), ok ? "OK" : "non");
        if (ok)
        {
            D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_8};
            device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
            std::println("  shader model max : {:#x}", static_cast<int>(shaderModel.HighestShaderModel));
        }
    }
    return 0;
}
