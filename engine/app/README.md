# `engine/app`

## Rôle

Le socle des programmes du moteur ([ADR-0029](../../docs/adr/0029-module-app.md)) : ce que le sandbox, *Rando*
et plus tard l'éditeur partagent, au lieu de le réécrire chacun. Il est au-dessus de tous les autres modules
(SPECS §7), et sous les programmes.

**État après M7.7** : ce qui était dans le sandbox et que tout programme réécrirait, et, depuis M7.7, ce que
l'éditeur demande à la boucle (ADR-0036 ; les invariants 6, 8, 9, 12 à 16 et « Fermer, et `isScriptedRun` ») :

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
  garde ses nœuds (`UiLayer::dock`), où l'éditeur ancre les siens (ADR-0034). Sous un éditeur
  (`UiLayer::editorLayout`, posé par `withEditor`) la disposition est celle des colonnes et les fenêtres du moteur
  ont une croix (son menu « Fenêtre » les rouvre) ; sinon (le sandbox, *Rando*) c'est celle d'avant, sans croix.
  **Le style d'ImGui** a un seul propriétaire, `App` : un programme qui en règle un autre que celui de
  `createUiContext` le pose par `setUiStyle(app, fonction)` (l'éditeur : `ui::applyEditorStyle`), qui le règle à
  l'échelle de la fenêtre ; la boucle rappelle la fonction à chaque `WindowEventType::ScaleChanged` (le zoom du système
  change, ou la fenêtre passe sur un autre écran), avant `NewFrame` (`gpu.editor-modes.*`, ligne `window scale`).
  **Pas encore** : le style de `createUiContext` (le sandbox, *Rando*) ne suit pas un changement d'échelle, il reste à
  celle de l'ouverture ; et le changement reçu fenêtre masquée refait le style aussitôt (la boucle masquée lit les
  événements), sans test (le banc d'essai n'a pas d'événement « masquée », comme pour la croix) ;
- **la scène dans une texture** (M7.7, morceau 7, ADR-0036 décision 7) : `requestScene(app, taille)`, que la Vue appelle dans
  `ui`, rend l'identifiant de l'image à montrer par `ImGui::Image` ; la scène se rend alors dans cette texture, la
  swapchain est effacée et seule l'UI y est dessinée (`scene_view.hpp`). Voir l'invariant 15 ;
- **le banc d'essai** (ADR-0036) : `--input-script f` (ou `AppSettings::inputScript`, pour un test) rejoue les
  `platform::Events` d'un script dans la vraie boucle, hors écran (une vraie fenêtre sous Windows), à la place des vrais
  événements d'input. Voir l'invariant 12.

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
6. **Une seule caméra, sauf caméra imposée** : zéro ou plusieurs entités avec un `CameraLens` font échouer le
   démarrage, puis l'image où cela arrive, en les nommant (règle n°7). `App::cameraOverride` (la caméra de l'éditeur,
   qui n'est pas une entité, ADR-0036) les dispense : posée, `renderCameraOf` n'est pas consulté, `startApp`
   compris ; sans elle, rien ne change.
7. **Un modèle skinné ne se charge qu'une fois** : l'animation est rangée par asset, et `loadModel` refuse un
   modèle skinné déjà chargé. Un modèle statique s'instancie de nouveau, sur les mêmes données GPU. Un nom
   d'entité déjà pris est refusé : la racine écraserait l'entité qui le porte.
8. **La souris se capture par `App`** : le programme pose `mouseCaptureWanted`, la boucle appelle
   `setMouseCaptured`, et le programme lit `mouseCaptured`. Les panneaux ouverts, elle n'est pas capturée, **sauf si
   `mouseCaptureOverPanels` est posé** (`mouseShouldBeCaptured`) : le vol de la caméra de l'éditeur se fait dans la Vue,
   un panneau (ADR-0036, décision 8). `App::rawFrameEvents` garde les événements de l'image avant tout filtre, que
   l'éditeur lit pour ses gestes, et `App::frameSeconds` la durée que le monde compte (le pas fixe sous `--steps`).
9. **L'ordre d'une image** (ADR-0032, ADR-0036) : les événements, ImGui (`NewFrame`), ce que l'UI garde pour elle
   (`gameInputOf`), l'input du jeu, `frame`, la souris, les pas, les fenêtres (`ui`), `ImGui::Render`, **si `ui` l'a
   demandé (`recomposeAfterUi`) la recomposition des matrices monde puis la relecture de la caméra**, puis le rendu,
   l'UI après le tonemapping. La caméra est aussi relue après `ui`, sans recomposition, tant qu'elle est imposée
   (`cameraOverride` : l'éditeur la bouge dans `ui`) ou qu'elle l'était avant `ui` (qui l'a rendue aux entités). Le
   drapeau est remis à faux à chaque image : le sandbox et *Rando* ne le posent pas et
   ne paient rien (`tools/recompose-cost.sh` mesure ce que coûte une image qui le pose).
10. **Un appelant dont l'envoi échoue après `open()`** appelle `submitAbandonedUpload` avant de rendre l'erreur
   (voir « Pièges connus »).
11. **`CameraLens` est décrit pour l'éditeur** (ADR-0034, `describeAppComponents`, par `prepareAppWorld`, que
   `createApp` appelle et le test des composants décrits aussi), pas `PlayerInput`, qui porte un pointeur et
   des conteneurs et que `app` repose à chaque image. Son plan lointain a un plancher (0,02), et `cameraFrom`
   le ramène à deux fois le proche s'il ne le passe pas (`farBeyondNear`) : l'inspecteur laisse taper n'importe
   quel couple, et `far = near` donnerait un viewport d'une seule couleur, sans un mot.

