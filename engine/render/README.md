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
| [`include/levain/render/shadows.hpp`](include/levain/render/shadows.hpp) | `CascadeSettings`, `cascadeSplitsOf`, `cascadesOf` — les tranches de profondeur et les projections du soleil des ombres en cascades |
| [`include/levain/render/environment.hpp`](include/levain/render/environment.hpp) | `createEnvironment` — l'HDRI du ciel converti en cubemap, mips comprises, puis l'irradiance, le spéculaire préfiltré et la table de la BRDF de l'éclairage par l'image |
| [`include/levain/render/sky.hpp`](include/levain/render/sky.hpp) | `createSkyPass`, `drawSky` — le ciel de l'environnement en fond, là où aucun mesh n'est dessiné |
| [`include/levain/render/tonemap.hpp`](include/levain/render/tonemap.hpp) | `HdrFormat`, `createTonemapPass`, `ensureHdrTarget`, `tonemap` — l'image HDR et sa passe vers la swapchain |
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
(GGX), leur ombrage mutuel (Smith « height-correlated ») et la part réfléchie selon l'angle (Fresnel, approximation
de Schlick), plus le diffus de Lambert pour la lumière qui entre dans la matière. Ces termes vivent dans
`shaders/brdf.slang`, partagé par l'éclairage direct et le précalcul de l'IBL. Comme le veut l'annexe B de la
spécification glTF, `metallic` mélange deux BRDF entières, celle d'un diélectrique (F0 de 0,04) et celle d'un métal
(F0 = sa couleur), et non leurs F0 : avec un F0 mélangé, une surface à moitié métallique perdait deux fois sa part
diffuse (#125). Les lumières :

- **le soleil** (`Sun`) touche tout l'écran ;
- **les lumières ponctuelles** ne sont lues que dans le cluster du pixel (`clusterOf`, le calcul inverse du
  tri) ; leur atténuation est l'inverse du carré de la distance, ramené à zéro à leur portée ;
- **le ciel** éclaire tout le reste (l'IBL, voir plus bas) : l'irradiance pour le diffus, le reflet préfiltré à la
  rugosité du matériau pour le spéculaire, multiplié par la table de la BRDF (`environmentLightOf`), avec l'énergie
  des rebonds multiples entre micro-facettes (`iblFresnelOf`, Fdez-Agüera 2019) : sans elle, un métal rugueux perd
  jusqu'au tiers de sa lumière. Le calcul suit l'annexe B de la spécification glTF, comme le glTF Sample Viewer. Sans HDRI
  (`--sky` dans le sandbox), un ciel uniforme et sombre (`createUniformEnvironment`) tient lieu de lumière ambiante.

`setFrameLighting` écrit ces constantes une fois par command list, après le tri (`assignLightsToClusters`). Le
résultat est de la lumière linéaire : au-delà de 1, la cible 8 bits la coupe, jusqu'au HDR de M5.2.

## Les ombres en cascades

Le soleil ombre la scène par des **shadow maps** : une image de profondeur vue depuis le soleil, où le shader
d'éclairage vérifie si un pixel est caché. Une seule ne suffit pas pour un monde vu de près comme de loin : ses
texels seraient trop gros devant la caméra. Le volume de vue est donc découpé en **quatre tranches de profondeur**
(`cascadeSplitsOf`), chacune avec sa shadow map, une **cascade** (M5.3) :

- les tranches mélangent un partage égal et un partage au même rapport (le schéma « pratique » de Zhang et al.) :
  fines près de la caméra, épaisses au loin ;
- chaque tranche est enfermée dans une **sphère**, dont la taille ne change pas quand la caméra tourne ; et la
  projection du soleil **avance par texels entiers**. Sans ces deux précautions, les bords des ombres
  scintillent à chaque mouvement de caméra ;
- le soleil regarde la tranche depuis un peu plus loin (`CasterMargin`), pour qu'un objet hors de la tranche, entre
  elle et le soleil, y jette quand même son ombre.

`tests/shadows_test.cpp` vérifie que chaque point du volume de vue tombe dans la shadow map de sa cascade (pas de
trou entre cascades), et la stabilité.

La **passe d'ombres** (`createShadowPass`, `drawShadowCaster`) dessine chaque objet, vu du soleil, dans chaque
cascade : un pipeline sans fragment shader, qui n'écrit que la profondeur, dans un **atlas** de 2 × 2 cascades (une
seule texture 2D, la même sur Vulkan et sur WebGPU). Le shader d'éclairage choisit la cascade du pixel par sa
profondeur, s'y projette et **compare** sa profondeur à celle de l'atlas (`sunVisibilityOf`) : un sampler de
comparaison rend 0 ou 1, filtré par le GPU sur les 2 × 2 texels voisins. Contre l'« acné » (une surface qui
s'ombre elle-même par l'arrondi de sa profondeur) : un biais de profondeur à l'écriture, et le point décalé le
long de sa normale à la lecture. Le bord de l'ombre est adouci par un **PCF** (*percentage-closer filtering*) :
3 × 3 comparaisons voisines, chacune déjà filtrée sur 2 × 2 texels, gardées dans le quart de leur cascade. Le
sandbox mesure le temps GPU de la passe d'ombres et le donne à la fin (« ombres : … ms GPU »). La scène `shadow` du test de fumée vérifie l'ombre d'un cube sur un sol.

