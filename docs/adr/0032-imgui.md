# ADR-0032 — Dear ImGui dans le moteur : le module `ui`

- **Statut** : accepté le 2026-10-07, sur les réponses de Donnovan au sondage du jour (le module, l'input, les
  fenêtres ancrées, le calque de la page web)
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

**`engine/ui`**, au-dessus de `platform`, `render` et `scene`, sous `app` (SPECS §6) :

- **ImGui 1.92 par vcpkg**, avec la fonctionnalité `docking-experimental` et **sans aucun de ses backends** :
  le rendu et l'input sont les nôtres ;
- **`UiPass`** : le renderer de Donut adapté (son en-tête de licence MIT gardé, avec celui d'ImGui), ses deux
  shaders en Slang. Le pipeline, l'atlas des polices, un buffer de sommets et d'index qui grandit au besoin,
  un ciseau par commande de dessin ;
- **`feedInput`** : les événements de `platform` traduits pour ImGui ;
- **la glu** : une `UiLayer` dans `App`, dont la boucle appelle `NewFrame` puis `Render` à chaque image, et un
  point d'accroche de plus, `FrameHooks::ui`, où le programme ajoute ses fenêtres.

### L'input

`platform` ajoute trois événements, sans rien savoir d'ImGui :

- la **position de la souris**, en pixels de la fenêtre ;
- la **molette** ;
- le **texte tapé**, en UTF-8, entre `startTextInput` et `stopTextInput` : SDL n'en envoie pas sans, et le
  clavier virtuel d'un téléphone s'ouvre avec.

`ui` les traduit. Ses codes de touche sont ceux de `platform`, c'est-à-dire les **scancodes USB HID** que SDL
reprend : la table des touches d'ImGui, adaptée de `imgui_impl_sdl3` (MIT), est une table de nombres, sans en-tête
SDL.

### L'image

L'UI se dessine **après le tonemapping**, sur l'image finale, dans la même command list, avec son minuteur GPU
comme chaque passe. Elle est **cachée au démarrage** : F1 l'ouvre et la ferme ; `--ui on` l'ouvre dès le
départ, pour la CI et les captures. Les captures des CI, prises sans elle, ne changent pas.

Les fenêtres sont **ancrées** (branche docking) dans un espace qui couvre l'image et laisse voir la scène
au centre :

- **Image** : images/s, temps CPU et GPU de l'image, leur courbe sur les dernières secondes ;
- **Passes** : le temps GPU de chaque passe du renderer (ombres, opaques, ciel, transparents, tonemapping) et
  de l'UI elle-même ;
- **Scène** : les entités, les modèles, les corps physiques ; le programme y ajoute les siens (*Rando* : le
  terrain, l'herbe, le renard et son état).

La disposition se recrée à chaque lancement (`DockBuilder`), sans fichier `imgui.ini` : un fichier écrit par
une session changerait l'image de la suivante.

### Les pièges, et leur nom

- **`uiTakesInput`** : quand ImGui veut la souris ou le clavier (`WantCaptureMouse`, `WantCaptureKeyboard`), le
  jeu ne les voit pas. Sinon, un clic dans une fenêtre tire aussi dans la scène, et taper un nombre fait
  marcher le renard. L'UI ouverte, la boucle **libère la souris** ; un jeu qui la capture (*Rando*) la
  reprend à la fermeture.
- **`linearOnSrgbTarget`** : les couleurs d'ImGui sont en sRGB. L'image finale est une cible sRGB, qui convertit
  en écrivant : la swapchain native (`SBGRA8_UNORM`) comme la page web (une vue sRGB du canevas). Écrites
  telles quelles, les couleurs seraient converties deux fois, et l'UI délavée. Le shader les linéarise quand
  la cible est sRGB, et les laisse sur une cible `UNORM` (le repli de la swapchain Vulkan). La vérification
  porte sur un pixel connu d'une capture, sous Vulkan et sous WebGPU.
- **L'échelle de l'écran** : sur un écran dense (un téléphone ×3), ImGui serait illisible. La taille des
  polices et du style suit le facteur d'échelle de l'écran, que `platform` donnera (`displayScale`, de
  `SDL_GetWindowDisplayScale`).
- **Le texte tapé** n'existe qu'entre `startTextInput` et `stopTextInput` : `ui` les appelle selon
  `WantTextInput`, sinon un champ de texte ne recevrait rien, sans erreur.

### Le critère, mesuré

- Le **temps CPU de l'UI** : de `NewFrame` à la fin de l'enregistrement de sa passe ;
- son **temps GPU** : son minuteur NVRHI.

Les deux, en moyenne et au pire, sont dans le bilan de fin du programme. Sur la machine de référence, avec les
trois fenêtres ouvertes (`levain_sandbox --view hike --ui on --seconds 10`), **chacun doit rester sous
0,5 ms**. La CI vérifie que la mesure existe, que l'UI s'est dessinée, et qu'elle a fait zéro erreur de
validation ; elle ne juge pas le temps, que lavapipe ne mesure pas comme un GPU. La page web l'affiche aussi
(`tools/web-smoke.mjs`).

## Conséquences

- **`ui` est le premier module qui dessine une interface** : le HUD de *Rando* (M8.2) et l'éditeur (M7.2 à
  M7.6) s'en serviront sans rien redemander.
- La branche docking d'ImGui est marquée expérimentale ; vcpkg la fige à la version de sa baseline, comme le
  reste.
- Le calque HTML de la page web reste : il s'affiche avant WebGPU, et sur un téléphone, où F1 n'existe pas.
- `--ui on` sert aussi à prendre une capture avec l'UI, pour la montrer.

## Ce que font les autres moteurs

- **Unreal** : son interface est **Slate**, « a completely custom and platform agnostic user interface
  framework that is designed to make building the user interfaces for tools and applications such as Unreal
  Editor, or in-game user interfaces, fun and efficient » [1]. Le même cadre sert donc à l'éditeur et au jeu,
  rendu par le RHI : c'est la place qu'on donne à `ui`, au-dessus de NVRHI. ImGui n'y arrive que par des
  plugins de la communauté.
- **Unity** : son **IMGUI** est un mode immédiat, comme ImGui : « a code-driven GUI system, and is mainly
  intended as a tool for programmers », pour « in-game debugging displays and tools », les inspecteurs et
  les fenêtres de l'éditeur [2]. Il déconseille d'en faire l'interface du joueur ; notre HUD de M8.2 restera
  simple.
- **Godot** : « The Godot editor runs on the game engine. It uses the engine's own UI system » [3]. L'éditeur
  est un programme du moteur, comme le nôtre (ADR-0018).

## Sources

1. Epic Games, *Slate Overview* — https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-overview-for-unreal-engine
2. Unity, *Immediate Mode GUI (IMGUI)* — https://docs.unity3d.com/Manual/GUIScriptingGuide.html
3. Godot, *Godot's design philosophy* — https://docs.godotengine.org/en/stable/getting_started/introduction/godot_design_philosophy.html
