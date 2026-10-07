# ADR-0032 — Dear ImGui dans le moteur : le module `ui`

- **Statut** : accepté le 2026-10-07, sur les réponses de Donnovan au sondage du jour (le module, l'input, les
  fenêtres ancrées, le calque de la page web) ; relu par un subagent, dont la relecture a ajouté la souris
  possédée par `App`, l'ordre d'une image, les touches traduites, les textures d'ImGui 1.92 et les limites
  de WebGPU
- **Date** : 2026-10-07
- **Milestone** : M7.1

## Contexte

M7.1 ouvre la phase de l'éditeur : **Dear ImGui** rendu par NVRHI, et des panneaux de statistiques et de
profiling. *Critère* : coût de l'UI inférieur à 0,5 ms par image (ROADMAP). SPECS §6 choisit ImGui, **branche
docking**, avec ImGuizmo plus tard (M7.4) ; Donut fournit un renderer ImGui pour NVRHI, à lire et adapter.

Trois contraintes viennent d'ailleurs :

- l'ADR-0018 range ImGui dans le **moteur** (« communs à tous les jeux et à l'éditeur »), et JEU.md dessinera
  avec lui les cœurs et l'endurance de *Rando* (M8.2) ;
- SDL n'est visible que dans `platform/` (SPECS §7), et ses événements ne donnent aujourd'hui ni la position
  de la souris, ni la molette, ni le texte tapé : ImGui en a besoin ;