## L'environnement de l'éclairage par l'image

L'**éclairage par l'image** (IBL, M5.4) éclaire la scène par le ciel tout entier, et non plus par une ambiance
uniforme. Le ciel est une **HDRI** : une photo à 360° en équirectangulaire (la longitude en largeur, la latitude en
hauteur), en lumière linéaire. `createEnvironment` la convertit au chargement en **cubemap**, six faces carrées
que le GPU lit sans la déformation des pôles, puis calcule ses **niveaux de mip** : chacun est la moyenne 2 × 2 du
précédent, d'une seule lecture bilinéaire au coin des quatre texels. Les deux passes sont des computes
(`shaders/environment.slang`) qui écrivent la cubemap face par face, comme un tableau de six textures 2D.

Puis trois **convolutions** préparent l'éclairage, d'après Karis (« Real Shading in Unreal Engine 4 ») :

- l'**irradiance** (32² par face) : ce qu'une surface mate reçoit du ciel par direction de sa normale, la moyenne
  du ciel pondérée par le cosinus ;
- le **spéculaire préfiltré** (128², six mips) : le reflet du ciel flouté par le lobe GGX, un mip par rugosité, de
  0 au premier à 1 au dernier, en supposant la surface vue de face ;
- la **table de la BRDF** (128², `brdfLut`) : pour un angle de vue et une rugosité, l'échelle et le biais de la
  couleur spéculaire F0, avec la visibilité de Smith « height-correlated » (Heitz 2014). Elle ne dépend pas du ciel. Le shader de mesh multiplie le reflet par cette table : la
  *split sum*, deux intégrales précalculées séparément au lieu d'une par pixel.

Chaque échantillon des convolutions lit le ciel au mip dont un texel couvre son angle solide (l'échantillonnage
*filtré* de Colbert et Křivánek) : 256 échantillons suffisent, sans les points brillants d'un soleil tiré au
hasard.

Le **soleil d'une HDRI** (`extractSun`) en est retiré avant le calcul : sans cela, il éclairerait aussi par l'IBL,
qui n'a pas d'ombres, et pâlirait celles du soleil. Le pixel le plus brillant, s'il dépasse 1000 fois la moyenne de
l'image, donne la direction ; les pixels à moins de 5° de lui sont ramenés à un millième de son pic, et l'énergie
retirée (luminance × angle solide de chaque pixel) devient une lumière directionnelle, qui jette les ombres. Le
soleil de Kloofendal culmine à 72 559, au-delà du plus grand flottant 16 bits (65 504) : écrêté sans être retiré, il
aurait perdu son énergie en silence.

Le **ciel en fond** (`drawSky`, `shaders/sky.slang`) se dessine après les meshes : un triangle plein écran au plan
lointain, qui ne passe le test de profondeur que là où aucun mesh n'est dessiné, et lit la cubemap dans la direction
de chaque pixel. La vue n'y garde que sa rotation : le ciel reste à l'infini quand la caméra avance.

`levain_environment` (tests `gpu.environment.*`) vérifie tout sur Vulkan et sur WebGPU :

- une image dont chaque pixel vaut sa propre direction doit donner une cubemap dont chaque texel vaut la sienne,
  à tous les niveaux. Une face mal orientée par rapport au GPU s'y voit dès le premier mip, que le GPU calcule en
  lisant la cubemap comme un cube ;
- un ciel uniforme reste lui-même après chaque convolution, et un ciel blanc au-dessus de l'horizon donne une
  irradiance de 1 vers le zénith, 0 vers le nadir, ½ à l'horizon ;
- la table renvoie toute la lumière quand la surface est lisse, de moins en moins quand elle devient rugueuse.

## L'image HDR et le tonemapping

La scène ne se dessine plus dans la swapchain mais dans une **image HDR** (`HdrFormat`, 16 bits flottants par
canal, M5.2) : la lumière y garde sa vraie valeur, au-delà de 1 (un reflet du soleil, une lampe proche). Une
passe plein écran, `tonemap`, la ramène ensuite dans la swapchain : un seul triangle, deux fois plus grand que
l'écran, et un shader qui lit l'image HDR pixel par pixel.

