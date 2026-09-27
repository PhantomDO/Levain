# `engine/gpu`

## Rôle

Donner un GPU au moteur : instance, surface et device Vulkan, puis le device NVRHI par-dessus. C'est, avec
`render/`, le seul module qui voit NVRHI, et avec `platform/`, le seul qui inclut SDL, pour créer la surface
(SPECS §7).

**État en M1.2** : device Vulkan et device NVRHI, validation redirigée vers nos logs (#12) ; swapchain
reconstruite au redimensionnement, frames cadencées par l'écran (#13).

**État en M4.6** : un second backend de NVRHI, **WebGPU** (ADR-0023, `src/webgpu/`), écrit dans le dépôt : dans
le navigateur par emdawnwebgpu, en natif sur Dawn. En natif, il ne sert qu'à développer et vérifier le backend
(`levain_sandbox --gpu webgpu`, rendu hors écran, `--capture` pour voir l'image) : le moteur reste sur Vulkan.
Il reproduit les images de Vulkan (test de fumée à 0 pixel près, renard et Sponza).

## Invariants

1. **Ni Vulkan ni vk-bootstrap dans l'API.** `device.hpp` n'expose que NVRHI et `platform::Window` ;
   `VulkanContext` n'y est que déclaré. vk-bootstrap, les en-têtes Vulkan et SDL sont liés en `PRIVATE`.
2. **L'ordre de destruction est écrit dans l'ordre des déclarations.** Dans `GpuDevice` : `swapchain`, dont les
   images sont des textures NVRHI, puis `nvrhi`, puis `vulkan`. Dans le sandbox, le `GpuDevice` est déclaré après
   la fenêtre, pour que la surface disparaisse avant la fenêtre SDL qui la porte.
3. **En Debug, toute erreur de validation arrête le programme** sur une assertion, qu'elle vienne des couches
   Vulkan ou de NVRHI (règle n°4). Les messages du *loader* Vulkan, qui signale par exemple une couche tierce
   cassée (SPECS §10), sont journalisés sans arrêter : ce ne sont pas des bugs du moteur.
4. **Le dispatcher de Vulkan-Hpp est défini une seule fois**, dans `device_vk.cpp`.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/gpu/device.hpp`](include/levain/gpu/device.hpp) | `createGpuDevice`, `GpuDevice`, `DeviceOptions`, `swapchainFormat`, `beginFrame`, `presentFrame` |
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

## Pièges connus

| Piège | Parade |
|---|---|
| NVRHI est compilé en bibliothèque statique : il ne définit pas le dispatcher de Vulkan-Hpp et ne l'initialise pas | `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` et `VULKAN_HPP_DEFAULT_DISPATCHER.init(…)` dans `device_vk.cpp`, `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1` dans le `CMakeLists.txt` |
| `nvrhi::vulkan::DeviceDesc desc;` laisse `transferQueue` et `computeQueue` indéterminés | `DeviceDesc desc{}` |
| LeakSanitizer signale 128 octets alloués par RADV | Faux positif : le loader décharge le pilote à la destruction de l'instance. Voir le skill `build`, `GOTCHA.md` |
| RADV affiche « radv is not a conformant Vulkan implementation » | Information de Mesa pour ce GPU récent (GFX1201), pas une erreur |
| La couche de validation de NVRHI enveloppe le device et n'expose pas `nvrhi::vulkan::IDevice`, seule interface qui connaît les sémaphores Vulkan | La swapchain garde le device Vulkan de NVRHI, le reste du moteur l'enveloppe (comme Donut) |
| Des sémaphores détruits pendant un redimensionnement peuvent encore être attendus par le moteur de présentation, et Vulkan ne dit pas quand il a fini | Ils ne sont jamais détruits avant la swapchain : on en ajoute si le nombre d'images augmente |

## Équivalents ailleurs

| Moteur | Module | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `VulkanRHI` | `FVulkanDynamicRHI` crée l'instance, `FVulkanDevice` le device, avec leur propre énumération des GPU et des extensions (**documenté** : sources publiques). |
| **Godot** | `drivers/vulkan` | `RenderingContextDriverVulkan` (instance, surfaces) et `RenderingDeviceDriverVulkan` (device) (**documenté** : dépôt public, depuis Godot 4.3). |
| **Unity** | — | Couche interne non publique. Ne pas supposer de correspondance. |