12. **Un script d'input est la seule source d'input, juste après `pollEvents`** : des vrais événements, seuls ceux de la
   fenêtre restent (fermeture, signal, taille, échelle, masquée, montrée ; `platform::withoutRealInput`), puis viennent
   ceux du script, et tout ce qui suit (ImGui, `gameInputOf`, `PlayerInput`, `frame`) ne sait pas d'où ils viennent. Sur
   un vrai bureau (les tests Windows, où lavapipe n'a pas de surface hors écran), le vrai curseur n'est pas celui du
   script : la fin d'un vol le replace au point de l'appui (`mouseReturnTo`), le système le rapporte quand il veut, et
   sur le runner en Debug il revenait après le déplacement scripté suivant (`tests/app_script_gpu.cpp`, « le vrai
   curseur »). Sans `--steps` ni `--seconds`, le script mène la boucle, qui s'arrête après son dernier événement ; avec
   l'un d'eux, la première fin gagne, et un script que la boucle n'a pas fini de jouer fait échouer le programme (règle
   n°7) : un test ne passe pas sans avoir rejoué ce qu'il dit. **Pour un résultat qui se reproduise**, `--steps N` avec
   N au moins la longueur du script : sans lui, le monde avance du temps réel de chaque image. `inputScriptOf` lit le
   fichier avant la fenêtre ; un script illisible, refusé ou vide arrête `runApp`. Sans option, rien n'est touché.
   `tests/app_script_gpu.cpp` est le banc : un appui atteint `PlayerInput` par sa position, ImGui par sa lettre, du
   fichier comme de l'API. **ImGui compte le temps d'une exécution scriptée en pas fixes** (`uiFrameSecondsFor` : le 1/60 s
   que `--steps` donne déjà au monde, non le temps mesuré) : un double-clic (0,30 s), la répétition d'une touche et les délais
   de survol se jugent sur l'horloge d'ImGui, qui avance de `DeltaTime`, et sous charge (ASan) deux clics scriptés sur des
   images voisines tombaient à plus de 0,30 s l'un de l'autre (`gpu.editor-gestures.vulkan`). `tests/app_script_gpu.cpp` joue
   des images de 350 ms : un double-clic quand même. Interactif, le temps mesuré. **Le panneau « Image » ne lit donc plus `io.Framerate`**
   (toujours 60 sous un script) : il montre la moyenne des images mesurées de l'historique (`averageFrameMs`). Pour un script, un
   double-clic veut deux appuis à moins de 18 images l'un de l'autre, la répétition d'une touche tenue commence après 17 images.