Cette passe applique l'**exposition** (`--exposure` dans le sandbox : 2 éclaire d'un diaphragme), puis une
**courbe de tonemapping** ramène la lumière, de 0 à l'infini, dans ce que l'écran affiche, de 0 à 1 :

- **AgX** (Troy Sobotka), par défaut, choisi sur captures comparatives (`tools/tonemap-captures.sh`) : la lumière
  passe en logarithme, puis une courbe en S la ramène ; les couleurs très vives virent au blanc au lieu de
  saturer d'un seul canal. C'est la courbe par défaut de Blender depuis la 4.0 ;
- **ACES** (approximation de Stephen Hill) : plus contrastée, le look « cinéma » ;
- **Khronos PBR Neutral** : les couleurs passent telles quelles jusqu'à 0,76, puis le pic se comprime vers 1 ; un
  albédo s'y lit à sa vraie couleur. C'est la courbe par défaut du glTF Sample Viewer, celle des comparaisons avec
  lui (#125, #131) ;
- **la coupe nette** : ce que faisait le moteur avant M5.2, les blancs saturent.

`--tonemap clip|aces|agx|neutral` choisit dans le sandbox.

## Se comparer au glTF Sample Viewer

`tools/khronos-compare.sh <dossier> [ibl|direct]` mesure l'écart avec la référence de Khronos, sous le ciel
(`ibl`, #131) ou sous la seule lumière directe (`direct`, #125) :

1. le glTF Sample Viewer hébergé ouvre MetalRoughSpheres dans un Firefox headless. Le viewer ne publie pas sa
   caméra : un script injecté avant la page la relit dans ses appels WebGL (`u_Camera`) ;
2. le sandbox rend la même vue (`--view khronos --camera x,y,z`) : le modèle seul, à l'origine, son champ de 45°,
   son ciel `Cannon_Exterior` tourné de 90° comme le sien, sans soleil tiré du ciel ni lumière de la démo, et sa
   courbe, Khronos PBR Neutral ;
   En `direct`, le script coupe l'IBL dans l'interface du viewer, et relit la direction de sa lumière principale,
   reprise comme soleil (`--sky none --sun x,y,z`). Sa lumière d'appoint, venue de l'opposé, n'éclaire pas la face
   des sphères tournée vers l'œil ;
3. l'écart se mesure sphère par sphère, en ΔE76 (CIELAB), sur un disque de 6 pixels au centre de chacune des 7 × 7
   sphères blanches : là, la normale fait face à l'œil, et les bords crénelés (le viewer lisse les siens) ne
   comptent pas.

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
| **Unreal** | Post Process Volume, *Film* | Exposition automatique ou manuelle, courbe « Filmic » inspirée d'ACES (**documenté** : documentation d'Epic). |
| **Godot** | `Environment.tonemap_mode` | Linear, Reinhard, Filmic, ACES, et AgX depuis Godot 4.4 (**documenté** : documentation de Godot). |
| **Unreal** | `FRHIRenderQuery`, `stat gpu` | Timer queries au-dessus de la RHI, affichées par passe (**documenté** : sources publiques). |
| **Unreal** | `recompileshaders changed`, `ShaderCompileWorker` | Recompilation à chaud par des processus séparés (**documenté** : documentation d'Epic). |
| **Unity** | GPU Instancing, Frame Timing Manager | Instancing activé par matériau ; temps GPU par frame (**documenté** : manuel). |
| **Unreal** | `TextureGroup`, `r.MaxAnisotropy` | L'anisotropie se règle par groupe de textures, plafonnée par un réglage global (**documenté** : sources publiques). |
| **Unity** | Texture Importer, *Aniso Level* ; `QualitySettings.anisotropicFiltering` | Un niveau par texture, que les réglages de qualité peuvent forcer (**documenté** : manuel). |
| **Godot** | `MultiMeshInstance3D` | Un mesh dessiné en N exemplaires par un seul draw (**documenté** : docs officielles). |
| **Unity** | Texture Importer, *Texture Shape : Cube*, *Mapping : Latitude-Longitude Layout* | L'HDRI équirectangulaire convertie en cubemap à l'import (**documenté** : manuel). |
| **Godot** | `PanoramaSkyMaterial`, `Sky.radiance_size` | L'HDRI gardée en panorama, d'où Godot calcule une carte de radiance par niveaux de rugosité (**documenté** : docs officielles). |
| **Unreal** | `SkyLight`, *Source Type : SLS Specified Cubemap* | Une cubemap, importée depuis une HDRI, éclaire la scène (**documenté** : documentation d'Epic). |
