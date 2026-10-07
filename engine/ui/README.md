# Module `ui`

Dear ImGui dans le moteur (M7.1, [ADR-0032](../../docs/adr/0032-imgui.md)) : son contexte, son rendu par NVRHI,
et l'input venu de `platform`. Le sandbox, *Rando* (son HUD, M8.2) et l'éditeur (M7.2 à M7.6) s'en servent.

## Rôle

ImGui est une interface en **mode immédiat** : à chaque image, le code redécrit ce qu'il veut voir (« une
fenêtre, un bouton, une courbe »), et ImGui en tire des triangles, des rectangles de découpe et des textures.
Il ne garde pas d'arbre de widgets. Ce module fait le reste :

- **le rendu** (`ui_pass.hpp`) : une passe NVRHI de plus, sous Vulkan comme sous WebGPU, adaptée du renderer
  de Donut ;
- **l'input** (`input.hpp`) : les événements de `platform` traduits pour ImGui ;
- **le contexte** (`context.hpp`) : créé et réglé pour le moteur.

La boucle (`app`, ADR-0029) décide de l'ordre d'une image et de ce que l'UI garde pour elle.

## Invariants

- **Aucun backend d'ImGui** : ni `imgui_impl_vulkan`, ni `imgui_impl_wgpu`, ni `imgui_impl_sdl3`. Le rendu
  passe par NVRHI, l'input par `platform`.
- **`ui` n'inclut pas SDL**, et rien sous `ui` n'inclut ImGui (`deps.imgui-visibility`).
- **Un contexte à la fois** : celui d'ImGui est global, comme la fenêtre de `platform`.
- **Les textures d'ImGui 1.92 sont les nôtres** (`ImGuiBackendFlags_RendererHasTextures`) : ImGui rastérise
  ses polices à la taille de l'écran, et demande de créer, mettre à jour ou détruire leurs textures.
- **Pas d'`imgui.ini`** : la disposition se reconstruit à chaque lancement.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/ui/ui_pass.hpp`](include/levain/ui/ui_pass.hpp) | `UiPass`, `createUiPass`, `recordUi`, `updateUiTextures`, `destroyUiTextures` ; les pièges `linearOnSrgbTarget` et `clampScissorToTarget` |
| [`include/levain/ui/input.hpp`](include/levain/ui/input.hpp) | `feedInput`, `imguiKeyOf`, `PressedKeys` |
| [`include/levain/ui/context.hpp`](include/levain/ui/context.hpp) | `UiContext`, `createUiContext`, `prepareUiFrame`, `followTextInput` |

Les tests : `tests/ui_test.cpp` (le contexte, les touches, l'input, la découpe, le choix sRGB), et `levain_ui_gpu [vulkan|webgpu]`
(`gpu.ui.*` dans ctest), qui relit la couleur d'un rectangle dessiné dans une cible sRGB puis UNORM.

## Pièges connus

- **Les couleurs d'ImGui sont en sRGB** (`linearOnSrgbTarget`). Une cible sRGB convertit en écrivant : sans
  linéarisation dans le shader, l'UI serait convertie deux fois, et délavée. Relu par `levain_ui_gpu` : 72
  niveaux d'écart sans la linéarisation.
- **Les découpes d'ImGui sortent de l'image** (`clampScissorToTarget`) : passées telles quelles, c'est une
  erreur de validation.
- **Pas de push constants** : notre backend WebGPU ne les a pas. La taille de l'image et le drapeau sRGB
  passent par un constant buffer.
- **ImGui attend des touches traduites** (`imguiKeyOf`) : `ImGuiKey_A` est la touche qui tape « A ». Sur un
  AZERTY, la partir du scancode mettrait Ctrl+Z sur la touche W.
- **Dans le navigateur, le keycode est modifié par Maj** : appuyer sur A, puis Maj, puis relâcher A donne « a »
  puis « A ». `feedInput` retient la touche d'ImGui envoyée à l'appui de chaque scancode (`PressedKeys`), et
  relâche celle-là : sinon, `ImGuiKey_A` resterait enfoncée.
- **Écrire dans un buffer arrondit à 4 octets** sous Vulkan (`vkCmdUpdateBuffer`) : un nombre impair d'index
  sur 16 bits ferait lire après la fin des données. `recordUi` en ajoute un, et arrondit la taille de ses
  buffers ; le test GPU dessine un triangle lissé (21 index), et ASan le vérifie en CI.
- **ImGui étale les événements sur plusieurs images** quand il en arrive beaucoup à la fois
  (`ConfigInputTrickleEventQueue`) : un clic très court reste visible au moins une image. Un test qui envoie
  plusieurs événements doit jouer plusieurs images.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Slate, rendu par le RHI | Le même cadre pour l'éditeur et le jeu ; en mode retenu, avec un arbre de widgets (**documenté**, ADR-0032). |
| **Unity** | IMGUI (`OnGUI`), pour les outils | Un mode immédiat, comme ImGui ; déconseillé pour l'interface du joueur (**documenté**, ADR-0032). |
| **Godot** | Les nœuds `Control` | L'éditeur est un programme du moteur, avec sa propre UI (**documenté**, ADR-0032). |
