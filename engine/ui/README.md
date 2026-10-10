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
- **Une texture que l'UI ne possède pas passe par `registerUiTexture`** (la scène de la Vue, ADR-0036) : un
  identifiant pour `ImGui::Image`, un binding set fait comme celui de l'atlas, et `releaseUiTexture` pour le
  retirer. La table ne rend **jamais** un identifiant, libéré ou non : un `ImDrawList` périmé ne montre jamais une
  autre texture que la sienne. Dessiner un identifiant inconnu ou libéré est l'assertion de `recordUi` en Debug.
  Libérer deux fois, ou un identifiant inconnu ou d'ImGui, est une assertion en Debug et une erreur au journal en
  Release. Le shader lit un `Texture2D<float4>` par un échantillonneur qui filtre : `registerUiTexture` refuse, par
  une erreur et non une assertion (`uiTextureRefusal`), une texture nulle, non 2D, multi-échantillon, sans usage de
  shader, de profondeur, d'un format entier ou de flottants sur 32 bits (WebGPU ne les filtre pas).
- **Pas d'`imgui.ini`** : la disposition se reconstruit à chaque lancement.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/ui/ui_pass.hpp`](include/levain/ui/ui_pass.hpp) | `UiPass`, `createUiPass`, `recordUi`, `updateUiTextures`, `destroyUiTextures`, `registerUiTexture`, `releaseUiTexture` ; les pièges `linearOnSrgbTarget`, `clampScissorToTarget` et `uiTextureRefusal` |
| [`include/levain/ui/input.hpp`](include/levain/ui/input.hpp) | `feedInput`, `imguiKeyOf`, `PressedKeys` |
| [`include/levain/ui/context.hpp`](include/levain/ui/context.hpp) | `UiContext`, `createUiContext`, `prepareUiFrame`, `followTextInput` |
| [`include/levain/ui/tr.hpp`](include/levain/ui/tr.hpp) | `tr`, `trf`, `textf`, `labelOf` : le catalogue des textes vu de l'interface, la table est dans `core` (ADR-0036) |

Les tests : `tests/ui_test.cpp` (le contexte, les touches, l'input, la découpe, le choix sRGB, les textures refusées), et
`levain_ui_gpu [vulkan|d3d12|d3d12-warp|webgpu]` (`gpu.ui.*` dans ctest, WARP compris), qui relit la couleur d'un rectangle
dessiné dans une cible sRGB puis UNORM, puis la table des identifiants.

## Pièges connus

- **Traduire un titre sans identifiant fait perdre sa fenêtre à la disposition.** ImGui ne hache que ce qui suit « ### »
  (`ImHashStr`, imgui.cpp) : « Image » et « Picture###Image » ont le même identifiant, que `labelOf` écrit. Un test qui
  bâtit et ouvre dans la même langue passe sans « ### » : `ui_i18n_test.cpp` bâtit en français, ouvre en anglais. Un texte
  traduit n'est jamais un format d'ImGui (`textf`, `TextUnformatted`). `i18n.untranslated` (`tests/check_untranslated.py`)
  refuse, sous `editor/`, `engine/app/` et `engine/ui/`, tout littéral d'un appel `ImGui::` hors du catalogue (un
  identifiant seul prend « ## », sauf pour les fonctions de `ID_ONLY`) et un `Begin` sans `labelOf` ; un texte construit
  à l'exécution lui échappe.
- **Des clés arrivent au catalogue par une variable**, hors de tout `tr("…")` : l'extraction de M7.8 ne les verra pas, et
  son contrôle « l'anglais de chaque clé » passerait en les laissant en français. Les constantes de fenêtre (`ImageWindow`,
  `PassesWindow`, `SceneWindow` dans `panels.hpp`, `HierarchyWindow`, `InspectorWindow`), les deux courbes de
  `plotHistory("image (ms)")` et `("GPU (ms)")`, `RendererPassNames` (`render/renderer.hpp`) et la ligne `"interface"` de
  `panels.cpp`. M7.8 les marque (comme le `N_` de gettext) ou les relit depuis cette liste.
- **Les couleurs d'ImGui sont en sRGB** (`linearOnSrgbTarget`). Une cible sRGB convertit en écrivant : sans
  linéarisation dans le shader, l'UI serait convertie deux fois, et délavée. Relu par `levain_ui_gpu` : 72
  niveaux d'écart sans la linéarisation.
- **Une image montrée est opaque, et n'est pas la cible de l'UI.** Le mélange de la passe est celui d'ImGui, l'alpha du
  texel compris (la sortie du tonemap écrit 1.0). Lire la texture où l'UI écrit est une boucle de rétroaction que
  `registerUiTexture` ne détecte pas.
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

Les textes : `FText`/`LOCTEXT` chez Unreal, le paquet Localization chez Unity, `tr()` et gettext chez Godot ; ici le français est la clé, comme le `msgid` de gettext (Godot **documenté**, ADR-0036 [8] ; Unreal et Unity **supposé**).

Montrer une texture que l'UI ne possède pas (`registerUiTexture`) : une `RenderTexture` dans une `EditorWindow` chez
Unity, la `ViewportTexture` d'un `SubViewport` posée dans un `TextureRect` chez Godot, une `FSlateBrush` sur une ressource
rendue (`SImage`) chez Unreal ; **supposé** pour les trois, aucun n'est lu dans leurs sources.