- la page web a son calque HTML des mesures (#294), lisible sur un téléphone et affiché avant WebGPU.

Les réponses de Donnovan au sondage, mot pour mot :

- le module : « Un module ui dans le moteur (Recommandé) » ;
- l'input : « platform s'enrichit, ui traduit (Recommandé) » ;
- les panneaux : « Plusieurs fenêtres ancrées » (plutôt qu'une seule fenêtre de statistiques) ;
- la page web : « On garde les deux (Recommandé) ».

## Options envisagées

**1. Où vit ImGui**

| Option | Pour | Contre |
|---|---|---|
| **A. Un module `engine/ui`** | Le sandbox, *Rando* (son HUD) et l'éditeur s'en servent ; c'est ce que dit l'ADR-0018 | Un module de plus |
| B. Dans `editor/` seulement (l'issue #251) | Rien de nouveau dans le moteur | Le HUD du jeu lierait la bibliothèque de l'éditeur |

**2. Comment ImGui reçoit l'input**

| Option | Pour | Contre |
|---|---|---|
| **A. `platform` donne la position, la molette et le texte ; `ui` traduit pour ImGui** | SDL reste dans `platform`, qui ne connaît pas ImGui | Une table des touches à écrire |
| B. Le backend SDL3 d'ImGui (`imgui_impl_sdl3`) dans `platform` | Moins de code | La couche la plus basse dépendrait de l'UI |

**3. Le rendu**

| Option | Pour | Contre |
|---|---|---|
| **A. Le renderer de Donut (`imgui_nvrhi.cpp`), adapté** | Une passe NVRHI comme les nôtres : même chemin sous Vulkan et WebGPU, chronométrée comme elles | Ses shaders, en HLSL, se réécrivent en Slang |
| B. Les backends d'ImGui (`imgui_impl_vulkan`, `imgui_impl_wgpu`) | Écrits par ImGui | Deux backends qui contournent NVRHI : un par API, hors de notre validation |

**4. Les panneaux** : une fenêtre de statistiques ; un calque permanent ; plusieurs fenêtres ancrées
(**choisies**).

**5. La page web** : garder le calque HTML et ImGui (**choisi**) ; ImGui seul.

## Décision

### Le module

**`engine/ui`**, au-dessus de `platform`, `gpu` et `render`, sous `app` (SPECS §7). Il ne voit pas flecs : les
panneaux, qui lisent `App`, vivent dans `app`.

- **ImGui 1.92 par vcpkg**, avec la fonctionnalité `docking-experimental` et **sans aucun de ses backends** :
  le rendu et l'input sont les nôtres.
- **Le rendu** : le renderer de Donut (`imgui_nvrhi.cpp`) adapté, son en-tête de licence MIT gardé avec celui
  d'ImGui. Sa classe devient des fonctions libres, comme nos passes (ADR-0011) : `createUiPass`,
  `recordUi(commandList, pass, drawData, target)`, `destroyUiTextures`. Ses deux shaders sont réécrits en Slang.
- **L'input** : `feedInput`, qui traduit les événements de `platform` pour ImGui.
- **La glu** : une `UiLayer` dans `App`, et un point d'accroche de plus, `FrameHooks::ui`, où le programme
  ajoute ses fenêtres. La `UiLayer` tient des ressources du GPU : elle est déclarée avant les points
  d'accroche et meurt avant le device (le piège de l'ordre, ADR-0029). Le contexte d'ImGui est global : un
  seul à la fois, comme la fenêtre de `platform`.

### Les textures d'ImGui 1.92

ImGui 1.92 rastérise ses polices **à la demande**, à la taille voulue : c'est ce qui rend l'UI nette sur un
écran dense. Il faut pour cela que le renderer gère les textures. `recordUi` déclare
`ImGuiBackendFlags_RendererHasTextures`, et traite `ImDrawData::Textures` avant ses dessins :

- `WantCreate` crée la texture NVRHI et donne son identifiant à ImGui (`SetTexID`) ;
- `WantUpdates` la renvoie **entière** : le `writeTexture` de NVRHI écrit une sous-ressource entière, sans
  rectangle, et c'est rare ;
- `WantDestroy` la libère, **avec son binding set**. Le cache de Donut, indexé par texture et jamais vidé,
  garderait sinon chaque atlas en vie.

À l'arrêt, `destroyUiTextures` libère celles qui restent, avant le device. Le chemin ancien de Donut
(`GetTexDataAsRGBA32`, un atlas cuit une fois) figerait les tailles de police.

**La police** est celle d'ImGui, ProggyClean, rastérisée à la taille de l'écran : nette mais carrée, et limitée
au Latin-1 (pas de « œ » ni de « — » dans les panneaux). Une vraie police, sous licence permissive, viendra avec
le HUD de *Rando* (M8.2).

### L'input

`platform` ajoute ce qu'il faut à ImGui, sans rien savoir de lui :

- la **position de la souris**, en pixels de l'image, densité comprise (comme `cursorPosition`) ;
- la **molette**, dans un événement à part : en axes 2 et 3 de la souris, elle changerait `MouseAxisCount`, qui
  dimensionne les tableaux d'`engine/input` ;
- le **texte tapé**, en UTF-8, dans une liste à part (`Events::text`), entre `startTextInput` et
  `stopTextInput` : SDL n'en envoie pas sans ;
- avec chaque touche, en plus du scancode, le **code de la touche selon la disposition du clavier** (un
  `uint32` : les `SDLK_` de SDL sont des points de code Unicode) et les **modificateurs** (Ctrl, Maj, Alt,
  Super) ;
- la **sortie de la souris** de la fenêtre et la **perte du focus**, sans quoi un survol resterait collé ;
- le **presse-papiers** (`clipboardText`, `setClipboardText`), sans quoi ImGui ne copierait qu'en lui-même.

`ui` traduit. **ImGui attend des touches traduites** : `ImGuiKey_A` est la touche qui tape « A ». La table
reprise de `imgui_impl_sdl3` (MIT) part donc du code de disposition, et non du scancode, comme ce backend : sur
l'AZERTY de Donnovan, Ctrl+Z est bien la touche marquée Z. Seul le pavé numérique part du scancode. Ces codes
sont des nombres : `ui` n'inclut pas SDL.

### L'ordre d'une image

Dans `runFrame` :

1. `pollEvents` ;
2. `feedInput` : les événements à ImGui ;
3. `NewFrame` : c'est lui qui traite ces événements, et après lui seulement `WantCaptureMouse` et
   `WantCaptureKeyboard` sont à jour ;
4. le filtre `uiTakesInput` (plus bas) ;
5. `input::updateInput`, puis `hooks.frame`, puis les pas de simulation ;
6. `hooks.ui` et les panneaux du moteur, puis `ImGui::Render` ;
7. `renderFrame` : la passe de l'UI après le tonemapping, sur l'image finale, avant la copie d'une capture.

### Ce que l'on voit

- **La couche `ui` tourne dès qu'un programme dessine avec elle** (le HUD de *Rando*, M8.2). **F1 montre et
  cache les panneaux de debug** du moteur, cachés au démarrage ; `--ui on` les ouvre dès le départ, pour la CI
  et les captures. F1 est consommé par l'UI : le jeu ne le voit pas.
- Les panneaux sont des **fenêtres ancrées** (branche docking) dans un espace qui couvre l'image et laisse voir
  la scène au centre. La disposition se construit par `DockBuilder`, une API interne d'ImGui
  (`imgui_internal.h`) qui peut changer d'une version à l'autre ; vcpkg la fige.
  - **Image** : images/s, temps CPU et GPU de l'image, leur courbe sur les dernières secondes ;
  - **Passes** : le temps GPU de chaque passe du renderer, et le coût de l'UI elle-même ;
  - **Scène** : les entités et les modèles ; le programme y ajoute les siens (*Rando* : le terrain, l'herbe,
    le renard et son état ; les corps physiques, que `app` ne voit pas).
- **Pas de fenêtres hors de la fenêtre principale** (les *viewports* multiples d'ImGui) : `platform` n'a
  qu'une fenêtre (son README).
- Les captures des CI, prises sans les panneaux, ne changent pas.

### La souris

Aujourd'hui, chaque programme capture la souris lui-même (le sandbox, *Rando*). Si la boucle la libérait
derrière eux pour l'UI, leur état deviendrait faux : *Rando* continuerait de tourner la caméra avec la
souris, et ne la reprendrait jamais à la fermeture. **`App` possède donc la capture** : le programme pose
`app.mouseCaptureWanted`, et la boucle seule appelle `setMouseCaptured(window, voulue && !panneauxOuverts)`
quand cette valeur change. Le programme lit `app.mouseCaptured`, l'état réel. Cela change le tableau des
points d'accroche de l'ADR-0029 (« *Rando* la capture de la souris »), et *Rando* suit à sa prochaine montée de
version du moteur.

### Les pièges, et leur nom

- **`uiTakesInput`** : quand ImGui veut la souris ou le clavier, le jeu ne les voit pas ; sinon, un clic dans
  une fenêtre tire aussi dans la scène, et taper un nombre fait marcher le renard. Le filtre ne retire que les
  appuis et les mouvements, **jamais un relâchement** : l'input du moteur garde l'état des touches tenues, et un
  relâchement perdu laisserait W enfoncé pour toujours. Quand ImGui prend le clavier ou la souris, tout ce qui
  était tenu est relâché.
- **`linearOnSrgbTarget`** : les couleurs d'ImGui sont en sRGB. Sur une cible sRGB, qui convertit en écrivant,
  elles seraient converties deux fois, et l'UI délavée. Le shader les linéarise quand la cible est sRGB, et
  les laisse sur une cible `UNORM`. Le format se lit sur la cible, pas sur la plateforme : la swapchain native
  est en `SBGRA8_UNORM`, son repli en `BGRA8_UNORM`, le rendu hors écran de `--gpu webgpu` en `SRGBA8_UNORM`,
  et un canevas de téléphone peut être en `RGBA8`. Le mélange se fait alors en espace linéaire : les fonds
  translucides et le bord des lettres diffèrent un peu du rendu habituel d'ImGui. La vérification porte sur
  un pixel **opaque** connu, dans une capture native et dans celle de `web-smoke`.
- **`clampScissorToTarget`** : les rectangles de découpe d'ImGui peuvent sortir de l'image ou être négatifs.
  Passés tels quels, ils sont une erreur de validation, sous WebGPU comme sous Vulkan (règle n°4). Chacun est
  borné à la cible, et un rectangle vide n'est pas dessiné.
- **Pas de push constants** : notre backend WebGPU ne les a pas, et Donut y passe la taille de l'image. Elle va
  dans un constant buffer écrit une fois par image, avec le drapeau sRGB.
- **L'échelle de l'écran** : sur un écran dense (un téléphone ×3), l'UI serait minuscule. La taille de la police
  et du style suit le facteur d'échelle de l'écran, que `platform` donne (`displayScale`, de
  `SDL_GetWindowDisplayScale`).
- **`imgui.ini`** : ImGui écrit par défaut sa disposition dans le dossier courant, et une session changerait
  l'image de la suivante. `io.IniFilename = nullptr` ; la disposition se reconstruit à chaque lancement.
- **Le texte tapé** n'existe qu'entre `startTextInput` et `stopTextInput` : `ui` les appelle selon
  `WantTextInput`, sinon un champ de texte ne recevrait rien, sans erreur. Que cela ouvre le clavier d'un
  téléphone sous Emscripten reste à vérifier.

### Le critère, mesuré

- **Le temps CPU de l'UI** est la somme de ses tranches : `feedInput`, `NewFrame`, `hooks.ui` et les panneaux,
  `ImGui::Render`, et l'enregistrement de sa passe. Pas l'image entière, qui contient la simulation et la
  scène.
- **Son temps GPU** est son minuteur NVRHI, sous Vulkan. Sous WebGPU, notre backend ne relit pas les minuteurs :
  le bilan dit « non mesuré », jamais 0.
- Les deux, en moyenne et au pire (un maximum gardé à côté du total), sont dans une ligne du bilan de fin :
  « ui : CPU … ms en moyenne, … au pire ; GPU … ms en moyenne, … au pire ».
- **Le critère** : sur la machine de référence, en Release, avec les trois fenêtres ouvertes
  (`levain_sandbox --view hike --ui on --seconds 10`), **chacun reste sous 0,5 ms** en moyenne.
- **La CI** (Vulkan, `--ui on`) échoue si l'UI n'a dessiné aucune commande, si son minuteur GPU n'a aucune
  mesure, ou s'il y a une erreur de validation. Elle ne juge pas le temps, que lavapipe ne mesure pas comme un
  GPU. La page web reçoit le temps CPU de l'UI, et `web-smoke` le vérifie avec `--ui on`.
- **`deps.imgui-visibility`** : un contrôle de configuration, sur le modèle de celui de flecs, échoue si un
  module sous `ui` inclut ImGui, ou si `ui` inclut SDL ; et bruyamment si un des dossiers qu'il surveille
  n'existe plus.

## Conséquences

- **`ui` est le premier module qui dessine une interface** : le HUD de *Rando* (M8.2) et l'éditeur (M7.2 à
  M7.6) s'en serviront sans rien redemander.
- **La capture de la souris passe des programmes à `App`** : le sandbox et *Rando* changent.
- **_Rando_ ajoute `imgui[docking-experimental]` à son `vcpkg.json`** à sa prochaine montée de version du
  moteur, dans la même PR : le contrôle de l'ADR-0018 (`CheckVcpkgManifest.cmake`) refuse sinon de configurer.
- La branche docking d'ImGui est marquée expérimentale ; vcpkg la fige à la version de sa baseline, comme le
  reste.
- Le calque HTML de la page web reste : il s'affiche avant WebGPU, et sur un téléphone, où F1 n'existe pas.

## Ce que font les autres moteurs

- **Unreal** : son interface est **Slate**, « a completely custom and platform agnostic user interface
  framework that is designed to make building the user interfaces for tools and applications such as Unreal
  Editor, or in-game user interfaces, fun and efficient » [1]. Le même cadre sert donc à l'éditeur et au jeu,
  rendu par le RHI : c'est la place qu'on donne à `ui`, au-dessus de NVRHI. Epic ne documente pas ImGui comme
  interface de l'éditeur.
- **Unity** : son **IMGUI** est un mode immédiat, comme ImGui : « a code-driven GUI system, and is mainly
  intended as a tool for programmers », pour « in-game debugging displays and tools », les inspecteurs et
  les fenêtres de l'éditeur [2]. Unity le dit « not generally intended to be used for normal in-game user
  interfaces » : un HUD fait avec ImGui est un choix d'outil, pas d'interface de jeu. Le nôtre sera simple (des
  cœurs, une jauge, un message), et une vraie UI de jeu reste en v2 (JEU.md).
- **Godot** : « The Godot editor runs on the game engine. It uses the engine's own UI system » [3]. L'éditeur
  est un programme du moteur, comme le nôtre (ADR-0018).

## Sources

1. Epic Games, *Slate Overview* — https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-overview-for-unreal-engine
2. Unity, *Immediate Mode GUI (IMGUI)* — https://docs.unity3d.com/Manual/GUIScriptingGuide.html
3. Godot, *Godot's design philosophy* — https://docs.godotengine.org/en/stable/getting_started/introduction/godot_design_philosophy.html
