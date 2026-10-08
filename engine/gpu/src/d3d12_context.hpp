#pragma once

#include <directx/d3d12.h>
#include <directx/d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <nvrhi/nvrhi.h>

#include "native_device.hpp"

// En-tête privé du module, jamais installé. Windows seulement. Direct3D 12 par <directx/d3d12.h>,
// de DirectX-Headers, celui que NVRHI inclut (<nvrhi/d3d12.h>) : le <d3d12.h> du SDK, mêlé à lui,
// le redéfinirait. DXGI vient du SDK.

namespace levain::gpu
{

/// L'IID d'une interface COM, dont le pointeur de type `T` reçoit l'objet. Le `IID_PPV_ARGS` de
/// Microsoft le lit par `__uuidof`, une extension que `-pedantic-errors` refuse (CMakeLists.txt
/// racine, règle n°4) : on nomme les IID de dxguid.lib (DXGI) et de DirectX-Guids (Direct3D 12),
/// une spécialisation par interface dont le module se sert. Une autre ne compile pas.
template <typename T> const IID& interfaceId() = delete;

template <> inline const IID& interfaceId<ID3D12Debug>()
{
    return IID_ID3D12Debug;
}

template <> inline const IID& interfaceId<ID3D12Device>()
{
    return IID_ID3D12Device;
}

template <> inline const IID& interfaceId<ID3D12InfoQueue1>()
{
    return IID_ID3D12InfoQueue1;
}

template <> inline const IID& interfaceId<ID3D12CommandQueue>()
{
    return IID_ID3D12CommandQueue;
}

template <> inline const IID& interfaceId<IDXGIFactory6>()
{
    return IID_IDXGIFactory6;
}

template <> inline const IID& interfaceId<IDXGIAdapter1>()
{
    return IID_IDXGIAdapter1;
}

/// Le pendant de `IID_PPV_ARGS(&pointer)`, en deux appels sur le même pointeur, d'où ils tirent
/// le même type : `D3D12GetDebugInterface(iidOf(debug), outPointer(debug))`.
template <typename T> const IID& iidOf(const nvrhi::RefCountPtr<T>& /*pointer*/)
{
    return interfaceId<T>();
}

/// Le pointeur à remplir, vidé d'abord par `ReleaseAndGetAddressOf` : le `&` de
/// nvrhi::RefCountPtr, à la différence de celui de ComPtr, rend l'adresse sans relâcher l'objet
/// tenu, que l'appel écraserait et ferait fuir.
template <typename T> void** outPointer(nvrhi::RefCountPtr<T>& pointer)
{
    return reinterpret_cast<void**>(pointer.ReleaseAndGetAddressOf());
}

/// Le NativeDevice de Direct3D 12 : ce que NVRHI ne crée pas, la factory DXGI, l'adaptateur, le
/// device et sa queue. `nvrhi::RefCountPtr` tient les objets COM comme un `ComPtr`.
struct D3d12Context final : NativeDevice
{
    /// Désinscrit le moteur de la couche de debug, puis relâche la queue, le device, l'adaptateur
    /// et la factory, dans cet ordre (l'ordre inverse des membres).
    ~D3d12Context() override;

    /// Celle qui a trouvé l'adaptateur, gardée pour la swapchain DXGI, qui se crée par elle.
    nvrhi::RefCountPtr<IDXGIFactory6> factory;
    nvrhi::RefCountPtr<IDXGIAdapter1> adapter;
    nvrhi::RefCountPtr<ID3D12Device> device;
    nvrhi::RefCountPtr<ID3D12CommandQueue> queue;
    /// Avec la validation seulement : la file de la couche de debug, qui rappelle le moteur à
    /// chaque message (`messageCookie`).
    nvrhi::RefCountPtr<ID3D12InfoQueue1> infoQueue;
    DWORD messageCookie = 0;
};

} // namespace levain::gpu