13. **La simulation s'arrête par `App::simulationPaused`** (ADR-0036), lu une fois par image, avant `advanceWorld`
   (un arrêt posé dans `ui` vaut pour l'image suivante, simulation et squelettes), `startApp` compris : aucun pas,
   `RenderAlpha` à 1, et l'accumulateur de `fixedStep` ne reçoit pas le temps de l'image, sinon le retour au jeu
   jouerait une rafale. `progress` tourne encore, et `--steps` compte des images. **Les squelettes s'arrêtent avec
   elle** : `animateModels` et `motionOf` reçoivent le temps de la scène moins le temps passé à l'arrêt
   (`advanceAnimationClock`), qui repart d'où il s'était arrêté ; `--time` le fige comme avant, à T même si le
   programme démarre à l'arrêt. L'eau, l'herbe, les
   matériaux et `record` gardent le temps de la scène, **sauf `App::realTime` décoché** (« Temps réel » de l'éditeur,
   décision 9) : une seconde `AnimationClock` (`Loop::realTimeClock`) fige alors ce temps pour tous, squelettes compris,
   et le rend sans saut (`gpu.editor-menus.*`). `tests/app_loop_gpu.cpp` joue l'arrêt, l'horloge, la
   recomposition et la caméra imposée dans la vraie boucle. **`App::singleStep`** (« Image suivante », ADR-0037) joue
   exactement un pas alors que `simulationPaused` tient la simulation arrêtée : le même pas que `--steps` compte
   (`planFrame`, `singleStepPlan`), l'accumulateur intact, `RenderAlpha` à 1, les squelettes avancent d'une image.
   **La boucle le remet à faux à chaque image** (`std::exchange`) : posé en marche il ne vaut rien et ne se garde pas pour
   l'arrêt suivant ; posé par `frame` il joue à la même image, par `ui` à la suivante.

14. **L'input a une route** (`App::inputRoute`, ADR-0036, décision 4), lue au début de chaque image par
   `devicesTakenBy` (les appareils que l'UI prend, donc que `gameInputOf` retire au jeu) et `uiEventsOf` (ce qu'ImGui
   reçoit). `Ui`, par défaut : le filtre de M7.1, le sandbox et *Rando* n'y changent rien. `Editor` : rien n'arrive
   au jeu (souris, clavier, manette), et ce qu'il tenait est relâché. `Game` : le clavier va au jeu, ImGui n'en
   recevant que les relâchements et F1 ; **souris capturée** (le vol), quelle que soit la route, ImGui ne reçoit ni
   mouvement, ni molette, ni appui, mais **toujours les relâchements** (`mouseTakenByCapture`) : sans eux, le bouton
   droit qui a pris la capture resterait enfoncé pour ImGui, et le menu du clic droit ne s'ouvrirait plus après un
   vol (`gpu.editor-shortcuts.*`) ; **ni le texte tapé**, pour que « zqsd » en vol n'aille pas dans le champ qui était actif. **Fait au morceau 11, côté éditeur** : ce relâchement, sans les mouvements d'avant, est un *clic* pour ImGui ; la Vue n'ouvre son menu du clic droit que si le clic n'a pas volé (editor/README.md, invariant 20) ; `gpu.editor-shortcuts.*` épingle le comportement d'`app` seul. `App::keymapChanges` compte aussi les changements de disposition reçus fenêtre masquée. F1 ouvre les panneaux **sans modificateur** (`togglesPanels`), sauf si un client pose `UiLayer::panelsKeyBlocked` (l'éditeur, pendant la saisie d'un raccourci, où F1 est une touche refusée) : Maj+F1 est « rendre
   la souris » ; la souris va au jeu **dans `App::sceneRect` ou capturée, qu'ImGui la veuille
   ou non** (la Vue sera une fenêtre d'ImGui, que le survol met à `WantCaptureMouse`). **L'éditeur la pose à la fin de
   l'image N pour l'image N+1** (survol et focus ne se savent que dans `ui`) : le clic qui donne le focus est lu sous
   l'ancienne route. Avec la Vue, une fenêtre d'ImGui (morceaux 7 et 8), il n'atteindra pas le jeu ; avec le trou du
   docking d'aujourd'hui, qui n'est pas une fenêtre, `WantCaptureMouse` est faux au-dessus de lui et la route *UI* le
   laisse passer au jeu. `sceneRect` est refait à chaque image avant `ui` : la fenêtre entière, panneaux fermés ; le trou du nœud
   central de la disposition, panneaux ouverts **tant que la scène est derrière lui** (`sceneRectOf`, `sceneBehind` :
   un nœud central où une fenêtre est ancrée, ou que la scène n'occupe pas parce qu'elle est dans la texture de la Vue
   ou nulle part, n'est pas un trou : rectangle vide, la souris n'est pas au jeu) ; **la Vue de l'éditeur (morceau 8)
   y pose ensuite le rectangle de son image**. La position de la souris est celle d'ImGui : avant tout mouvement,
   nulle part, et sous la route *jeu* aucun clic n'arrive. **`App::cursor`** (morceau 8) est cette position, ou celle de
   SDL tant qu'ImGui n'en connaît pas, posée avant `frame` : le picking du sandbox la lit (`scenePixelAt` la ramène au
   coin de la Vue et à sa taille, ou à la fenêtre quand la scène y est) plutôt que `platform::cursorPosition`, qu'un
   script d'input ne déplace pas.

15. **La scène va dans la fenêtre, dans la texture de la Vue, ou nulle part** (`SceneOutput`, ADR-0036, décision 7),
   selon ce que `ui` a demandé cette image par `requestScene`. **Rien demandé : la fenêtre**, le rendu d'avant la Vue,
   que le sandbox, *Rando* et l'éditeur panneaux fermés (F1) gardent sans rien savoir. **Une taille : la texture**, au
   format de la swapchain (le tonemapping y encode en sRGB, l'UI la montre telle quelle : la conversion ne se fait qu'une fois), cible de
   rendu au repos en `ShaderResource` et avec une valeur d'effacement, défensive (Direct3D 12, message 820 ; le 821 est
   celui de la profondeur) ; la
   swapchain est effacée (`gpu::PresentClearLevel`), et seule l'UI la dessine. **0 × 0, ou aucune texture : nulle part** (la
   Vue fermée) : aucune scène, la swapchain effacée quand même, sans quoi l'image présentée n'aurait jamais été
   écrite (la couche de validation de Vulkan refuse de présenter une image sans disposition). Une Vue fermée demande
   donc 0 × 0 plutôt que de se taire, **à chaque image où elle n'est pas montrée** (fermée, derrière un onglet,
   repliée : `ImGui::Begin` rend faux). C'est un écart assumé avec « rien demandé panneaux ouverts = nulle part » :
   le moteur ne sait pas si un programme est un éditeur, et le sandbox ne demande rien. Une Vue qui se tairait
   ferait revenir la scène dans la fenêtre, derrière les panneaux, et chaque bascule changerait la taille de l'image HDR
   et de la profondeur (un `waitForIdle` chacune) : l'exigence est au morceau 8 (ADR-0036, tableau des morceaux). **La texture ne se recrée qu'une fois la taille demandée stable**
   (`StableFramesBeforeResize` images de suite, `shouldRecreateScene`) : à chaque taille, l'image HDR et la profondeur du
   renderer (`ensureHdrTarget`, `ensureDepthTexture`) se recréent avec elle, et un glissé de bord changerait la
   taille à chaque image. La première taille crée la texture tout de suite ; en attendant, l'ancienne est montrée
   étirée (`ImGui::Image` à la taille demandée). La nouvelle existe quand `ImGui::Image` la reçoit ; **l'ancienne
   n'est libérée qu'après l'image** (`releaseRetiredScenes`, une fois la command list enregistrée) : NVRHI la garde
   vivante tant qu'une command list en vol tient son framebuffer, sa texture suivie ou son binding set (sources de
   NVRHI du port : `vulkan-graphics.cpp:509`, `vulkan-state-tracking.cpp:304`, `vulkan-resource-bindings.cpp:966` ;
   `d3d12-state-tracking.cpp:236`, `d3d12-resource-bindings.cpp:1003`). `SceneView::created` compte les textures nées, ce
   que lit le test des « au plus 2 » d'un glissé. **Les panneaux lisent la sortie de l'image d'avant** : l'espace
   d'ancrage est soumis avant `ui`, où la Vue demande, et perd `PassthruCentralNode` (`dockspaceFlagsFor`) une image
   après que la scène a quitté la fenêtre, comme la route de l'input (invariant 14). `tests/scene_view_gpu.cpp`
   est le banc (`gpu.scene-view.*`).
