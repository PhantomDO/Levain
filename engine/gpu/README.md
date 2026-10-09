# `engine/gpu`

## Rôle

Donner un GPU au moteur : instance, surface et device Vulkan (ou, sous Windows, device Direct3D 12), puis le
device NVRHI par-dessus. C'est, avec `render/`, le seul module qui voit NVRHI, et avec `platform/`, le seul qui
inclut SDL, pour créer la surface (SPECS §7).

**État en M1.2** : device Vulkan et device NVRHI, validation redirigée vers nos logs (#12) ; swapchain
reconstruite au redimensionnement, frames cadencées par l'écran (#13).

**État en M4.6** : un second backend de NVRHI, **WebGPU** (ADR-0023, `src/webgpu/`), écrit dans le dépôt : dans
le navigateur par emdawnwebgpu, en natif sur Dawn. En natif, il ne sert qu'à développer et vérifier le backend
(`levain_sandbox --gpu webgpu`, rendu hors écran, `--capture` pour voir l'image) : le moteur reste sur Vulkan.
Il reproduit les images de Vulkan (test de fumée à 0 pixel près, renard et Sponza).

**État en M1.4** : le backend **Direct3D 12** (#18, ADR-0035), dans les builds Windows seulement, choisi au
lancement (`levain_sandbox --gpu d3d12`) ; Vulkan reste le défaut. Pas encore de swapchain DXGI : comme WebGPU en
natif, il dessine dans l'image hors écran de `GpuDevice`, la fenêtre reste vide, `--capture` montre l'image. Sur la
RTX 4070 de Donnovan, en Debug, sans un message de la couche de debug : les cinq tests de fumée à 0 pixel près, les
trois tests GPU (`levain_light_clusters.exe d3d12`, `levain_environment.exe d3d12`, `levain_ui_gpu.exe d3d12`), et
le sandbox avec `--capture`. Ils ne sont pas déclarés à ctest en `d3d12` : un runner sans GPU n'a que WARP, que
choisira #19, avec deux prérequis relevés ici :

- la fenêtre est créée avec `SDL_WINDOW_VULKAN` (`engine/platform/src/window.cpp`), qui charge `vulkan-1.dll` même
  en `--gpu d3d12` : sans chargeur Vulkan, Direct3D 12 ne se lancerait pas ;
- en Debug, Direct3D 12 exige `ID3D12InfoQueue1` (Windows 11, Windows Server 2025) et la fonctionnalité facultative
  « Outils graphiques » de Windows (`d3d12SDKLayers.dll`) : sans elles, le Debug refuse de démarrer (règle n°7).

## Invariants

1. **Aucune API native dans l'API du module.** `device.hpp` n'expose que NVRHI et `platform::Window` ;
   `NativeDevice` et `Swapchain` n'y sont que déclarés. Chaque backend natif en dérive dans ses fichiers
   (`VulkanContext` et `VulkanSwapchain`, `D3d12Context`) : les backends vivent ensemble dans l'exe Windows, où un
   même nom ne peut avoir qu'une définition. vk-bootstrap, les en-têtes Vulkan, Direct3D 12 et DXGI, et SDL sont
   liés en `PRIVATE`.
2. **L'ordre de destruction est écrit dans l'ordre des déclarations.** Dans `GpuDevice` : `swapchain`, dont les
   images sont des textures NVRHI, puis `nvrhi`, puis `native`. Dans le sandbox, le `GpuDevice` est déclaré après
   la fenêtre, pour que la surface disparaisse avant la fenêtre SDL qui la porte.
3. **En Debug, toute erreur de validation arrête le programme** sur une assertion, qu'elle vienne des couches
   Vulkan, de la couche de debug Direct3D 12 (erreur ou corruption) ou de NVRHI (règle n°4). La couche de debug est
   exigée comme les couches Vulkan : sans elle, ou sans `ID3D12InfoQueue1` (Windows 11), qui la fait rappeler le
   moteur, le device refuse de se créer (règle n°7) ; les messages qu'elle a gardés avant que le moteur ne
   s'inscrive passent par le même chemin. Les messages du *loader* Vulkan, qui signale par exemple une couche tierce
   cassée (SPECS §10), sont journalisés sans arrêter : ce ne sont pas des bugs du moteur.
4. **Le dispatcher de Vulkan-Hpp est défini une seule fois**, dans `device_vk.cpp`.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/gpu/device.hpp`](include/levain/gpu/device.hpp) | `createGpuDevice`, `GpuDevice`, `DeviceOptions`, `swapchainFormat`, `beginFrame`, `presentFrame` ; `graphicsApiNamed` (`--gpu vulkan\|d3d12\|webgpu`), `DefaultBackend` (Vulkan en natif, WebGPU dans le navigateur) et `requireBackendBuilt`, qui refuse en le disant Direct3D 12 hors de Windows et tout sauf WebGPU dans le navigateur, où `requestGpuDevice` l'appelle aussi |
| [`src/device.cpp`](src/device.cpp), [`src/native_device.hpp`](src/native_device.hpp) | Le choix du backend au lancement, la frame hors écran de WebGPU, la cadence des frames (`limitFramesInFlight`) ; les interfaces `NativeDevice` et `Swapchain` que chaque backend implémente |
| [`src/device_d3d12.cpp`](src/device_d3d12.cpp), [`src/d3d12_context.hpp`](src/d3d12_context.hpp) | Direct3D 12, Windows seulement : couche de debug, factory DXGI, adaptateur le plus performant, device, queue, puis `nvrhi::d3d12::createDevice`. Adapté de Donut (`DeviceManager_DX12.cpp`, MIT) |
| [`include/levain/gpu/webgpu.hpp`](include/levain/gpu/webgpu.hpp) | `requestWebGpuDevice` (asynchrone dans le navigateur), `createWebGpuDevice` (natif), le canvas HTML (web) |
| [`src/webgpu/`](src/webgpu/) | Le backend : `device.cpp` (ressources), `bindings.cpp`, `pipelines.cpp`, `commandlist.cpp`, `canvas.cpp` |

## Ce que NVRHI fait pour nous, et ce qu'il ne fait pas

- **Il ne crée ni l'instance ni le device** : on les lui fournit, avec vk-bootstrap
  ([ADR-0012](../../docs/adr/0012-vk-bootstrap.md)). Il exige Vulkan 1.3 avec `dynamicRendering`,
  `synchronization2` et les timeline semaphores, et on ne lui annonce que les extensions réellement activées.
  S'y ajoutent `shaderDrawParameters`, pour `SV_VertexID` dans les shaders Slang (ADR-0005, amendement), et
  `samplerAnisotropy`, pour le filtrage anisotrope (M2.2).
- **Il ne nomme les objets Vulkan que si on le lui permet** : les `debugName` n'arrivent jusqu'au pilote, donc
  jusqu'à RenderDoc et aux messages de validation, que si `VK_EXT_debug_utils` figure dans ses extensions
  d'instance. On la lui annonce quand vk-bootstrap l'a activée, avec la validation.
- **Une fois créé, il prend en charge le plus fastidieux** : allocation de la mémoire, barrières de
  synchronisation (il suit l'état de chaque ressource), destruction différée des ressources encore utilisées par
  le GPU, envois de données. On le verra à l'œuvre avec le premier triangle (M1.3).
- **Deux validations complémentaires** : les couches Vulkan vérifient ce que NVRHI envoie au pilote ; la couche de
  validation de NVRHI, qui enveloppe le device, vérifie l'usage qu'on fait de NVRHI lui-même.
- **Sous Direct3D 12, même partage** : NVRHI ne crée ni la factory DXGI, ni le device, ni la queue, ni la
  swapchain ; on lui donne le device et la queue (`nvrhi::d3d12::DeviceDesc`), et il crée le reste (tas de
  descripteurs, root signatures, barrières). Ce qu'il y a à écrire est bien plus court qu'en Vulkan : ni instance,
  ni extensions, ni sélection de fonctionnalités, ni sémaphores.

Mesuré sur la machine de référence : device créé en 30 à 40 ms, validation comprise
(`./build/linux-debug/sandbox/levain_sandbox`).

## Une frame, de bout en bout

1. **`beginFrame`** compare la taille de la fenêtre à celle de la swapchain, et la reconstruit si elles
   diffèrent. Sous Wayland, c'est indispensable : la surface ne connaît pas sa taille, et Vulkan ne déclare
   jamais la swapchain périmée au redimensionnement. Puis elle acquiert une image, et demande à NVRHI d'attendre
   le sémaphore qui dira qu'elle est libre.
2. **L'application dessine** avec une command list NVRHI. Les images de la swapchain sont des textures dont l'état
   initial « Present » est conservé : NVRHI place seul les barrières pour passer en cible de rendu, puis revenir
   en présentation.
3. **`presentFrame`** fait signaler un sémaphore quand le rendu est fini, présente, puis limite le CPU à deux
   frames d'avance sur le GPU (des *event queries* NVRHI). En présentation FIFO, calée sur le rafraîchissement
   de l'écran, c'est là que la boucle attend : **120 images/s sur l'écran à 120 Hz de la machine de référence,
   et le CPU tombe de 2 010 à 50 ms toutes les 2 s**.

Sous Direct3D 12, tant qu'il n'a pas de swapchain, la frame est celle de WebGPU en natif : `beginFrame` rend l'image
hors écran, `presentFrame` attend que le GPU ait fini (`waitForIdle`), une image en vol.

## Pièges connus

| Piège | Parade |
|---|---|
| `IID_PPV_ARGS` passe par `__uuidof`, une extension de Microsoft que `-pedantic-errors` refuse | Les IID nommés, de dxguid et DirectX-Guids (`interfaceId`, `iidOf`, `outPointer` dans `d3d12_context.hpp`) |
| Le `&` de `nvrhi::RefCountPtr`, à la différence de celui de `ComPtr`, ne relâche pas l'objet tenu, que l'appel COM écrase : il fuit | `outPointer` passe par `ReleaseAndGetAddressOf`, qui relâche d'abord l'objet tenu (`d3d12_context.hpp`) |
| La couche de debug D3D12 garde les messages émis avant que le moteur ne s'inscrive (la création du device) : le rappel ne les reçoit jamais | Relus juste après l'inscription, passés par le même chemin, puis effacés (`drainStoredMessages`) |
| Le NVRHI du port ne remettait un vertex buffer en état `VertexBuffer` que si le dessin avait un index buffer : les lignes de debug restaient en `COPY_DEST` sous Direct3D 12 | Le correctif de NVRHI (7a04ce9b8a), repris dans le port overlay (`ports/nvrhi`) |
| La couche de debug D3D12 avertit à chaque effacement d'une image créée sans valeur d'effacement, ou avec une autre (D3D12_MESSAGE_ID 820 et 821, trois par image) | La valeur d'effacement donnée à la création : profondeur et atlas des ombres (`render::FarthestDepth`, la même constante qu'à l'effacement), image HDR (recréée si le fond change) |
| NVRHI est compilé en bibliothèque statique : il ne définit pas le dispatcher de Vulkan-Hpp et ne l'initialise pas | `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` et `VULKAN_HPP_DEFAULT_DISPATCHER.init(…)` dans `device_vk.cpp`, `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1` dans le `CMakeLists.txt` |
| `nvrhi::vulkan::DeviceDesc desc;` laisse `transferQueue` et `computeQueue` indéterminés | `DeviceDesc desc{}` |
| LeakSanitizer signale 128 octets alloués par RADV | Faux positif : le loader décharge le pilote à la destruction de l'instance. Voir le skill `build`, `GOTCHA.md` |
| RADV affiche « radv is not a conformant Vulkan implementation » | Information de Mesa pour ce GPU récent (GFX1201), pas une erreur |
| La couche de validation de NVRHI enveloppe le device et n'expose pas `nvrhi::vulkan::IDevice`, seule interface qui connaît les sémaphores Vulkan | La swapchain garde le device Vulkan de NVRHI, le reste du moteur l'enveloppe (comme Donut) |
| WebGPU veut savoir qu'une texture est de profondeur, ou qu'un sampler compare (les ombres) ; le binding layout de NVRHI ne le dit pas, Vulkan s'en passe | Le backend WebGPU le déduit : des ressources liées pour un binding set, des déclarations WGSL (`texture_depth_2d`, `sampler_comparison`) pour un pipeline. WebGPU tient pour équivalents deux layouts aux mêmes entrées (`BindingHint`, `BindingLayout::layoutFor`). Même chose pour les cubemaps (`texture_cube<f32>`) et les storage textures, dont WebGPU veut le format (`texture_storage_2d_array<rgba16float, write>`) |
| Sous Vulkan, NVRHI n'active le biais de profondeur que si sa part constante (`depthBias`) est non nulle : un biais de pente seul est ignoré, et les ombres se couvrent d'acné, alors que WebGPU l'applique | Une part constante non nulle, même 1 (`shadows.cpp`) |
| Sous Windows, le chargeur Vulkan ne cherche pas les couches de validation à côté de l'exécutable | Le build copie celles du port vcpkg à côté de chaque exécutable qui lie `levain::gpu` (`cmake/LevainVulkanLayers.cmake`), et `device_vk.cpp` désigne ce dossier par `VK_ADD_LAYER_PATH` ; sans elles, le refus nomme le cas Windows |
| Sous Windows, Dawn ne trouve pas `vulkan-1.dll` et retombe sans un mot sur son backend Null, qui ne dessine rien | `webgpu/create.cpp` lui donne `System32` (`DawnInstanceDescriptor::additionalRuntimeSearchPaths`) ; sans pilote Vulkan, le repli sur Null reste muet jusqu'au refus du backend Null (#345) |
| Un `nvrhi::BindingSetItem` ne tient sa ressource que par un pointeur brut : un buffer qu'on ne garde que par son binding set (les constantes d'un matériau) disparaît à la création, et le suivant reprend son adresse | Le binding set garde une référence sur chaque ressource, comme le backend Vulkan de NVRHI ; le backend WebGPU le fait aussi (`webgpu::BindingSet::resources`) |
| Des sémaphores détruits pendant un redimensionnement peuvent encore être attendus par le moteur de présentation, et Vulkan ne dit pas quand il a fini | Ils ne sont jamais détruits avant la swapchain : on en ajoute si le nombre d'images augmente |

## Équivalents ailleurs

| Moteur | Module | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `VulkanRHI` | `FVulkanDynamicRHI` crée l'instance, `FVulkanDevice` le device, avec leur propre énumération des GPU et des extensions (**documenté** : sources publiques). |
| **Unreal** | `D3D12RHI` | `FD3D12DynamicRHI`, choisi au lancement (`-d3d12`, `-vulkan`) derrière l'interface `FDynamicRHI` ; l'adaptateur, le device et ses queues, la swapchain de `FD3D12Viewport` (**documenté** : sources publiques). Notre `NativeDevice`, réduit à la création du device et à la swapchain, NVRHI faisant le reste. |
| **Godot** | `drivers/vulkan` | `RenderingContextDriverVulkan` (instance, surfaces) et `RenderingDeviceDriverVulkan` (device) (**documenté** : dépôt public, depuis Godot 4.3). |
| **Godot** | `drivers/d3d12` | `RenderingContextDriverD3D12` (factory DXGI, adaptateurs) et `RenderingDeviceDriverD3D12` (device, swapchain), depuis Godot 4.3 (**documenté** : dépôt public). |
| **Unity** | — | Couche interne non publique. Ne pas supposer de correspondance. |
