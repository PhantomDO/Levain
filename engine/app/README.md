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
- **les modèles glTF** : `loadModel` les lit, les envoie au GPU (meshes skinnés ou non, textures cuites si
  possible, ADR-0020, un binding set par matériau) et les instancie ; l'étape « modèles » dessine toute entité
  qui porte un `MeshRef`, et `app` anime les modèles skinnés (ADR-0022) ;
- **la caméra du rendu** : l'unique entité qui porte un `CameraLens` ;
- **l'input du joueur en singleton du monde** (`PlayerInput`), avec les appuis qu'aucun pas n'a encore vus ;
- le hot-reload des shaders (ADR-0014) et des textures (ADR-0021). Pour les shaders, le programme relance
  `cmake --build --target levain_shaders` (`shaderReloadCommand`). Un exe Windows compilé dans une distro WSL le
  relance par `wsl.exe`, dans la distro d'où il vient (ADR-0035, décision 5) ; compilé ailleurs (l'exe de la CI), il
  refuse une fois dans le log, et tourne sans recharger ;
- **l'interface** (M7.1, ADR-0032) : ImGui, par le module `ui`, chaque image ; les panneaux de debug du moteur
  (Image, Passes, Scène, en fenêtres ancrées), que F1 ou `--ui on` ouvrent ; le point d'accroche `ui`, où le
  programme ajoute ses fenêtres ; et la capture de la souris, que `App` possède. La disposition des panneaux
  garde ses nœuds (`UiLayer::dock`), où l'éditeur ancre les siens (ADR-0034) ;
- **le banc d'essai** (ADR-0036) : `--input-script f` (ou `AppSettings::inputScript`, pour un test) rejoue les
  `platform::Events` d'un script dans la vraie boucle, hors écran. Voir l'invariant 12.

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
6. **Une seule caméra** : zéro ou plusieurs entités avec un `CameraLens` font échouer le démarrage, puis
   l'image où cela arrive, en les nommant (règle n°7).
7. **Un modèle skinné ne se charge qu'une fois** : l'animation est rangée par asset, et `loadModel` refuse un
   modèle skinné déjà chargé. Un modèle statique s'instancie de nouveau, sur les mêmes données GPU. Un nom
   d'entité déjà pris est refusé : la racine écraserait l'entité qui le porte.
8. **La souris se capture par `App`** : le programme pose `mouseCaptureWanted`, la boucle appelle
   `setMouseCaptured`, et le programme lit `mouseCaptured`. Les panneaux ouverts, elle n'est pas capturée.
9. **L'ordre d'une image** (ADR-0032) : les événements, ImGui (`NewFrame`), ce que l'UI garde pour elle
   (`gameInputOf`), l'input du jeu, `frame`, la souris, les pas, les fenêtres (`ui`), `ImGui::Render`, puis le
   rendu, l'UI après le tonemapping.
10. **Un appelant dont l'envoi échoue après `open()`** appelle `submitAbandonedUpload` avant de rendre l'erreur
   (voir « Pièges connus »).
11. **`CameraLens` est décrit pour l'éditeur** (ADR-0034, `describeAppComponents`, par `prepareAppWorld`, que
   `createApp` appelle et le test des composants décrits aussi), pas `PlayerInput`, qui porte un pointeur et
   des conteneurs et que `app` repose à chaque image. Son plan lointain a un plancher (0,02), et `cameraFrom`
   le ramène à deux fois le proche s'il ne le passe pas (`farBeyondNear`) : l'inspecteur laisse taper n'importe
   quel couple, et `far = near` donnerait un viewport d'une seule couleur, sans un mot.

12. **Un script d'input s'ajoute aux vrais événements, juste après `pollEvents`** : tout ce qui suit (ImGui,
   `gameInputOf`, `PlayerInput`, `frame`) ne sait pas d'où ils viennent. Sans `--steps` ni `--seconds`, le script
   mène la boucle, qui s'arrête après son dernier événement ; avec l'un d'eux, la première fin gagne, et un script
   que la boucle n'a pas fini de jouer fait échouer le programme (règle n°7) : un test ne passe pas sans avoir rejoué
   ce qu'il dit. **Pour un résultat qui se reproduise**, `--steps N` avec N au moins la longueur du script : sans lui,
   le monde avance du temps réel de chaque image. `inputScriptOf` lit le fichier avant la fenêtre ; un script
   illisible, refusé ou vide arrête `runApp`. Sans option, rien n'est touché. `tests/app_script_gpu.cpp` est le banc :
   un appui atteint `PlayerInput` par sa position, ImGui par sa lettre, du fichier comme de l'API.

## Pièges connus

- **Les appuis entre deux pas** : `input::actionPressed` ne vaut que pour l'image de l'appui. À 144 images/s, la
  plupart des images ne jouent aucun pas de simulation, et un système du pas fixe ne verrait jamais l'appui ;
  une image qui en joue deux le verrait deux fois. `PlayerInput` cumule donc les appuis jusqu'au prochain pas
  joué (`pressedSinceLastStep`), et les oublie à la fin de **chaque** pas, dans la phase `scene::EndOfStep`
  (`forgetPressesAtEachStep`) : les oublier après tous les pas de l'image ferait voir l'appui deux fois à une
  image qui en joue deux. C'est le piège de `Input.GetKeyDown` lu dans `FixedUpdate` chez Unity, dont la
  documentation dit : « Call this function from the MonoBehaviour.Update function, since the state gets reset
  each frame ».
