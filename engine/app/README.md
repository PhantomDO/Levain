# `engine/app`

## Rôle

Le socle des programmes du moteur ([ADR-0029](../../docs/adr/0029-module-app.md)) : ce que le sandbox, *Rando*
et plus tard l'éditeur partagent, au lieu de le réécrire chacun. Il est au-dessus de tous les autres modules
(SPECS §7), et sous les programmes.

**État en M6.4 (en cours)** : ce qui était dans le sandbox et que tout programme réécrirait :

- **le cycle de vie** (`runApp`) : la fenêtre, le device, le renderer, le monde, la boucle native ou celle du
  navigateur, le compte rendu des images (le titre, le calque de la page web, Tracy), les bilans et la capture ;
- **les points d'accroche** (`FrameHooks`) que la fonction de démarrage du programme rend ;
- **les options communes** de la ligne de commande (`parseCommonOption`) ;
- **les modèles glTF sur le GPU** : leurs meshes, skinnés ou non, leurs textures, cuites si possible
  (ADR-0020), un binding set par matériau, l'animation des modèles skinnés (ADR-0022) ;
- le hot-reload des shaders (ADR-0014) et des textures (ADR-0021).

La suite arrive par la PR suivante de l'issue #300 : la caméra du rendu par `CameraLens`, l'input en singleton
et `loadModel`.

## Invariants

1. **Aucun plugin** : le module ne lie jamais un plugin (ADR-0018). C'est vérifié au configure par
   `levain_check_plugin_boundaries` (`cmake/LevainPlugin.cmake`), sur toutes les cibles sous `engine/`, et les
   tests `cmake.plugins.*` vérifient ce contrôle lui-même. Les plugins s'inscrivent dans le renderer que le
   programme crée, comme ils le font déjà (ADR-0025).
2. **Un modèle par asset** : `ModelGpu` est rangé par GUID. Un même modèle instancié deux fois partage ses
   meshes, ses textures et son animateur ; deux renards qui courent chacun à leur vitesse demanderont un
   animateur par instance.
3. **Le mouvement d'un modèle animé vient de l'appelant** (`MotionOf`) : le module ne sait pas ce qu'est un
   joueur. Le sandbox donne au renard celui du joueur, aux autres une vitesse de démonstration.
4. **L'état du programme meurt avant le device** : les points d'accroche le gardent dans leurs captures, et
   `FrameHooks` est le dernier champ d'`App`, donc le premier détruit, avant le renderer ; `runApp` détruit `App`
   avant le device.
5. **`App` ne bouge pas en mémoire** : il est sur le tas, créé une fois. Les fonctions d'étape et les systèmes
   du programme peuvent le garder par référence.
6. **Un appelant dont l'envoi échoue après `open()`** appelle `submitAbandonedUpload` avant de rendre l'erreur
   (voir « Pièges connus »).

## Pièges connus

- **Le temps où la fenêtre est cachée n'est pas une image** : dans le navigateur, un onglet caché n'est plus
  appelé, et la première image au retour durerait toute l'absence (`forgetHiddenTime`). En natif, la boucle
  attend les événements et remet son horloge à l'heure.
- **Dans le navigateur, `runApp` rend 0 aussitôt** : le device arrive plus tard (ADR-0023). Ce que la boucle
  utilise vit dans un état statique jusqu'à la fermeture de l'onglet, et un échec ne s'écrit que dans la console.

- **Une command list ouverte puis détruite fuit** jusqu'à la destruction du device, avec tout ce qu'elle a
  enregistré : `open()` l'inscrit dans les ressources de son propre command buffer (NVRHI,
  `vulkan-commandlist.cpp`), un cycle que seule la file rompt. Un envoi abandonné se ferme et se soumet quand
  même (`submitAbandonedUpload`, `.agents/skills/build/GOTCHA.md`).
- **Les données ne sont pas des couleurs** : la rugosité-métal et les normal maps se chargent en UNORM, les
  couleurs en sRGB. Une même image peut donc exister en deux textures (`TextureKey`).

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/app/app.hpp`](include/levain/app/app.hpp) | `AppSettings`, `ShaderBuild`, `parseCommonOption`, `OptionUse`, `parsePositive`, `parseVector` ; `App`, `FrameHooks`, `StartFunction`, `runApp` |
| [`include/levain/app/models.hpp`](include/levain/app/models.hpp) | `TextureKey`, `ModelPrimitiveGpu`, `ModelGpu` ; `textureLevelsOf`, `uploadModel`, `submitAbandonedUpload`, `bindModelMaterials` ; `isSkinned`, `clipIndexOf` ; `SkinningCost`, `maxJointSpeedOf`, `MotionOf`, `SkinningState`, `createSkinningState`, `animateModels` |
| [`include/levain/app/texture_reload.hpp`](include/levain/app/texture_reload.hpp) | `TextureReload`, `startTextureReload`, `reloadChangedTextures` : le hot-reload des textures |
| [`src/model_textures.hpp`](src/model_textures.hpp) | `textureTargetOf`, `UploadedTexture`, `uploadTexture` : une texture au format que le GPU échantillonne ; interne, partagé par l'envoi et le hot-reload |
| [`src/app.cpp`](src/app.cpp) | La fenêtre et le device, le ciel, `App`, une image, les bilans, la boucle du navigateur |
| [`src/shader_reload.hpp`](src/shader_reload.hpp) | Le hot-reload des shaders : relancer leur build, recréer les pipelines ; interne |
| [`src/models.cpp`](src/models.cpp) | L'envoi des meshes et des textures, les matériaux, l'animation |
| [`src/texture_reload.cpp`](src/texture_reload.cpp) | Les textures modifiées, rechargées, et les binding sets refaits |

## Équivalents ailleurs

| Moteur | Où | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `FEngineLoop` (module `Launch`), `UGameEngine` ; `UStaticMeshComponent`, `USkeletalMeshComponent` | La boucle du moteur, où le jeu se branche comme un module (**documenté**, ADR-0029) ; un modèle se dessine par le *render proxy* de son composant (supposé, d'après le code source). |
| **Unity** | `PlayerLoop` ; `MeshRenderer`, `SkinnedMeshRenderer` | La boucle du *Player*, que les scripts modifient (**documenté**, ADR-0029) ; un modèle se dessine par son renderer, skinné ou non. |
| **Godot** | `MainLoop`, `SceneTree` ; `MeshInstance3D`, `Skeleton3D` | La boucle par défaut d'un projet (**documenté**, ADR-0029) ; un modèle est un nœud, son squelette un autre. |
