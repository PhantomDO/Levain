# `engine/render`

## Rôle

Dessiner, avec NVRHI : pipelines, passes de rendu, et plus tard caméras, matériaux, éclairage et ombres
(SPECS §7). **Le module ne connaît que `nvrhi::IDevice`**, jamais Vulkan ni `engine/gpu` : il dessinera tel
quel sous Direct3D 12 le jour où ce backend existera (`docs/QA.md`, question du 2026-09-21).

**État en M2.2** : le premier triangle (`TrianglePass`), une caméra perspective (`Camera`), des meshes indexés
dessinés avec un depth buffer (`MeshPass`), en plusieurs exemplaires par un seul draw (`Instances`), texturés
avec tous leurs niveaux de mip (`createTexture`, `createMaterialBindings`), lus par un sampler réglable
(`createSampler`, filtrage anisotrope), et le temps GPU d'une frame (`GpuTimer`). Le pipeline des meshes se
recrée à chaud quand son shader change (`reloadMeshPassShaders`, ADR-0014).

**En M4.5** : la première passe compute, le skinning (`SkinningPass`, ADR-0022).

## Invariants

1. **Aucune dépendance vers l'API graphique.** Le seul endroit qui distingue Vulkan de Direct3D 12 est
   `shaderExtensionFor` (`src/shader.cpp`), qui choisit entre SPIR-V et DXIL d'après `device.getGraphicsAPI()`.
