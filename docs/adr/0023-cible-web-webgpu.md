# ADR-0023 — Cible web : un backend WebGPU pour NVRHI, des fonctionnalités par plateforme

- **Statut** : proposé
- **Date** : 2026-09-27
- **Milestone** : M4.6 (nouveau, avant la phase 5)

## Contexte

Donnovan veut que le moteur tourne aussi dans un navigateur, et, à terme, sur PC, consoles et mobiles : **un seul
moteur**, dont les binaires sont compilés pour chaque plateforme visée. Il veut aussi **profiter des dernières
avancées du rendu** (ray tracing, mesh shaders, bindless…), activées selon la plateforme et ses capacités. Il l'a
demandé avant la phase 5, pour que le rendu PBR soit vérifié dans le navigateur dès son écriture. L'ADR-0002
(NVRHI) ne prévoyait ni le web, ni cette gradation.

Un prototype jetable (branche `spike/webgpu`, `prototypes/webgpu/README.md`) a été fait le 27/09/2026 avant de
trancher. Mesuré sur la machine de référence :

- **Le cube texturé de M2.2 tourne en WebGPU dans Firefox 156**, compilé par Emscripten 6.0.10 avec
  `emdawnwebgpu` (l'implémentation de `webgpu.h` que maintient Dawn pour le navigateur) : 60 images/s en headless,
  aucune erreur. 173 Ko de `.wasm`, 124 Ko de JavaScript. WebGPU y est derrière un réglage (`dom.webgpu.enabled`)
  sous Linux ; Chrome le demande aussi sur une carte AMD.
- **Le même code tourne en natif sur Dawn** (port vcpkg, 4,3 min de compilation) : même image, 0,065 ms par image.
- **Les shaders du moteur passent en WGSL par Slang**, validés par le compilateur WGSL de Firefox : `triangle`, et
  `mesh` avec les mêmes décalages de binding que NVRHI sous Vulkan. `skinning` demande de remplacer ses
  `ByteAddressBuffer`, que la cible WGSL de Slang ne sait pas lire, par des `StructuredBuffer<uint>` ; la
  variante compile aussi en SPIR-V.
- **Ce que le moteur demande à NVRHI** : une quarantaine de méthodes (buffers, textures, samplers, binding
  layouts et sets, pipelines graphique et compute, command lists, timer queries, relecture). Les backends Vulkan et
  D3D12 de NVRHI font chacun environ 10 000 lignes ; notre sous-ensemble en WebGPU en demanderait environ 2 000
  (estimation). NVRHI n'a pas de backend WebGPU, et une demande en ce sens a été close « not planned » [1].

## Options envisagées

| Option | Pour | Contre |
|---|---|---|
| **NVRHI reste l'interface du moteur, avec un backend WebGPU écrit chez nous** | `render/` ne change pas. En natif, NVRHI garde l'accès aux fonctionnalités récentes (ray tracing, meshlets, bindless, shading à taux variable), que le moteur interroge (`queryFeatureSupport`) ; le backend WebGPU les déclare absentes. Xbox (D3D12) et Switch 2 (Vulkan) restent proches de NVRHI | Environ 2 000 lignes à écrire, puis à suivre à chaque mise à jour de NVRHI. Mac et iOS n'ont pas de backend Metal (MoltenVK, avec ses limites) |
| WebGPU (Dawn) partout, à la place de NVRHI | Une seule API, rien à maintenir ; Mac, iOS, Android et web fournis par Dawn ; environ 1 400 lignes à réécrire | **WebGPU est le plus petit dénominateur commun** : ni ray tracing, ni mesh shaders, ni bindless. Le moteur serait bridé au niveau du navigateur, même sur une RX 9070 XT. Aucune console. Revient sur l'ADR-0002 |
| Préparer seulement, et reporter le backend en v2 | 0,75 h : shaders portables, WGSL compilé par la CI, code CPU compilé en `.wasm` | Rien ne se voit dans le navigateur avant la v2, et la phase 5 s'écrit sans être vérifiée sur le web |

Donnovan a écarté Dawn partout pour la raison du plus petit dénominateur commun, et choisi de faire le backend
**avant la phase 5**, sur sondage le 27/09/2026.

## Décision

**NVRHI reste la couche de rendu du moteur, sur toutes les plateformes. On lui ajoute un backend WebGPU**, écrit
dans le dépôt de Levain, sur l'API C `webgpu.h` :

- **dans le navigateur**, par `emdawnwebgpu` (Emscripten) ;
- **en natif, sur Dawn** (port vcpkg), pour développer et déboguer le backend sur PC, et pour que la CI le teste
  sans navigateur. Ce n'est pas un moteur de rendu natif de plus : en natif, le moteur garde NVRHI sur Vulkan.

**Les fonctionnalités se choisissent par plateforme.** Au démarrage, le moteur interroge le device
(`queryFeatureSupport`, `queryFormatSupport`) et active chaque technique selon ce qui est déclaré, avec un repli
(le BC7 sur PC, l'ASTC sur mobile, ADR-0020 ; plus tard le ray tracing ou son absence). Le backend WebGPU répond
« non supporté » à tout ce que WebGPU n'a pas.

**Les shaders restent en Slang, avec une troisième cible, WGSL**, compilée au build avec les mêmes décalages de
binding. Ils s'écrivent dans le **sous-ensemble portable** : pas de `ByteAddressBuffer` (des
`StructuredBuffer<uint>`), et la CI compile tout shader en WGSL, pour qu'un écart se voie à la PR.

**Ce que le backend doit traduire**, relevé par le prototype :

1. **Les constantes « volatiles »** : NVRHI écrit une nouvelle version à chaque dessin, dans l'ordre de la command
   list ; `writeBuffer` de WebGPU s'applique avant toute la soumission. Parade : un tampon circulaire et des
   offsets dynamiques.
2. **L'initialisation asynchrone** : l'adaptateur et le device arrivent par callbacks.
3. **La boucle principale** : le navigateur appelle une fonction à chaque image (`emscripten_set_main_loop`) ;
   la boucle `while` du sandbox devient cette fonction.
4. **La relecture est asynchrone** (`mapAsync`) : `--capture` reste natif ; le web se vérifie par
   `prototypes/webgpu/snapshot.mjs` (WebDriver BiDi).
5. **Le canvas n'est pas en sRGB** : une vue sRGB (`viewFormats`) garde les couleurs du natif.
6. **Les fichiers** : les assets sont préchargés dans le système de fichiers d'Emscripten.

Le sandbox web se publie sur **GitHub Pages**, pour que Donnovan le voie depuis sa tablette.

## Conséquences

- **Un milestone M4.6 « Cible web »**, avant la phase 5, estimé à **3,5 h** Donnovan :
  1. le code CPU (`core`, `scene`, `assets`, `animation`) compilé en `.wasm`, ses tests lancés par Node en CI ;
  2. les shaders en WGSL au build, et le skinning portable ;
  3. le backend, première moitié : device, buffers, textures, samplers, bindings, pipeline graphique ; le cube ;
  4. le backend, seconde moitié : compute (skinning), constantes volatiles, timer queries ;
  5. le sandbox web (boucle, SDL3 sous Emscripten, assets) et sa publication.

  La v1 passe de 56,9 h à 60,4 h. Les échéances ne bougent pas : le projet a environ onze semaines d'avance.
- **Une chaîne d'outils de plus** : Emscripten (emsdk, 1,7 Go installé), et Dawn pour les tests natifs du
  backend. La CI gagne un job web.
- **Le backend suit NVRHI** : une mise à jour de NVRHI qui change son interface demande de mettre le backend à jour.
  vcpkg fige la version ; la CI le verra.
- **Les consoles**, plus tard, suivront le même schéma : un backend NVRHI par plateforme, sous NDA, que D3D12 et
  Vulkan rapprochent déjà pour Xbox et Switch 2 (déduit). Mac et iOS demanderont MoltenVK ou un backend Metal.
- **Le multithreading** reste éteint sur le web pour l'instant : il demande `SharedArrayBuffer`, donc des en-têtes
  HTTP (COOP et COEP) que GitHub Pages ne pose pas. Il se rouvrira avec le premier besoin.
- **L'ADR-0002 n'est pas remplacé**, il est complété : NVRHI en reste le choix.

## Ce que font les autres moteurs

- **Unreal** (documenté [2]) : une RHI avec un backend par API et par plateforme, et des *feature levels* (ES3_1,
  SM5, SM6) qui disent ce que le moteur peut supposer. Les *scalability settings* règlent la qualité par machine.
  C'est le modèle de cette décision.
- **Unity** (documenté [3]) : WebGPU est une API graphique de plus pour le build web, à activer à la main, et
  présentée comme expérimentale dans la documentation d'Unity 6.1 ; WebGL2 reste en repli.
- **Godot** (documenté [4]) : le web n'utilise que le rendu *Compatibility* (WebGL 2.0). Forward+ et Mobile n'y
  tournent pas, faute de WebGPU. Trois rendus différents selon la plateforme : l'inverse de ce que cherche Donnovan.

## Sources

1. NVIDIA, *Web Support (WebGL/WebGPU)*, NVRHI issue #37 — https://github.com/NVIDIA-RTX/NVRHI/issues/37
2. Epic Games, *ERHIFeatureLevel::Type* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI/ERHIFeatureLevel__Type
3. Unity, *Introduction to the WebGPU graphics API* —
   https://docs.unity3d.com/6000.6/Documentation/Manual/WebGPU-features.html
4. Godot, *Exporting for the Web* —
   https://github.com/godotengine/godot-docs/blob/master/tutorials/export/exporting_for_web.rst
5. Emscripten, *Using WebGPU in Emscripten* —
   https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU-support.html
6. WebGPU, *Implementation Status* — https://github.com/gpuweb/gpuweb/wiki/Implementation-Status