16. **Les aides de l'éditeur : un drapeau, `App::editorHelpersVisible`** (ADR-0036, décision 15, morceau 17), vrai au
   départ. L'éditeur le pose à la fin de `ui` : faux en vue de jeu (touche G) et en Play, vrai sinon. Les étapes du
   rendu qui dessinent une aide (le contour de la sélection du bac à sable, la grille de l'éditeur) le lisent et ne
   dessinent rien s'il est faux. C'est tout ce que le moteur sait de l'éditeur : un programme sans éditeur ne le pose
   jamais et garde ses aides. Équivalent : le *Game View* d'Unreal (touche G), qui cache de même ce que le jeu ne
   montre pas.

## Fermer, et `isScriptedRun` (ADR-0036, décisions 10 et 11)

`isScriptedRun(settings)` est vrai sous `--seconds`, `--steps`, `--time`, `--capture` ou `--input-script` : ni préférences
lues ni écrites (morceau 15, `editor/README.md` point 20), et la croix ferme tout de suite. `AppSettings::testInteractive`
le rend faux pour un test du banc d'essai qui éprouve ce qu'un humain déclenche (aucune option de ligne de commande ne le
pose) ; un test qui éprouve les préférences injecte aussi un dossier jetable (`EditorOptions::preferencesDirectory`) ; sans lui,
il n'en a pas (`preferencesFileFor` : jamais le dossier de qui lance le test).
`resetEditorLayout(app)` rebâtit la disposition par défaut de l'éditeur sur l'espace d'ancrage (« Réinitialiser la
disposition ») et rouvre les fenêtres du moteur ; `mainDockspaceId()` est l'identifiant de cet espace. **La croix**
(`CloseRequested`) passe par `FrameHooks::closeRequested` : `true` veut dire « je demande d'abord » (la boucle ne
s'arrête pas, le programme pose `App::quitRequested` s'il quitte) ; il n'est jamais appelé sous `isScriptedRun`. **Un
signal** (`QuitSignal`) arrête toujours, sans demander. Le titre de la fenêtre passe par `platform::asciiTitleOf`.
Une boucle fermée avant la fin de ses `--steps` ou de son script sort en erreur (règle n°7) : un test qui ferme en
attend le code 1.

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
- **Ce que l'UI garde, le jeu ne le voit pas** (`gameInputOf`, route `Ui`) : un clic dans une fenêtre ne tire pas dans la
  scène. Mais un relâchement passe toujours, et ce qui était tenu est relâché quand l'UI prend l'appareil :
  l'input garde l'état des touches, et un relâchement perdu laisserait une touche enfoncée pour toujours.