- **Le matériau d'une primitive qui n'en a pas** est blanc, non métallique, assez rugueux (0,8), et non celui
  que la spécification glTF prévoit par défaut (métallique 1, rugosité 1) : un métal sombre rendrait ses
  formes illisibles, et c'est ce que le sandbox faisait avant (avec son damier).

- **Le temps où la fenêtre est cachée n'est pas une image** : dans le navigateur, un onglet caché n'est plus
  appelé, et la première image au retour durerait toute l'absence (`forgetHiddenTime`). En natif, la boucle
  attend les événements et remet son horloge à l'heure.
- **Dans le navigateur, `runApp` rend 0 aussitôt** : le device arrive plus tard (ADR-0023). Ce que la boucle
  utilise vit dans un état statique jusqu'à la fermeture de l'onglet, et un échec ne s'écrit que dans la console.
  `--capture` y est ignoré, avec un avertissement : la relecture de l'image demande d'attendre le GPU.
- **L'explorer de flecs s'ouvre dans tout programme construit en Debug**, sur `127.0.0.1:27750` : deux
  programmes lancés ensemble en Debug (le sandbox et *Rando*) se disputent ce port, et le second n'a pas
  d'explorer.
- **Une command list ouverte puis détruite fuit** jusqu'à la destruction du device, avec tout ce qu'elle a
  enregistré : `open()` l'inscrit dans les ressources de son propre command buffer (NVRHI,
  `vulkan-commandlist.cpp`), un cycle que seule la file rompt. Un envoi abandonné se ferme et se soumet quand
  même (`submitAbandonedUpload`, `.agents/skills/build/GOTCHA.md`).
- **Ce que l'UI garde, le jeu ne le voit pas** (`gameInputOf`) : un clic dans une fenêtre ne tire pas dans la
  scène. Mais un relâchement passe toujours, et ce qui était tenu est relâché quand l'UI prend l'appareil :
  l'input garde l'état des touches, et un relâchement perdu laisserait une touche enfoncée pour toujours.
- **Les données ne sont pas des couleurs** : la rugosité-métal et les normal maps se chargent en UNORM, les
  couleurs en sRGB. Une même image peut donc exister en deux textures (`TextureKey`).

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/app/app.hpp`](include/levain/app/app.hpp) | `AppSettings`, `ShaderBuild`, `ExeSystem`, `WslBuild`, `shaderReloadCommand`, `parseCommonOption`, `OptionUse`, `parsePositive`, `parseVector` ; `DrawCount`, `App` (dont l'étape « modèles » et ses compteurs), `FrameHooks` (dont `motionOf` et `ui`), `StartFunction`, `runApp` |
| [`include/levain/app/camera.hpp`](include/levain/app/camera.hpp) | `CameraLens`, `cameraFrom`, `renderCameraOf` : la caméra du rendu |
| [`include/levain/app/player_input.hpp`](include/levain/app/player_input.hpp) | `PlayerInput`, `takeFrameInput`, `forgetPresses`, `forgetPressesAtEachStep`, `pressedSinceLastStep` : l'input en singleton |
| [`include/levain/app/load_model.hpp`](include/levain/app/load_model.hpp) | `ModelLoad`, `LocomotionClips`, `LoadedModel`, `loadModel` : un glTF dans le monde et sur le GPU, en un appel |
| [`include/levain/app/models.hpp`](include/levain/app/models.hpp) | `TextureKey`, `ModelPrimitiveGpu`, `ModelGpu` ; `textureLevelsOf`, `uploadModel`, `submitAbandonedUpload`, `bindModelMaterials` ; `isSkinned`, `clipIndexOf` ; `SkinningCost`, `maxJointSpeedOf`, `MotionOf`, `SkinningState`, `createSkinningState`, `animateModels` |
| [`include/levain/app/ui_layer.hpp`](include/levain/app/ui_layer.hpp) | `UiLayer`, `UiCost`, `FrameHistory`, `PanelsKey`, `gameInputOf`, `mouseShouldBeCaptured`, `recordHistory`, `recordUiCpu` : l'interface dans la boucle |
| [`src/panels.hpp`](src/panels.hpp) | `drawEnginePanels` : les panneaux de debug du moteur, ancrés ; interne |
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
| **La caméra** | `CameraComponent` actif (Unreal), `Camera.main` (Unity), `Camera3D.current` (Godot) | Une caméra parmi d'autres est celle du rendu ; ici, il n'y en a qu'une, et deux sont une erreur. |
| **L'input** | `Input.GetKeyDown` (Unity), `Input.is_action_just_pressed` (Godot) | Un appui ne vaut que pour l'image : Unity dit de le lire dans `Update`, pas dans `FixedUpdate` (**documenté**, `Input.GetKeyDown`). `PlayerInput` le garde jusqu'au pas suivant. |
