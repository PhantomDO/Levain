# Module `ui`

Dear ImGui dans le moteur (M7.1, [ADR-0032](../../docs/adr/0032-imgui.md)) : son contexte, son rendu par NVRHI,
et l'input venu de `platform`. Le sandbox, *Rando* (son HUD, M8.2) et l'éditeur (M7.2 à M7.6) s'en servent.

## Rôle

ImGui est une interface en **mode immédiat** : à chaque image, le code redécrit ce qu'il veut voir (« une
fenêtre, un bouton, une courbe »), et ImGui en tire des triangles, des rectangles de découpe et des textures.
Il ne garde pas d'arbre de widgets. Ce module fait le reste :

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
| [`include/levain/ui/context.hpp`](include/levain/ui/context.hpp) | `UiContext`, `createUiContext`, `prepareUiFrame`, `followTextInput` |

Les tests : `tests/ui_test.cpp` (le contexte).

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Slate, rendu par le RHI | Le même cadre pour l'éditeur et le jeu ; en mode retenu, avec un arbre de widgets (**documenté**, ADR-0032). |
| **Unity** | IMGUI (`OnGUI`), pour les outils | Un mode immédiat, comme ImGui ; déconseillé pour l'interface du joueur (**documenté**, ADR-0032). |
| **Godot** | Les nœuds `Control` | L'éditeur est un programme du moteur, avec sa propre UI (**documenté**, ADR-0032). |
