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
- **le contexte** (`context.hpp`) : créé et réglé pour le moteur ;
- **le style de l'éditeur** (`style.hpp`) : une fonction libre sur un `ImGuiStyle`, que l'éditeur seul appelle.

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
  autre texture que la sienne. Dessiner un identifiant inconnu ou libéré est une assertion en Debug, et en Release
  une erreur au journal, la commande sautée et comptée (`UiDrawStats::unknownTextures`) : jamais un silence. Libérer
  deux fois, ou un identifiant inconnu ou d'ImGui, est traité de même. Le shader lit un `Texture2D<float4>` par un
  échantillonneur qui filtre : `registerUiTexture` refuse, par une erreur et non une assertion (`uiTextureRefusal`),
  une texture nulle, non 2D, multi-échantillon, sans usage de shader, de profondeur, d'un format entier ou de
  flottants sur 32 bits (WebGPU ne les filtre pas).
- **Pas d'`imgui.ini`** : la disposition se reconstruit à chaque lancement.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/ui/ui_pass.hpp`](include/levain/ui/ui_pass.hpp) | `UiPass`, `createUiPass`, `recordUi`, `updateUiTextures`, `destroyUiTextures`, `registerUiTexture`, `releaseUiTexture` ; les pièges `linearOnSrgbTarget`, `clampScissorToTarget` et `uiTextureRefusal` |
| [`include/levain/ui/input.hpp`](include/levain/ui/input.hpp) | `feedInput`, `imguiKeyOf`, `PressedKeys` |
| [`include/levain/ui/context.hpp`](include/levain/ui/context.hpp) | `UiContext`, `createUiContext`, `prepareUiFrame`, `followTextInput` |
| [`include/levain/ui/fonts.hpp`](include/levain/ui/fonts.hpp), [`icons.hpp`](include/levain/ui/icons.hpp) | `findFontFiles`, `addFonts`, `UiFonts` ; la table des icônes et `withIcon` |
| [`include/levain/ui/style.hpp`](include/levain/ui/style.hpp) | `applyEditorStyle`, `EditorAccent` (et ses deux nuances), `srgb8` |
| [`include/levain/ui/tr.hpp`](include/levain/ui/tr.hpp) | `tr`, `trf`, `textf`, `labelOf` : le catalogue des textes vu de l'interface, la table est dans `core` (ADR-0036) |