- **Les données ne sont pas des couleurs** : la rugosité-métal et les normal maps se chargent en UNORM, les
  couleurs en sRGB. Une même image peut donc exister en deux textures (`TextureKey`).

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/app/app.hpp`](include/levain/app/app.hpp) | `App::simulationPaused`, `singleStep`, `realTime`, `stepsPlayed`, `recomposeAfterUi`, `cameraOverride`, `editorHelpersVisible`, `inputRoute`, `sceneRect`, `cursor`, `rawFrameEvents`, `frameSeconds`, `mouseCaptureOverPanels`, `mouseReturnTo`, `sceneView`, `requestScene`, `keyAtPosition`, `keymapChanges` (ADR-0036) ; `AppSettings`, `ShaderBuild`, `ExeSystem`, `WslBuild`, `shaderReloadCommand`, `parseCommonOption`, `OptionUse`, `parsePositive`, `parseVector` ; `DrawCount`, `App` (dont l'étape « modèles » et ses compteurs), `FrameHooks` (dont `motionOf` et `ui`), `StartFunction`, `setUiStyle`, `runApp` |
| [`include/levain/app/camera.hpp`](include/levain/app/camera.hpp) | `CameraLens`, `cameraFrom`, `renderCameraOf`, `renderCameraOr` (la caméra imposée d'abord) : la caméra du rendu |
| [`include/levain/app/player_input.hpp`](include/levain/app/player_input.hpp) | `PlayerInput`, `takeFrameInput`, `forgetPresses`, `forgetPressesAtEachStep`, `pressedSinceLastStep` : l'input en singleton |
| [`include/levain/app/load_model.hpp`](include/levain/app/load_model.hpp) | `ModelLoad`, `LocomotionClips`, `LoadedModel`, `loadModel` : un glTF dans le monde et sur le GPU, en un appel |
| [`include/levain/app/models.hpp`](include/levain/app/models.hpp) | `TextureKey`, `ModelPrimitiveGpu`, `ModelGpu` ; `textureLevelsOf`, `uploadModel`, `submitAbandonedUpload`, `bindModelMaterials` ; `isSkinned`, `clipIndexOf` ; `SkinningCost`, `maxJointSpeedOf`, `MotionOf`, `AnimationClock`, `advanceAnimationClock`, `AnimationMemo`, `rememberAnimation`, `restoreAnimation` (Play les garde, Stop les rend : ADR-0037), `SkinningState`, `createSkinningState`, `animateModels` |
| [`include/levain/app/scene_view.hpp`](include/levain/app/scene_view.hpp) | `SceneSize`, `SceneOutput`, `StableFramesBeforeResize`, `SizeSettling`, `shouldRecreateScene`, `sceneOutputFor`, `dockspaceFlagsFor`, `ScenePixel`, `scenePixelAt`, `SceneExtent`, `sceneExtentOf`, `SceneView` ; `createSceneTexture`, `requestSceneTexture`, `beginSceneRequest`, `settleSceneOutput`, `releaseRetiredScenes`, `clearSwapchain` : la scène dans la texture de la Vue |
| [`include/levain/app/ui_layer.hpp`](include/levain/app/ui_layer.hpp) | `UiLayer`, `UiStyleFunction`, `UiCost`, `FrameHistory`, `PanelsKey`, `InputRoute`, `ScreenRect`, `rectContains`, `DevicesTaken`, `RouteFacts`, `devicesTakenBy`, `gameInputOf`, `uiEventsOf`, `mouseTakenByCapture`, `togglesPanels`, `mouseShouldBeCaptured`, `uiFrameSecondsFor`, `averageFrameMs`, `recordHistory`, `recordUiCpu` : l'interface dans la boucle |
| [`src/panels.hpp`](src/panels.hpp) | `drawEnginePanels` : les panneaux de debug du moteur, ancrés ; `buildLayout` et les clés de leurs fenêtres (`ImageWindow`…) pour `ui_i18n_test.cpp` ; interne |
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
| **L'arrêt** | `SetGamePaused` (Unreal), `Time.timeScale = 0` (Unity), `SceneTree.paused` (Godot) | Arrêter la simulation sans arrêter le rendu (**documenté**) ; ici un drapeau d'`App`, et les squelettes suivent la simulation, l'eau non. La caméra de l'éditeur, hors de la scène, est celle de la vue de niveau chez Unreal (**supposé**). |
| **La scène dans une texture** | `USceneCaptureComponent2D` et `UTextureRenderTarget2D` (Unreal), `Camera.targetTexture` (Unity), `SubViewport` et `ViewportTexture` (Godot) | Rendre une caméra dans une texture que l'UI affiche (**documenté** pour chacun). Les vues de l'éditeur d'Unreal et d'Unity ne passent pas par ces API publiques : ce qu'elles font à la taille d'une fenêtre qu'on glisse, **non vérifié**. Ici, la taille ne se reprend qu'une fois stable. |
| **L'input** | `Input.GetKeyDown` (Unity), `Input.is_action_just_pressed` (Godot) | Un appui ne vaut que pour l'image : Unity dit de le lire dans `Update`, pas dans `FixedUpdate` (**documenté**, `Input.GetKeyDown`). `PlayerInput` le garde jusqu'au pas suivant. |