2. **Les shaders sont compilés au build** (`shaders/CMakeLists.txt`, ADR-0005) et lus sur le disque à la création
   des passes. Une passe qui ne trouve pas ses shaders échoue avec un `Result`, pas une assertion : c'est le
   contenu du disque, pas un bug (ADR-0008).

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/render/triangle.hpp`](include/levain/render/triangle.hpp) | `createTrianglePass`, `drawTriangle` |
| [`include/levain/render/camera.hpp`](include/levain/render/camera.hpp) | `Camera`, `viewOf`, `projectionOf`, `viewProjectionOf` — profondeur de 0 à 1, comme Vulkan et Direct3D 12 |
| [`include/levain/render/mesh.hpp`](include/levain/render/mesh.hpp) | `Mesh`, `createMesh`, `createCube`, `createPlane` — buffers de sommets et d'indices ; `Instances`, `createInstances`, `updateInstances` — un décalage par exemplaire, remplaçable à chaque frame |
| [`include/levain/render/mesh_pass.hpp`](include/levain/render/mesh_pass.hpp) | `createMeshPass`, `reloadMeshPassShaders`, `ensureDepthTexture`, `createMaterialBindings`, `drawMesh` — la première passe avec constantes, profondeur et texture |
| [`include/levain/render/texture.hpp`](include/levain/render/texture.hpp) | `TextureLevel`, `createTexture` — une texture sRGB et tous ses niveaux de mip ; `SamplerSettings`, `createSampler`, `clampAnisotropy` |
| [`include/levain/render/gpu_timer.hpp`](include/levain/render/gpu_timer.hpp) | `GpuTimer`, `beginGpuTimer`, `endGpuTimer` — temps GPU par timer queries |
| [`include/levain/render/skinning.hpp`](include/levain/render/skinning.hpp) | `SkinnedVertex`, `createSkinningPass`, `createSkinnedMesh`, `skinMesh` — le skinning en compute |
| [`include/levain/render/light_clusters.hpp`](include/levain/render/light_clusters.hpp) | `PointLight`, `ClusterGrid`, `createLightClusterPass`, `assignLightsToClusters` — le tri des lumières ponctuelles en clusters (forward+) ; `lightsPerClusterOf`, sa référence CPU |

## Ce qu'il faut pour dessiner un triangle avec NVRHI

1. **Deux shaders** (`nvrhi::IShader`), créés à partir du bytecode compilé au build.
2. **Un pipeline graphique** (`nvrhi::IGraphicsPipeline`) : les shaders, l'état de rendu (profondeur, faces à
   écarter), et le format des cibles où il dessinera. Il se crée une fois, pas à chaque frame.
3. **Un framebuffer** (`nvrhi::IFramebuffer`) : les textures cibles de ce draw. Ici, l'image de la swapchain.
4. **Un état graphique** passé à la command list (pipeline, framebuffer, viewport), puis un `draw` de 3 sommets.
   NVRHI place lui-même les barrières que ces changements d'état demandent.

Aucun vertex buffer : les sommets sont écrits dans le shader et choisis par `SV_VertexID`, comme le Basic
Triangle de Donut-Samples.

Mesuré sur la machine de référence, en Debug avec validation : **0,09 ms de CPU pour enregistrer et soumettre une
frame**, 0,15 ms pour la frame entière hors attente de l'écran
(`SDL_VIDEO_DRIVER=offscreen ./tools/tracy-capture.sh 3`, zones `commandes` et `rendu`).

## Ce qu'ajoute un mesh au triangle

- **Des buffers de sommets et d'indices**, envoyés par `writeBuffer` : NVRHI passe par un buffer d'envoi interne et
  place les barrières. Un *input layout* dit au pipeline comment lire chaque sommet (position, normale, tangente,
  couleur, coordonnées de texture). La normale et la tangente servent à l'éclairage (M5.1) ; `tangentOf` calcule
  la seconde pour les meshes générés.
- **Des constantes par frame** dans un *volatile constant buffer*, lié par un binding set dans `space0`
  (ADR-0013) : NVRHI fournit une nouvelle version à chaque écriture, sans buffer par frame en vol à gérer.
- **Un depth buffer**, recréé seulement quand la taille de l'image change, et l'élimination des faces arrière
  (sens trigonométrique, vérifié par le test de fumée du cube).

## Ce qu'ajoute l'instancing

- **Un second vertex buffer, lu par exemplaire et non par sommet** : l'attribut `INSTANCE_OFFSET` est déclaré
  `setIsInstanced(true)` dans l'input layout et lu dans le slot 1. Le GPU avance d'un élément à chaque exemplaire.
- **Un seul `drawIndexed`** avec `instanceCount = 10 000` : le CPU enregistre un appel, quel que soit le nombre
  de cubes. Mesuré : 0,022 ms de GPU pour 10 000 cubes en 1080p (journal, #42).
- Le décalage est une simple position, pas une matrice : c'est tout ce dont la grille a besoin. Une matrice par
  exemplaire viendra avec des objets qui tournent chacun de leur côté.

## Ce qu'ajoute une texture

- **Une texture sRGB avec tous ses niveaux** : les mips sont calculées sur le CPU par `engine/assets`, puis
  `writeTexture` envoie chaque niveau par le buffer d'envoi interne de NVRHI. Le format `SRGBA8_UNORM` fait
  reconvertir chaque lecture en lumière linéaire par le GPU, avant le filtrage.
- **Un binding set de matériau dans `space2`** (ADR-0013) : la texture et un sampler. `space1`, réservé aux
  ressources de passe, reste vide : NVRHI comble le trou par un descriptor set vide. Le binding set se crée une
  fois par matériau, pas à chaque dessin.
- **Un sampler par matériau** (`SamplerSettings`) : trilinéaire toujours, anisotrope en option, texture répétée
  ou étirée au-delà de [0, 1].
- `render` ne connaît pas `assets::Image` : l'appelant la décrit par des `TextureLevel` (SPECS §7).

## Le filtrage anisotrope

Sur un sol vu de biais, un pixel de l'écran couvre une bande de texture longue et étroite. Le trilinéaire choisit
le niveau de mip d'après la **plus grande** dimension de cette bande : l'image est nette en travers, floue en
long, et le sol tourne au gris bien avant l'horizon. Le filtrage anisotrope prend un niveau plus fin et fait
jusqu'à N lectures le long de la bande (`maxAnisotropy`, 16 au plus).

Mesuré sur le sol du sandbox (`tools/renderdoc-anisotropy.py`, `docs/images/m2.2-anisotropy.png`) : près de
l'horizon, le contraste du damier passe de 0,088 à 0,160 ; le temps GPU de la frame, de 0,047 à 0,072 ms.

Côté Vulkan, il faut la fonctionnalité `samplerAnisotropy` du device (`engine/gpu/src/device_vk.cpp`) : sans
elle, la validation refuse le sampler.

## Le hot-reload des shaders

Modifier `shaders/mesh.slang` pendant que le sandbox tourne change l'image **en moins d'une demi-seconde**
([ADR-0014](../../docs/adr/0014-hot-reload-des-shaders.md)) :

1. le sandbox voit la nouvelle date du fichier (`core::takeChangedFiles`, toutes les 100 ms) ;
2. il relance le build des shaders (`platform::runProcess`, `cmake --build … --target levain_shaders`), ~350 ms ;
3. `reloadMeshPassShaders` relit le SPIR-V et recrée **le pipeline seul** : binding layouts, buffers et binding
   sets restent. Le pipeline dépend des shaders ; les layouts, de ce que la passe leur fournit.

Si la compilation échoue, le message de slangc va dans le log et le pipeline en place continue de servir.
Si elle réussit mais que NVRHI refuse le pipeline, la passe garde aussi l'ancien : le remplacement se fait sur une
copie. Limite : un shader qui ne correspond plus à la passe (un attribut retiré) déclenche une erreur de
validation, donc une assertion en Debug. Vérification : `tools/shader-hot-reload.sh`.

## Le skinning en compute

Un mesh skinné a deux buffers de sommets ([ADR-0022](../../docs/adr/0022-animation-squelettique.md)) : ses
sommets d'origine, avec leurs quatre os et leurs poids (`SkinnedVertex`), et ses sommets déformés, au format de
`MeshVertex`. À chaque image, `skinMesh` envoie les matrices des os et lance `shaders/skinning.slang`, un thread
par sommet, qui écrit les sommets déformés. La passe des meshes les dessine ensuite comme ceux d'un mesh rigide :
elle ne sait rien de l'animation, et les ombres (M5.3) les reliront de même.

Ce qu'apporte le compute à ce qu'on savait déjà :

- **un pipeline compute** (`nvrhi::IComputePipeline`) : un shader et ses binding layouts, sans état de rendu ;
- **des tampons de mots** (`StructuredBuffer<uint>`, vues `StructuredBuffer_SRV` et `StructuredBuffer_UAV` de
  pas 4) : le shader lit et écrit mot de 32 bits par mot, pour que la disposition soit exactement celle du C++.
  Un `StructuredBuffer` de `float3` serait aligné sur 16 octets sous Vulkan, et décalerait tout sans erreur. Pas
  de `ByteAddressBuffer`, que la cible WGSL de Slang ne sait pas lire (ADR-0023) ;
- **les transitions entre compute et dessin** : le même buffer est écrit par le compute (état *UnorderedAccess*)
  puis lu comme vertex buffer. NVRHI suit l'état de chaque ressource et place la barrière entre les deux
  (suivi automatique des états, activé par défaut).

Coût mesuré sur Fox (24 os, en Release) : 3 µs CPU (pose, matrices, enregistrement) et 5 µs GPU par image.

## Les lumières en clusters (forward+)

Le rendu est un forward+ en clusters ([ADR-0024](../../docs/adr/0024-forward-plus-en-clusters.md)) : avant de
dessiner, un compute trie les lumières ponctuelles, pour que chaque pixel ne parcoure que celles qui peuvent
l'atteindre. Le volume de la caméra est découpé en une grille 3D (`ClusterGrid`, 16 × 9 cases à l'écran,
24 tranches en profondeur) ; les tranches s'épaississent avec la distance (`sliceDepthOf`), parce que la
précision compte surtout près de la caméra.

`assignLightsToClusters` envoie les lumières et lance `shaders/light_clusters.slang`, un thread par cluster : il
calcule la boîte du cluster dans le repère de la caméra (les quatre coins de sa case, prolongés jusqu'aux deux
profondeurs de sa tranche), puis garde les lumières dont la sphère la touche. Il écrit deux listes : le **compte**
exact par cluster, et les indices des `MaxLightsPerCluster` premières lumières. Un compte plus grand que la
limite se voit, au lieu de disparaître.

Le même calcul existe côté CPU (`lightsPerClusterOf`) : `levain_light_clusters` compare les deux, cluster par
cluster, sur Vulkan et sur WebGPU.

## L'éclairage PBR

Le fragment shader de la passe des meshes (`shaders/mesh.slang`) éclaire chaque pixel selon le modèle de matériau
de glTF, *metallic-roughness* :

- **la couleur de base** (`baseColorFactor` × texture sRGB × couleur du sommet) ;
- **le métal et la rugosité** (facteurs × texture de données : rugosité en vert, métal en bleu). Un diélectrique
  réfléchit 4 % de la lumière de face, un métal réfléchit sa couleur, et n'a pas de diffus ;
- **la normale**, inclinée par la normal map dans le repère (tangente, bitangente, normale) du sommet.

La BRDF est celle de Cook-Torrance, la même que dans Unreal, Unity et Godot : une distribution des micro-facettes
(GGX), leur ombrage mutuel (Smith) et la part réfléchie selon l'angle (Fresnel, approximation de Schlick), plus
le diffus de Lambert pour la lumière qui entre dans la matière. Les lumières :

- **le soleil** (`Sun`) touche tout l'écran ;
- **les lumières ponctuelles** ne sont lues que dans le cluster du pixel (`clusterOf`, le calcul inverse du
  tri) ; leur atténuation est l'inverse du carré de la distance, ramené à zéro à leur portée ;
- **une lumière ambiante** uniforme remplace l'éclairage d'environnement en attendant l'IBL (M5.4).

`setFrameLighting` écrit ces constantes une fois par command list, après le tri (`assignLightsToClusters`). Le
résultat est de la lumière linéaire : au-delà de 1, la cible 8 bits la coupe, jusqu'au HDR de M5.2.

## Mesurer le temps GPU

Le CPU ne voit que le temps qu'il passe à enregistrer : le GPU exécute plus tard, en parallèle. Une **timer
query** demande au GPU d'horodater le début et la fin d'un bloc de commandes ; NVRHI la crée, l'enregistre
(`beginTimerQuery`, `endTimerQuery`) et rend la durée en secondes (`getTimerQueryTime`). Le résultat n'arrive
que quand le GPU a fini la frame, d'où l'anneau de trois requêtes de `GpuTimer`.

## Équivalents ailleurs

| Moteur | Module | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `Renderer` | Les passes (`FDeferredShadingSceneRenderer`) écrites au-dessus de la RHI, via le Render Dependency Graph (**documenté** : sources publiques). |
| **Godot** | `servers/rendering/renderer_rd` | Les renderers Forward+ et Mobile, écrits au-dessus de `RenderingDevice` (**documenté** : dépôt public). |
| **Unity** | SRP (URP, HDRP) | Les pipelines de rendu, écrits en C# au-dessus de la couche graphique interne (**documenté** : packages publics). |
| **Godot** | `ClusterBuilderRD` | Le tri des lumières en clusters du renderer Forward+ (**documenté** : dépôt public). |
| **Unity** | URP Forward+ | Les lumières triées par tuiles et en profondeur, au-delà de la limite de 8 lumières par objet du Forward (**documenté** : manuel de l'URP). |
| **Unreal** | `FRHIRenderQuery`, `stat gpu` | Timer queries au-dessus de la RHI, affichées par passe (**documenté** : sources publiques). |
| **Unreal** | `recompileshaders changed`, `ShaderCompileWorker` | Recompilation à chaud par des processus séparés (**documenté** : documentation d'Epic). |
| **Unity** | GPU Instancing, Frame Timing Manager | Instancing activé par matériau ; temps GPU par frame (**documenté** : manuel). |
| **Unreal** | `TextureGroup`, `r.MaxAnisotropy` | L'anisotropie se règle par groupe de textures, plafonnée par un réglage global (**documenté** : sources publiques). |
| **Unity** | Texture Importer, *Aniso Level* ; `QualitySettings.anisotropicFiltering` | Un niveau par texture, que les réglages de qualité peuvent forcer (**documenté** : manuel). |
| **Godot** | `MultiMeshInstance3D` | Un mesh dessiné en N exemplaires par un seul draw (**documenté** : docs officielles). |
