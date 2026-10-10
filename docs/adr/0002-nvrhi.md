# ADR-0002 — Couche graphique : NVRHI, backends Vulkan et Direct3D 12

- **Statut** : accepté le 2026-09-20 ; remis en vigueur par [ADR-0011](0011-retour-au-cpp.md) après la parenthèse Rust ;
  amendé le 2026-10-10 (la vérification sous Proton remplacée, voir les conséquences)
- **Date** : 2026-09-20
- **Milestone** : M0.1 (mise en place en phase 1)
- **Historique** : la première version de cet ADR proposait une couche RHI écrite par nous au-dessus de Vulkan.
  Donnovan a précisé que la priorité est **de faire un jeu avec notre moteur**, et qu'apprendre Vulkan en détail
  ne l'intéresse pas. La décision a donc été revue le même jour.

## Contexte

Un moteur a besoin d'une couche entre son renderer et les API graphiques : la RHI (*Rendering Hardware
Interface*). Elle masque les différences entre Vulkan et Direct3D 12 et prend en charge le plus fastidieux des API
explicites : barrières de synchronisation, descripteurs, durée de vie des ressources, envois de données vers le GPU.
Le moteur doit tourner sous Windows et Linux.

## Options envisagées

| Option | Pour | Contre |
|---|---|---|
| RHI maison sur Vulkan | Compréhension maximale ; c'est ce que font Unreal et Godot | Plusieurs semaines avant le premier rendu utile ; un seul backend réaliste |
| **NVRHI** (NVIDIA, MIT) | Backends D3D11, D3D12 et Vulkan 1.3 ; suivi automatique des états et placement des barrières (désactivable) ; destruction différée des ressources ; couche de validation propre ; port vcpkg ; utilisé par les SDK RTX de NVIDIA et par RBDOOM-3-BFG | Pas de Metal ni d'OpenGL ; l'application doit créer elle-même le device et la swapchain ; communauté plus petite que les moteurs |
| Donut entier (NVIDIA, MIT) | Framework complet au-dessus de NVRHI : passes de rendu, glTF, ImGui | NVIDIA précise que ce n'est **pas** un moteur de jeu ; utilise GLFW et son propre graphe de scène, en conflit avec SDL3 et flecs ; le moteur ne serait plus le nôtre |
| Diligent Engine, bgfx | Multi-backend, matures | Modèles d'abstraction plus éloignés de ce que font les moteurs AAA (bgfx surtout) ; moins de documentation sur les internes |
| SDL_GPU | Même dépendance que la fenêtre ; Vulkan, D3D12 et Metal | Abstraction volontairement réduite (pas de ray tracing, pas de mesh shaders) |

## Décision

**NVRHI**, avec deux backends :

- **Vulkan** sous Linux et Windows : c'est le backend principal, testé sur la machine de référence ;
- **Direct3D 12** sous Windows (M1.4).

Direct3D 11 est disponible dans NVRHI, mais nous ne l'activons pas : c'est une API ancienne qui n'apporte rien à
nos plateformes.

**NVRHI ne crée pas le device ni la swapchain** : c'est à l'application de le faire. Nous écrivons donc un
`DeviceManager` par backend, en nous inspirant de celui de Donut (licence MIT), adapté à SDL3. C'est l'occasion
de comprendre l'initialisation d'un device sans en porter tout le code.

**Donut sert de référence** : on lit ses passes de rendu (forward, deferred, ombres, tonemapping…) et ses
exemples (Donut-Samples) pour écrire les nôtres.

## Conséquences

- La phase « écrire notre RHI » disparaît de la roadmap. Le triangle arrive plus tôt et la phase 2 est plus légère.
- NVRHI raisonne en **slots de type HLSL** (`t0`, `u0`, `b0`, `s0`) et applique des décalages sous Vulkan : nos
  shaders suivent cette convention (voir ADR-0005).
- Bibliothèques retirées de la stack : volk et VMA. NVRHI alloue lui-même la mémoire des ressources et s'appuie
  sur Vulkan-Hpp pour charger les fonctions Vulkan. vk-bootstrap reste possible pour le `DeviceManager` Vulkan,
  décision en M1.2.
- En Debug : couche de validation NVRHI **et** validation layers Vulkan (ou couche de debug D3D12). Une erreur de
  l'une ou de l'autre est un bug bloquant.
- Tests de fumée sans GPU en CI : lavapipe (Vulkan logiciel de Mesa) sous Linux, WARP (D3D12 logiciel de Microsoft)
  sous Windows.
- Sur la machine de référence (Linux), le backend D3D12 est vérifié en lançant le binaire Windows sous Proton :
  vkd3d-proton traduit D3D12 en Vulkan. Ce n'est pas un pilote D3D12 natif (voir SPECS §10). *Amendé le
  2026-10-10, par sondage de Donnovan :* Proton n'est plus requis. À chaque milestone de rendu, le sandbox se lance
  nativement sur son PC Windows, en Vulkan et en Direct3D 12, avec la couche de debug D3D12, et les tests Direct3D 12
  tournent sous WARP en CI (SPECS §10, point 2).
- NVRHI ne dépend pas du constructeur : la machine de référence a un GPU AMD. Les extensions propres à NVIDIA
  (NVAPI) restent désactivées.
- Si un jour on vise macOS, il faudra une autre solution pour Metal (hors périmètre v1).

## Ce que font les autres moteurs

- **Unreal** : RHI maison (`FRHICommandList`), traduite vers D3D12, Vulkan ou Metal par un thread dédié (RHI thread).
- **Godot 4** : `RenderingDevice`, avec des drivers Vulkan, D3D12 et Metal ; le renderer Compatibility (OpenGL)
  le contourne.
- **Unity** : couche graphique interne en C++, non documentée publiquement ; les pipelines (URP, HDRP) sont écrits
  en C# au-dessus.

Détails et sources : [étude E1](../etudes/E1-rhi.md).