Les tests : `tests/ui_test.cpp` (le contexte, les touches, l'input, la découpe, le choix sRGB, les textures refusées),
`tests/ui_style_test.cpp` (le style de l'éditeur : fonds opaques, un seul accent), et
`levain_ui_gpu [vulkan|d3d12|d3d12-warp|webgpu]` (`gpu.ui.*` dans ctest, WARP compris), qui relit la couleur d'un rectangle
dessiné dans une cible sRGB puis UNORM, puis un gris moyen montré par `ImGui::Image` dans chaque combinaison de formats,
plusieurs textures dans la même image, une image libérée en vol, et la table des identifiants. `gpu.ui-released-id.*`
dessine un identifiant libéré dans un processus fils, et `gpu.ui-released-twice.*` libère deux fois la même texture, puis un
identifiant inconnu (un signal ne se juge pas par un motif de ctest : build/GOTCHA.md) : l'assertion en Debug, les erreurs au
journal en Release.

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
- **Une image montrée par `ImGui::Image` n'est convertie qu'une fois, par la cible.** La passe ne touche pas aux
  texels : elle les multiplie par la couleur (blanche) du sommet, que `linearOnSrgbTarget` ne change pas. Une texture sRGB
  sur une cible sRGB est juste (le matériel décode en lisant, la cible encode en écrivant), comme une UNORM aux valeurs
  déjà encodées sur une cible UNORM. Une UNORM aux valeurs encodées (188 pour le gris moyen) sur une cible sRGB est
  **convertie deux fois** : 223 relu au lieu de 188 (`levain_ui_gpu`, le cas témoin). D'où le format de la Vue :
  celui de la swapchain. Les valeurs linéaires d'une UNORM sur une cible sRGB sont justes (128 devient 188, à ±2 près : 187 sur la 4070).
- **Libérer une image n'attend pas le GPU, et c'est sûr.** Les command lists qui la dessinent tiennent chacune le binding
  set (NVRHI : `referencedResources` ; le bind group de WebGPU), qui tient la texture : `releaseUiTexture` après
  `recordUi`, avant la soumission, montre encore l'image (`levain_ui_gpu`, « libérée en vol »). Ce qui est interdit, c'est de
  la **dessiner** après : l'identifiant a quitté la table. Cette garde est `BindingSetDesc::trackLiveness` (nvrhi.h), que
  la passe pose explicitement : à faux, `levain_ui_gpu` rougit sur Vulkan (« vkDestroyDescriptorPool … currently in use »).
  L'état de la texture n'est pas deviné par NVRHI : avec un état initial gardé (`setKeepInitialState`), la command list la
  remet dans cet état en se fermant ; avec `beginTrackingTextureState`, elle reste en `ShaderResource` et l'appelant
  fixe l'état d'après.
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

- **Le style de l'éditeur est ici, appelé par l'éditeur, pas par `createUiContext`** (M7.7 morceau 17b, ADR-0036 décision 15). Le
  sandbox et *Rando* gardent le style par défaut d'ImGui : un HUD de jeu ne doit pas avoir l'air d'un outil, et les panneaux
  de debug du moteur, que le sandbox dessine aussi, restent ce qu'ils étaient. `applyEditorStyle(style, displayScale)` vit
  dans `ui` (pas dans `editor/`) parce que ses tests sont ceux d'un `ImGuiStyle` seul, sans fenêtre ni contexte.
  **L'accent est un bleu**, `EditorAccent` (0, 112, 224) : celui de
  l'éditeur d'Unreal 5, et un bleu aussi chez Unity et chez Godot (**supposé** pour les trois : leurs valeurs ne sont pas
  lues dans leurs sources) ; un orange aurait fait un deuxième rouge à côté du X des vecteurs. Il ne désigne que le
  **actif ou choisi** : l'onglet choisi (filet de 2 px), la case cochée, la poignée d'un curseur **tenue** (grise au repos,
  comme un histogramme), la pression d'un bouton. Tout le reste est un gris neutre, et
  `tests/ui_style_test.cpp` le tient : une couleur qui prendrait une teinte hors de sa liste est un deuxième accent.
- **Les couleurs du style n'ont aucun alpha**, hors celles qui en ont besoin par nature (le voile de la modale, l'aperçu
  d'ancrage, la cible d'un glisser-déposer, les trois « rien » : `ChildBg`, `BorderShadow`, `TableRowBg`) : l'interface se
  mélange en linéaire sur une cible sRGB, où un alpha se lit trop fort (« Fonds opaques », ci-dessous). Le mélange en espace
  gamma, s'il vient (décision de Donnovan à la clôture de M7.7), ne changera donc pas l'aspect du style.

- **Fonds opaques, en attendant un mélange en espace gamma.** L'alpha de 94 % des fenêtres et des popups d'ImGui est
  pensé pour un mélange en espace sRGB ; l'interface se mélange ici en linéaire sur une cible sRGB, où 94 % se lit autour
  de 73 % : un menu laissait voir le texte de la barre d'outils, la modale le damier. `createUiContext` rend donc
  `WindowBg`, `PopupBg` et `MenuBarBg` opaques (`ChildBg` reste transparent). **Pas encore** : mélanger l'interface en
  espace gamma, qui rendrait aussi leur sens aux autres alphas du style ; noté au morceau 17 de M7.7 (ADR-0036).
- **Les polices ne sont pas dans le dépôt** (`fonts.hpp`, M7.7 morceau 17, ADR-0036 décision 15). `tools/fetch-assets.sh` met
  dans `assets-cache/Fonts/` Inter (SIL OFL 1.1, `Inter-LICENSE.txt`), Noto Sans (OFL 1.1, `NotoSans-OFL.txt`) et Material
  Symbols Outlined (Apache-2.0, `MaterialSymbols-LICENSE`) ; *Rando* les prend avec le préfixe `Fonts/`. `addFonts` en fait
  la police de l'interface (Inter, puis Noto Sans en `MergeMode` pour un glyphe qu'Inter n'a pas, puis les icônes de
  `icons.hpp`) et celle de la console (ProggyForever à chasse fixe, Inter derrière). **Qui répond à quel code** : la
  première source qui a le glyphe répond, et deux listes d'exclusion (`GlyphExcludeRanges`, la seule que le chargeur
  d'ImGui 1.92 lit : `GlyphRanges` ne sert qu'au préchargement « legacy ») partagent le terrain. Inter et Noto Sans
  n'ont aucun droit sur la zone d'usage privé U+E000..U+F8FF (Inter y a 745 glyphes, qui masqueraient des icônes), et
  Material Symbols n'en a aucun hors d'elle (il y a des lettres et des chiffres, qu'un caractère manquant ne doit pas
  faire dessiner en ligature). `tests/ui_fonts_test.cpp` épingle l'ordre des sources et leurs listes.
  **Sans elles, l'éditeur ne démarre pas** : `findFontFiles` nomme le fichier absent et les racines cherchées, et
  refuse aussi un fichier qui n'est pas une police (trop court, signature inconnue, table manquante ou qui dépasse la fin
  du fichier : un téléchargement tronqué). ImGui, lui, s'arrête sur une assertion en Debug (`ImFontFlags_NoLoadError` ne
  couvre que le fichier absent) ; règle n°7. Le sandbox et *Rando* gardent la police par défaut d'ImGui : un `UiContext`
  seul n'ajoute aucune police, et leurs textes (`engine/app`, `engine/ui`) restent en Latin-1 (« œ » s'y dessinerait
  « ? »). ProggyForever fusionne Inter derrière seulement avec une taille explicite (`SizePixels`) : une taille implicite
  fait échouer une assertion d'ImGui (`AddFont`).
