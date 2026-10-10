# `engine/platform`

## Rôle

La frontière avec le système d'exploitation : fenêtre, événements, processus, et plus tard l'input brut (clavier, souris,
manettes). C'est le **seul module qui inclut SDL3** ([ADR-0003](../../docs/adr/0003-plateforme-sdl3.md)) ; le
reste du moteur ne voit que nos propres types.

**État en M3.4** : une fenêtre, ses événements (fermeture, redimensionnement, masquée, visible) et son titre ;
le lancement d'un programme externe (`runProcess`), pour le hot-reload des shaders ; l'**input brut** du
clavier, de la souris et des manettes, avec la résolution des noms de SDL dont se sert `engine/input`.

## Invariants

1. **Aucun en-tête SDL dans l'API.** `window.hpp` ne connaît SDL que par la déclaration anticipée
   `struct SDL_Window;`, et SDL3 est lié en `PRIVATE`. Seule exception : `engine/gpu` inclut `SDL_vulkan.h` pour
   créer la surface Vulkan à partir de `Window::handle`. C'est pour elle que la fenêtre est créée avec
   `SDL_WINDOW_VULKAN`, mais pour Vulkan seulement (`GraphicsSurface`, `gpu::surfaceFor`) : SDL charge la
   bibliothèque Vulkan pour toute fenêtre qui l'annonce, et refuse de la créer sans elle. La fenêtre de Direct3D 12 et
   celle de WebGPU en natif n'en ont donc plus besoin ; Direct3D 12 seul se lance sans chargeur Vulkan, car Dawn
   charge `vulkan-1.dll` lui-même pour son backend Vulkan (#19).
2. **Une seule fenêtre à la fois.** La fenêtre possède SDL : la détruire appelle `SDL_Quit`. Une assertion le
   vérifie dans `createWindow`. À revoir quand l'éditeur ouvrira des fenêtres secondaires (M7.1).
3. **Titres en ASCII**, vérifié par assertion. Voir « Pièges connus ».

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/platform/window.hpp`](include/levain/platform/window.hpp) | `createWindow` (et `GraphicsSurface`, la surface que la fenêtre annonce), `windowPixelSize`, `pollEvents`, `waitEvents`, `setWindowTitle` |
| [`include/levain/platform/input_script.hpp`](include/levain/platform/input_script.hpp) | `InputScript`, `parseInputScript`, `loadInputScript`, `addScriptedEvents` : des événements écrits dans un fichier, rejoués image par image (ADR-0036) |
| [`include/levain/platform/process.hpp`](include/levain/platform/process.hpp) | `runProcess` — lance un programme, attend sa fin, rend sa sortie (standard et erreur mêlées) et son code de retour |
| [`include/levain/platform/input.hpp`](include/levain/platform/input.hpp) | `InputEvent` (appuis, axes, souris), `keyCodeFromName` et ses cousines, `setMouseCaptured`, `cursorPosition` (la souris en pixels de la swapchain, pour viser à l'écran) ; pour une interface (ADR-0032) : `UiEvent`, `startTextInput`, `stopTextInput`, `clipboardText`, `setClipboardText`, `displayScale` |

## Trois choses à savoir sur l'input brut

**Une manette doit être ouverte pour parler.** SDL n'envoie les événements d'une manette qu'après
`SDL_OpenGamepad`, et il faut la refermer quand elle est débranchée. Le module garde donc la liste des manettes
ouvertes (`input.cpp`) et la vide avant `SDL_Quit` — comme la fenêtre possède déjà SDL, cette liste n'a pas à
sortir dans l'API.

**Un axe de manette et un mouvement de souris ne se lisent pas pareil.** Le premier donne une **position**,
qui reste la même tant que le joueur ne bouge pas le stick ; le second donne un **déplacement**, qui n'existe
que le temps d'une image. `engine/input` accumule l'un et remet l'autre à zéro à chaque image.

**Les noms de SDL sont notre table.** `SDL_GetScancodeFromName("Space")`, `SDL_GetGamepadAxisFromString("leftx")` :
rien à maintenir ici. Mais la table des boutons est restée celle d'une manette Xbox — `SDL_GetGamepadButtonFromString("south")`
rend −1 alors que l'énumération s'appelle `SDL_GAMEPAD_BUTTON_SOUTH`. Les fichiers de liaisons écrivent donc
`pad:a`, et un nom refusé fait échouer le chargement.

## Ce qu'une interface demande en plus (ADR-0032)

Le jeu lit des actions ; une interface (ImGui, `engine/ui`) a besoin d'autre chose, que `Events` donne à part :
`ui`, une liste de `UiEvent`, et `text`, le texte tapé. Ce module ne sait rien d'ImGui.

- **La touche selon la disposition du clavier** (`keycode`), en plus du scancode. Le jeu veut la position
  physique d'une touche (ZQSD sur un AZERTY, WASD sur un QWERTY, la même place) ; une interface veut la lettre
  qu'elle tape, pour que Ctrl+Z annule sur tous les claviers.
- **Les répétitions d'une touche tenue** passent ici, sans dommage (ImGui écarte un appui répété et produit ses
  propres répétitions), mais pas dans `InputEvent`, où une action ne doit partir qu'au vrai appui.
- **Le texte n'arrive qu'entre `startTextInput` et `stopTextInput`.** C'est aussi ce qui ouvre le clavier d'un
  téléphone.
- **La position de la souris est en pixels de l'image**, densité comprise, comme `cursorPosition`.

## Le banc d'essai de l'input (ADR-0036)

`parseInputScript` lit un fichier d'événements (format dans `input_script.hpp`) et `addScriptedEvents` les ajoute,
image par image, à ceux de `pollEvents` : ImGui, `gameInputOf` et les actions ne savent pas d'où ils viennent. **Une
touche y a deux identités**, comme chez SDL : sa position (le scancode, que lit le jeu) et sa lettre (le keycode, que
lit ImGui) ; `key down W as z` simule un AZERTY. Sans `as`, la lettre est celle d'un QWERTY américain, par une table à
nous : `SDL_GetKeyFromScancode` lit la disposition de la machine, et le même script doit donner les mêmes touches
partout. Une ligne illisible, un script sans événement, une image au-delà de `MaxInputScriptFrame` sont refusés en
nommant la ligne et le champ (règle n°7).

**Pas encore scriptés, et le morceau d'ADR-0036 qui les ajoute** (`addKey` et `addButton` montrent où) :

| Événement | Morceau | Pour quel scénario |
|---|---|---|
| la position de la souris | 5, 8 | un clic dans la Vue ou hors d'elle |
| le mouvement, la molette | 11 | l'orbite, le zoom, le vol |
| le texte | 12 | un champ texte qui garde son Ctrl+Z |
| la fermeture de la fenêtre | 13 | la modale de fermeture |
| la répétition d'une touche | aucun | rien ne la demande |

**Le piège du morceau 8** : le picking du sandbox lit `platform::cursorPosition`, qui interroge SDL, pas les
événements. Une position scriptée n'y changerait rien : le morceau 8 doit faire lire à `App` la position que les
événements ont portée (ou la leur substituer sous un script), sans quoi « un clic dans la Vue choisit le cube visé »
resterait intestable.

## Trois choses à savoir sur les fenêtres

**Masquée, pas minimisée.** Sous Wayland, une application n'apprend jamais qu'elle a été minimisée : le
protocole ne le lui dit pas. Le compositeur peut seulement la déclarer « suspendue », ce que SDL traduit en
`OCCLUDED`. Nos événements s'appellent donc `Hidden` et `Shown`, et répondent à la seule question qui compte pour
le moteur : **y a-t-il quelque chose à dessiner ?** Quand la réponse est non, la boucle appelle `waitEvents` et
dort, au plus jusqu'à son échéance (`--seconds` du sandbox) : bureau verrouillé, une fenêtre masquée peut ne plus
rien recevoir. Mesuré avec `tools/kwin-window-smoke.sh` : **0 ms de CPU en 2 s minimisée, contre 2 010 ms visible**.

**Sous Wayland, pas d'image, pas de fenêtre.** Une surface Wayland n'apparaît à l'écran qu'après avoir reçu son
premier buffer. Jusqu'à M1.2, le moteur ne présentait rien et la fenêtre restait invisible ; depuis la swapchain
(#13), elle apparaît, et la minimisation sous Wayland est vérifiée : KWin suspend la fenêtre, SDL émet
`OCCLUDED`, la boucle s'endort.

**Des pixels, pas des points.** Sur un écran à 200 %, une fenêtre de 1280 × 720 points fait 2560 × 1440 pixels.
`createWindow` prend des points, parce que c'est le compositeur qui applique l'échelle ; `Resized` donne des
pixels, parce que c'est ce dont la swapchain aura besoin.

## Pièges connus (SDL 3.4.12)

| Piège | Symptôme | Parade |
|---|---|---|
| `SDL_WINDOW_VULKAN` charge la bibliothèque Vulkan (`SDL_video.c:2517`, `SDL_CreateWindow`) | Sur une machine sans chargeur Vulkan (`vulkan-1.dll` d'un runner Windows), `SDL_CreateWindow` rend `NULL` même pour un backend qui n'en veut pas. | `createWindow` ne pose le drapeau que pour `GraphicsSurface::Vulkan` ; `window_test.cpp` pointe `SDL_VULKAN_LIBRARY` sur un fichier absent et vérifie que seule la fenêtre Vulkan échoue. |
| Titre non ASCII sous X11 (`SDL_x11window.c:2300`) | En locale C, SDL renonce **sans rien dire** à tout titre qu'il ne sait pas convertir, et fuit la mémoire de la conversion. « — » et « × » échouent, « é » passe. | Assertion ASCII dans `setWindowTitle`. Aussi présent sur la branche `main` de SDL. |
| Deux signaux rapprochés (`SDL_quit.c:171`) | Une assertion du SDL compilé en Debug saute, et SDL ouvre une boîte de dialogue qui attend une réponse. | `timeout --foreground`, qui n'envoie qu'un signal. Ctrl+C n'en envoie qu'un par appui. |
| LeakSanitizer sous X11 | 50 052 octets « perdus » en 913 allocations : la mémoire permanente de libX11, que SDL décharge par `dlclose` à la sortie. Une fois la bibliothèque déchargée, plus rien ne semble la retenir. | Faux positif : précharger libX11 (`LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libX11.so.6:…`) le fait disparaître, **sans masquer les vraies fuites** (celle du titre restait signalée). Zéro fuite sous Wayland et en offscreen, là où tourne la CI. |

## Équivalents ailleurs

| Moteur | Module | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `ApplicationCore` | `FGenericApplication` et `FGenericWindow`, une implémentation par plateforme. Sous Linux, `FLinuxApplication` est construit sur SDL : Unreal fait exactement le choix de l'ADR-0003 (**documenté** : sources publiques, `Runtime/ApplicationCore/Private/Linux/`). |
| **Godot** | `DisplayServer` | Une classe par système (`DisplayServerX11`, `DisplayServerWayland`). La boucle principale saute le rendu quand aucune fenêtre ne peut dessiner (`can_any_window_draw`, `main/main.cpp`) : c'est notre `Hidden` (**documenté** : dépôt public). |
| **Unity** | — | Côté C#, `OnApplicationFocus`, `OnApplicationPause` et `Application.runInBackground` exposent le même besoin. L'implémentation C++ n'est pas publique. |
| **Test d'input** | `InputTestFixture` (Unity Input System), `Input.parse_input_event` (Godot), Automation Driver (Unreal) | Rejouer des événements dans la vraie boucle : **documenté** pour Unity et Godot ; chez Unreal, `IAutomationDriver` simule la souris et le clavier par Slate (**documenté** dans l'API, non lu en détail). |
