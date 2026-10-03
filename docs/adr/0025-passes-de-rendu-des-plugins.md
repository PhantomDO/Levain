# ADR-0025 — Des passes de rendu venues d'un plugin

- **Statut** : proposé
- **Date** : 2026-10-03
- **Milestone** : M5.6

## Contexte

L'ADR-0018 range le terrain, l'eau et l'herbe dans des **plugins moteur**. Le terrain arrive en M5.6 : il lui
faut dessiner, avec le même éclairage que le reste de la scène (soleil et ses ombres, lumières en clusters,
ciel), et projeter son ombre. Or `render` ne connaît que ses propres passes, et **le moteur ne dépend jamais d'un
plugin**.

Aujourd'hui, `render` est une bibliothèque de passes (`createMeshPass`, `drawMesh`, `drawShadowCaster`,
`drawSky`, `tonemap`…), et c'est l'application, le sandbox ou le jeu, qui les enregistre dans l'ordre de l'image
(`renderFrame` du sandbox). Il n'y a pas de « renderer » qui posséderait l'image. Deux questions :

1. **Où s'insère une passe de plugin** dans l'image, et qui le décide ?
2. **Comment elle partage l'éclairage** : les ressources de l'image (constantes de la frame, lumières triées par
   cluster, atlas des ombres, environnement de l'IBL), qui vivent aujourd'hui dans `MeshPass`, et le code des
   shaders (la BRDF, la lecture des ombres et de l'IBL), qui vit dans `mesh.slang`.

## Options envisagées

**1. L'ordre des passes**

| Option | Pour | Contre |
|---|---|---|
| **A. Des passes explicites** : le plugin publie des fonctions (`terrain::drawTerrain`, `terrain::drawShadowCaster`), que l'application appelle dans `renderFrame`, là où elle les veut | Tout se lit dans une seule fonction, comme aujourd'hui ; aucun mécanisme à écrire ; c'est déjà ainsi que l'application appelle `render` | L'application doit savoir où placer chaque passe ; trois plugins, ce sont trois appels de plus dans le jeu |
| B. Un registre par étape : `render` définit des étapes (ombres, opaque, après le ciel…), et le plugin y inscrit une fonction à son import | Le jeu importe le plugin, et sa passe tourne ; le modèle d'Unity et de Godot | L'ordre réel se lit dans l'ordre des inscriptions, pas dans le code ; il faut d'abord un renderer qui possède l'image, qui n'existe pas ; un mécanisme pour un seul plugin |
| C. Des phases du pipeline de flecs : le plugin dessine dans un système, à une phase de rendu | Un plugin est déjà un module flecs ; l'ordre est celui des phases | L'enregistrement des commandes GPU entre dans les systèmes : la glu ECS ne tient plus en une ligne (ADR-0011) ; tout `renderFrame` devient un pipeline flecs |

**2. Le partage de l'éclairage**

| Option | Pour | Contre |
|---|---|---|
| **Les ressources de l'image sortent de `MeshPass`** dans une `FrameBindings` publique (layout et binding set de `space0`, constantes de scène), et le code d'éclairage des shaders dans `shaders/lighting.slang`, que tout shader inclut | Une passe de plugin éclaire exactement comme les meshes, par les mêmes fonctions ; la mesure contre Khronos (#229 à #231) vaut pour elle | Le layout de `space0` devient un contrat public : le changer touche les plugins |
| Chaque plugin recopie ce dont il a besoin | Aucune contrainte sur `render` | Trois copies de la BRDF, qui divergeront ; c'est ce que #231 vient de corriger entre l'IBL et la lumière directe |

## Décision

Proposée par Claude, à valider par Donnovan :

- **A : des passes explicites.** Le plugin publie ses passes en fonctions libres, comme `render` ; l'application
  les appelle dans `renderFrame`. `render` documente les emplacements : dans chaque cascade d'ombres, parmi les
  dessins opaques (avant le ciel, qui ne remplit que ce qui reste), et après les opaques pour ce qui se mélange
  (l'eau, en M5.7). Un registre (option B) attendra qu'un renderer possède l'image, et plusieurs plugins.
- **L'éclairage partagé** :
  - `FrameBindings` (`render/frame.hpp`) : le layout et le binding set de `space0` (ADR-0013), et le buffer des
    constantes de scène, créés une fois par `createFrameBindings(device, lights, shadows, environment)`.
    `MeshPass` et les passes des plugins prennent la même ;
  - `shaders/lighting.slang` : les bindings de `space0`, les constantes de la frame, et les fonctions
    d'éclairage (`brdf`, `sunVisibilityOf`, `environmentLightOf`, `clusterOf`), avec une fonction qui assemble
    tout, `shadeSurface`. `mesh.slang` l'inclut, et le shader du terrain aussi ;
  - `levain_add_shader` prend le dossier des shaders du moteur comme chemin d'inclusion, pour qu'un plugin compile
    ses shaders dans son propre dossier.
- **Le plugin** se crée par `levain_add_plugin` (ADR-0018). Ses données sont des composants de son module flecs ;
  ses passes lisent ces composants, et la glu tient en une ligne.

## Conséquences

- Une PR de refactorisation avant le terrain : `FrameBindings` et `lighting.slang`, sans changer un pixel (les
  tests de fumée et `tools/khronos-compare.sh` le vérifient).
- Le layout de `space0` et la signature de `shadeSurface` deviennent un **contrat** entre le moteur et ses
  plugins : les changer, c'est changer les plugins dans la même PR.
- Le jeu (*Rando*) appellera les passes du terrain, de l'eau et de l'herbe dans son `renderFrame` : quelques lignes,
  dans un ordre qui se lit. Le jour où un renderer du moteur possédera l'image, l'option B se construira au-dessus
  de ces mêmes fonctions, et un nouvel ADR remplacera celui-ci.
- Une passe de plugin ne vérifie pas son emplacement : un terrain dessiné après le ciel serait recouvert. Les
  tests de fumée du plugin le montreront.

## Ce que font les autres moteurs

- **Unreal** (documenté [1]) : une *Scene View Extension* (`ISceneViewExtension`) s'inscrit auprès du renderer et
  reçoit des appels à des moments fixes de l'image : notre option B. Le terrain (Landscape) n'en a pas besoin :
  il est dans le cœur du moteur (ADR-0018).
- **Unity** (documenté [2]) : dans l'URP, une *Scriptable Renderer Feature* ajoute des passes au renderer, chacune
  à un moment choisi de l'image : l'option B, avec un renderer qui possède l'image.
- **Godot** (documenté [3]) : le *Compositor* accepte des effets, des scripts appelés à un moment de l'image
  donné par leur `effect_callback_type` (par exemple après les objets transparents) : encore l'option B.

Les trois moteurs ont un renderer qui possède l'image, et y inscrivent des passes. Levain n'en a pas encore :
l'option A est l'étape d'avant, que les trois ont dépassée.

## Sources

1. Epic Games, *ISceneViewExtension* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ISceneViewExtension
2. Unity, *Introduction to Scriptable Renderer Features in URP* —
   https://docs.unity3d.com/6000.2/Documentation/Manual/urp/renderer-features/scriptable-renderer-features/intro-to-scriptable-renderer-features.html
3. Godot, *The Compositor* — https://docs.godotengine.org/en/stable/tutorials/rendering/compositor.html