- **Un caractère non ASCII d'un littéral de `editor/` doit être dans `tests/data/ui-glyphs.txt`**, et ceux d'`engine/app` et
  d'`engine/ui` rester en Latin-1 (leur police ne dessine rien d'autre). `i18n.untranslated` refuse les autres ;
  `tests/ui_fonts_test.cpp` charge les vraies polices et vérifie que chaque caractère de la liste a son glyphe, dans
  l'interface (Inter) et dans la console, plus « œ », « — », « É » et chaque icône (dans la source des icônes). Une icône
  s'écrit en échappement `\uXXXX` (`icons.hpp`) : le source reste ASCII.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Slate, rendu par le RHI | Le même cadre pour l'éditeur et le jeu ; en mode retenu, avec un arbre de widgets (**documenté**, ADR-0032). |
| **Unity** | IMGUI (`OnGUI`), pour les outils | Un mode immédiat, comme ImGui ; déconseillé pour l'interface du joueur (**documenté**, ADR-0032). |
| **Godot** | Les nœuds `Control` | L'éditeur est un programme du moteur, avec sa propre UI (**documenté**, ADR-0032). |

Le style de l'éditeur : le thème de l'éditeur. Unreal : le style de Slate (`FAppStyle`, un `FSlateStyleSet`) ; Unity : les skins *Personal* et *Pro* (le sombre) de l'éditeur, et les feuilles de style USS
d'UI Toolkit ; Godot : le thème de l'éditeur (*Editor Settings*, section *Interface > Theme* : couleur de base, couleur
d'accent, contraste), un `Theme` de nœuds `Control` (**supposé** pour les trois : aucun n'est lu dans les sources ni dans
la documentation pour ce morceau). Ici, une fonction sur un `ImGuiStyle`, sans fichier de thème : le jeu de couleurs et de
tailles est dans le code, et l'accent est une constante.

Les polices : une police principale et des polices de repli pour les glyphes qu'elle n'a pas. Unreal : les *composite fonts* de Slate ; Unity : la liste de polices de repli de TextMesh Pro ; Godot : la propriété `fallbacks` d'une `Font` (**supposé** pour les trois : aucun n'est lu dans les sources). Ici, l'ordre des sources fusionnées dans une `ImFont` (`MergeMode`) joue ce rôle, et les icônes sont les glyphes d'une police à part.

Les textes : `FText`/`LOCTEXT` chez Unreal, le paquet Localization chez Unity, `tr()` et gettext chez Godot ; ici le français est la clé, comme le `msgid` de gettext (Godot **documenté**, ADR-0036 [8] ; Unreal et Unity **supposé**).

Montrer une texture que l'UI ne possède pas (`registerUiTexture`) : une `RenderTexture` dans une `EditorWindow` chez
Unity, la `ViewportTexture` d'un `SubViewport` posée dans un `TextureRect` chez Godot, une `FSlateBrush` sur une ressource
rendue (`SImage`) chez Unreal ; **supposé** pour les trois, aucun n'est lu dans leurs sources.
