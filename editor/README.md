# `editor`

## Rôle

La bibliothèque de l'éditeur ([ADR-0018](../docs/adr/0018-moteur-plugins-et-jeu.md),
[ADR-0034](../docs/adr/0034-reflexion-des-composants.md), choix 6B) : ce que l'éditeur ajoute à un programme du
moteur, au-dessus d'`app` (SPECS §7). Le jeu en construit l'exécutable : son `main.cpp` compilé une seconde fois,
avec une définition qui enveloppe son démarrage par `withEditor`, et lié à `levain::editor`. Levain construit
ainsi `levain_sandbox_editor` (dans `sandbox/`) pour ses démos et sa CI ; *Rando* construira `rando_editor` à côté
de `rando`, dont la cible ne change pas.

**État après M7.2 : l'éditeur, c'est le jeu avec deux panneaux de plus.** Il a le branchement sur la boucle,
l'option `--select`, le bilan que lit la CI, **la hiérarchie** : les entités placées (`Transform`), rangées par
`flecs::Parent` (ADR-0015), et un nœud *Singletons*, qui sélectionne seulement ; et **l'inspecteur** : les
composants de l'entité choisie, un widget par champ, lus dans la description de flecs, aux valeurs de l'image. Une
donnée d'auteur (`Authored`) s'édite, le reste est grisé ; trois dessinateurs lui donnent des angles, des noms
d'entité et d'asset. Il n'a pas encore :

- **de caméra d'éditeur** (morceau 9) : en Édition (M7.7, morceau 5), la simulation est à l'arrêt, le `frame` du
  programme n'est jamais appelé et le jeu ne reçoit rien (invariant 16) ; la caméra du jeu reste donc une vue fixe,
  aucun système ne la déplace, et ZQSD ne fait plus rien ;
- **de caméra qui se pilote dans la Vue** (morceaux 9 et 11) : la **Vue existe** (M7.7, morceau 8, `view.hpp`, invariant 19),
  la scène y est rendue à la taille de son contenu, mais la caméra est celle du jeu, une entité que déplace un
  système du pas fixe (sandbox/src/main.cpp:641-647 ; engine/scene/src/scene.cpp:111-117) ;
- **d'écriture vue dans son image** : les panneaux passent après les pas et le choix de la caméra (app.cpp:875-891) ;
  une valeur tapée dans l'inspecteur se voit à l'image suivante ;
- **d'enregistrement** : rien n'écrit la scène, et fermer la fenêtre arrête la boucle sans rien demander
  (app.cpp:106-107) ;
- **d'annulation** : `commitEdit` écrit la copie par `setComponentValue` sans garder l'ancienne valeur
  (editor/src/inspector.cpp:543-552) ;
- ni menu, ni raccourci que Alt+P et Échap (invariant 16).

**La console est écrite** (M7.7, morceau 14) : la fenêtre « Console » lit le journal du processus par un récepteur de
`core::log` (`withEditor` le pose avant `runApp` : ce que la fenêtre et le device écrivent en s'ouvrant y est), dans un
anneau de 2 000 messages ; niveaux, compteurs de tous les messages reçus, recherche, Effacer, Réduire. Elle s'ancre
pour l'instant au nœud de la hiérarchie (le bas de l'écran est pour la disposition du morceau 15), dessinée *avant* elle :
après, ImGui lui donnerait l'onglet visible du nœud à l'ouverture. Dans cette colonne étroite, la barre d'outils s'enroule
(`fitsOnLine`) et le champ de recherche a sa propre ligne. De bout en bout : `gpu.editor-console.*`
(`tests/editor_console_gpu.cpp`), qui lit ce que les fenêtres ont dessiné par `EditorOptions::afterUi`.

**La table des raccourcis est écrite, défauts seuls** (M7.7, morceau 10, `shortcuts.hpp`, ADR-0036, décision 13) : par
action son nom (clé du catalogue), au plus deux accords, un contexte (vol, Vue, global, jeu ; le vol passe avant la
Vue, le jeu vaut seul sous sa route). Le vol et les outils vont **par position** (le scancode d'USB), les accords à
Ctrl ou Alt **par lettre**. `displayNameOf` montre la touche de la disposition d'aujourd'hui (`App::keyAtPosition` :
« Z » à la place du W sur un AZERTY) ; `tokenOf` et `chordFromToken` fixent le nom stable des préférences (`pos:26`,
`Ctrl+S`, jamais un nom de SDL ou d'ImGui). **Pas encore branchée** : Alt+P et Échap restent lus par `mode.cpp`
(`playShortcutPressed`, `StopScancode`), et les gestes (morceau 11) et les menus (12) appelleront `actionFor` ; changer
et enregistrer un accord sont les morceaux 15 et 16. Équivalents : les raccourcis d'éditeur d'Unreal, le *Shortcuts
Manager* d'Unity, les *Editor Settings > Shortcuts* de Godot. Test : `tests/editor_shortcuts_test.cpp`, et de bout en
bout `gpu.editor-shortcuts.*`. **Le contrat avec ImGui** : `actionFor` sert les accords de position et le contexte *jeu* ; les accords de lettre passent par `ImGui::Shortcut(imguiChordOf(...))`, et un champ de texte actif (`ActiveContexts::textInput`) ne laisse rien au vol, à la Vue ni au global. Alt-Gr tenu ne vaut aucun accord. **À faire au morceau 11** : le relâchement d'un vol est un clic pour ImGui (le menu contextuel de la Vue s'ouvrirait à la fin de chaque vol) ; la Vue ne l'ouvrira que si aucun vol n'a eu lieu depuis l'appui (`playFlightReleaseIsAClick` épingle le comportement d'aujourd'hui).

**La logique de sa caméra est écrite** (M7.7, morceau 9, `camera.hpp`), sans entrée ni Vue pour la porter : les gestes
(morceau 11) lui donneront des deltas, `App::cameraOverride` sa sortie.

La suite : l'[ADR-0036](../docs/adr/0036-mode-edition-vue-et-camera-de-l-editeur.md), proposé pour M7.7, que
suivent M7.5, M7.3, M7.4 et M7.6. Les gestes quotidiens d'Unity et d'Unreal que l'éditeur doit rendre, chacun avec son
milestone et le scénario qui le prouvera, sont dans [GESTES.md](GESTES.md) : chaque clôture y compte « N gestes sur
M couverts par un test ».

## Invariants

1. **Le jeu livré n'embarque pas l'éditeur.** Ni une cible du moteur (`engine/`) ni la cible runtime d'un plugin
   ne lient `levain_editor` : `levain_check_plugin_boundaries` le refuse au configure (`cmake/LevainPlugin.cmake`,
   vérifié par les tests `cmake.plugins.*`). `build.no-editor` refuse un symbole `levain::editor::` dans
   `levain_sandbox`, et `build.editor-symbols` en exige dans `levain_sandbox_editor` : sans lui, un motif périmé
   laisserait le premier vert sans rien lire (règle n°7). La cible éditeur d'un plugin (M7.6) pourra le lier.
   Le configure ne lit que les liens directs : une cible intermédiaire qui lie l'éditeur lui échappe, et seul
   `build.no-editor` la rattrape, pour le sandbox.
2. **L'éditeur ne lie jamais un plugin** : la même fonction parcourt `editor/` comme `engine/`.
3. **Natif seulement**, hors du bloc `PROJECT_IS_TOP_LEVEL` : la page web n'a pas d'éditeur, et un jeu qui
   récupère Levain par `FetchContent` reçoit la bibliothèque.
4. **La boucle ne change que par les points nommés d'`app`** (ADR-0036, qui modifie cet invariant) :
   `withEditor` enveloppe la fonction de démarrage du programme et ses points d'accroche (ADR-0029), `frame`
   compris depuis le morceau 5, et pose `simulationPaused`, `inputRoute`, `mouseCaptureWanted` et
   `recomposeAfterUi` ; il ne touche rien d'autre.
5. **L'exécutable éditeur ouvre les panneaux** : son `main` pose `settings.showUiPanels = true` avant de lire
   les options, que `--ui off` les ferme encore.
6. **Un `--select` introuvable fait échouer le démarrage**, en nommant l'entité (règle n°7).
7. **Les panneaux suivent ceux du moteur** : dessinés après les fenêtres du programme, panneaux ouverts
   seulement (F1, `--ui`), et ancrés aux nœuds que la disposition d'`app` garde dans `app.ui.dock`, la seule
   extension d'`app` pour l'éditeur. La hiérarchie y fait un onglet à côté d'« Image » ; l'inspecteur a le
   sien, sous « Scène ».
8. **La hiérarchie ne lit que ce qu'elle montre** : les racines viennent d'une requête gardée, les enfants des
   seuls nœuds ouverts, une table à la fois. Chaque image range une ligne par racine, sans widget, et ImGui ne
   dessine que les lignes visibles (`ImGuiListClipper`).
9. **L'inspecteur ne connaît aucun composant** : il suit la description de flecs (`EcsStruct`, `EcsArray`,
   `EcsPrimitive`, `EcsEnum`), un widget par champ selon son type : nombres, booléen, liste des constantes d'une
   enum, nœud d'un agrégat, lignes de quatre nombres d'un tableau (une colonne de `glm::mat4` par ligne), et
   `vec2`/`vec3` d'un bloc. Un composant non décrit, une étiquette ou une paire : une ligne à leur nom, jamais
   leurs octets ; un type sans widget (texte, type opaque) : le nom du type. Trois dessinateurs (invariant 13)
   remplacent le widget générique de trois types, et la valeur d'un `flecs::Parent`, une entité de flecs, se
   lit par le chemin de son parent. Le nom de l'entité (`Identifier`) est en tête de la fenêtre, pas un
   composant.
10. **Les widgets éditent une copie**, jamais la table. Si elle a changé, `commitEdit` l'écrit par
    `setComponentValue` : un seul `OnSet`, la seule écriture de l'inspecteur. Un composant sans `Authored` est
    grisé, et `commitEdit` le refuse encore si une copie a changé : deux gardes, chacune testée (le grisé par un
    champ qui ne devient jamais actif, `commitEdit` par une copie changée que rien n'écrit).
11. **Les bornes sont imposées par l'inspecteur** (`ImGuiSliderFlags_AlwaysClamp`, sans lequel Ctrl+clic tape
    au-delà) : flecs ne borne rien. Une borne posée par `.range` s'applique à tous les nombres, entiers 64 bits
    compris, et un entier la lit vers l'intérieur (un plancher de 0,5 est 1). **Un nombre non fini ne
    s'écrit jamais** (`rejectNonFinite`) : ImGui laisse taper « nan », « inf » ou « 1e39 » (`sscanf` sans
    filtre), et sa borne compare par `<` et `>`, que NaN ne satisfait pas. Le widget qui en rend un laisse le
    champ comme il était.
12. **Les panneaux passent entre `defer_begin` et `defer_end`** (`withEditor`) : les observateurs d'une écriture
    passent après le parcours des composants de l'entité, qu'ils feraient changer de table. `drawInspector`
    refuse un monde non différé (assertion).
13. **Trois dessinateurs, dans l'éditeur** (ADR-0034) : le quaternion en angles de tangage, lacet et roulis
    (`eulerHint`, les angles tapés gardés d'une image à l'autre), l'entité par son chemin (`entityFieldOf`), et
    l'asset par le nom de son fichier au registre, sinon son GUID (`assetNameOf`), en lecture seule : les deux
    entiers de 64 bits d'un `AssetRef` ne se tapent pas. Celui d'un plugin viendra dans sa cible éditeur
    (ADR-0018).
14. **Tout texte affiché passe par le catalogue** (`ui::tr`, `trf`, `textf`, `labelOf` ; ADR-0036, décision 14), sauf les
    noms de la réflexion (composants, champs), qui restent ceux du code. Un identifiant seul prend « ## » ; une fenêtre,
    un titre en `ui::labelOf`. `i18n.untranslated` le refuse, un texte construit à l'exécution lui échappe.
15. **La caméra de l'éditeur est un état et des fonctions libres** (ADR-0036, décision 6, ADR-0011) : `EditorCamera`
    (l'œil, le lacet, le tangage, la distance du pivot, la vitesse, le champ), que chaque fonction reçoit et rend
    modifié. Ni entité, ni SDL, ni ImGui, ni flecs : un delta est en pixels, en crans de molette ou en unités, la durée
    est celle de l'image. **Les angles sont la source** (comme `scene::FpsController`), le pivot se déduit (`pivotOf`,
    à `pivotDistance` devant l'œil). **Tout nombre qui entre est fini ou ignoré** : un NaN dans un delta, une durée
    ou un axe ne change rien. Le tangage, le lacet et la vitesse ne sont **jamais utilisés** hors de leurs bornes (±89°,
    replié à ±180°, 0,1 à 1000) : un état lu d'un fichier avec un tangage de 120° regarde comme à 89°. Le pan et le
    dolly, qui ne les changent pas, les laissent tels quels dans l'état.

16. **L'éditeur s'ouvre en Édition** (`mode.hpp`, ADR-0036, décision 3) : aucun pas (`App::simulationPaused`), le
    `frame` du programme jamais appelé (`programFrameRunsIn`), la route de l'input *éditeur* (rien au jeu, ce qu'il
    tenait relâché), `PlayerInput` vide touches tenues. **Alt+P** (`ImGui::Shortcut`, route globale) ou le bouton de
    la barre de mode passent en « Jouer (sans retour) » : le jeu d'aujourd'hui sous un bandeau, rien n'est restauré à
    l'arrêt (M7.5 viendra avec Play et Stop). **Alt+P ne fait que jouer** (la table de la décision 13 : « Jouer »),
    **Échap ne fait qu'arrêter**, et seul le bouton va dans les deux sens : `modeRequested` en décide, testée. Sous la
    route *jeu*, ImGui n'a pas Alt+P ; une Alt+P qui basculerait ne ramènerait qu'aux images où un panneau a le focus.
    Échap est lue sur l'input brut par position (`StopScancode`) : elle vaut partout où le jeu reçoit le clavier,
    donc sous la route *jeu* mais aussi sous la route *UI* quand une fenêtre a le focus sans qu'un champ soit actif ;
    la décision 13 la réserve au contexte *jeu*, ce que la table des raccourcis (morceau 10) rendra. **Chaque
    changement de mode remet `mouseCaptureWanted` à faux** (`enterMode`) : le `frame` du programme, qui le repose,
    ne tourne plus en Édition, et un clic droit tenu qui capturait la souris la gardait ; le jeu repart aussi d'un
    `InputState` neuf, sans appuis en attente pour le premier pas. La route de l'image suivante se pose à la fin de
    `ui` : *éditeur* en Édition, en jeu *jeu* si la scène a le focus (`sceneFocusedIn`), sinon *UI*.
    **La Vue** (morceau 8) donne le rectangle de la scène : panneaux ouverts, `App::sceneRect` est le rectangle de son
    image (le contenu de la fenêtre) ; panneaux fermés, la fenêtre entière. Elle a le focus quand on clique dedans, ou
    quand Play le lui donne (`View::focusRequested`) : la route *jeu* veut **la Vue focalisée** panneaux ouverts
    (`sceneFocusedIn`), aucune fenêtre focalisée panneaux fermés. **Le clic qui donne le focus à la Vue n'atteint pas
    le jeu** (la route est encore *UI* à cette image : ImGui le prend pour focaliser), mais celui qui suit Alt+P
    l'atteint (`viewHasFocus` compte la demande de focus de Play, la route est *jeu* dès l'image suivante). **La barre de mode ne passe pas au jeu** : tant que la
    souris la survole (`View::barHovered`), la route de l'image suivante est *UI*, et la cliquer laisse le focus à
    la Vue (`IsWindowFocused(ChildWindows)`). Piège connu, du même ordre : le survol ne se sait qu'en fin d'image,
    donc un clic dans l'image même où la souris arrive sur la barre atteint encore le jeu ; le morceau 12, qui
    remplace la barre par une vraie barre d'outils, le règle. Une Vue fermée ou cachée n'a pas de rectangle
    (vide, à l'origine du nœud) : la souris n'est pas au jeu. **La barre de mode** est un enfant de la Vue, dans le coin
    haut gauche de l'image, pas une fenêtre flottante, qui passerait derrière la Vue dès qu'elle prend le focus
    (`drawModeBarOverlay`). Panneaux fermés, il n'y a pas de Vue, et la barre est le bandeau d'avant, sans bouton
    (`NoInputs`), pour que le bandeau de « Jouer (sans retour) » se voie aussi sans panneaux et que la route *jeu*
    reste. Celle du morceau 12 la remplacera.
17. **Une valeur tapée dans l'inspecteur se voit dans l'image même** : `commitEdit` qui écrit lève
    `Inspector::wrote` (remis à faux à chaque `drawInspector`), et `withEditor` pose alors
    `App::recomposeAfterUi` (ADR-0036, décision 2). Le scénario de bout en bout attend le texte scripté (morceau 12).

18. **L'éditeur refuse `--gpu webgpu`, bruyamment** (`requireEditorBackend`, ADR-0036, décision 7), dans le `main` de
    l'exécutable éditeur, une fois les options lues et **avant `runApp`** : ni fenêtre ni device Dawn pour rien, et le
    message est celui de l'éditeur, pas celui d'un adaptateur absent. La page web n'a pas d'éditeur, et la Vue de
    l'éditeur natif n'a pas été éprouvée sur Dawn. Le programme sort en 1 (`editor.refuses-webgpu`, qui lance
    l'exécutable et lit le message, sans « device créé »).

19. **La Vue** (ADR-0036, décisions 7 et 10 ; `view.hpp`) : une fenêtre sans marge, ancrée au nœud central, qui demande
    à chaque image la taille de son contenu (`sceneSizeOfContent`, l'entier en dessous, vide sous un pixel) par
    `App::requestScene`, puis montre la texture par `ImGui::Image` sur tout son contenu. **Chaque image où elle n'est pas
    montrée, elle demande 0 × 0** (fermée par le menu, repliée, derrière un onglet : `ImGui::Begin` faux) : « rien
    demandé » est la scène dans la fenêtre, que la Vue ne doit jamais provoquer. `App::sceneRect` est le rectangle de
    son image, que la route de l'input et le picking du sandbox lisent (`scenePixelAt`, avec `App::cursor`, la position
    des événements et non celle de SDL). **La disposition** : la hiérarchie à gauche, la Vue au centre, l'inspecteur à
    droite **seul sur son nœud** (donc toujours visible, la CI le compte), la console, Image, Passes et Scène en onglets
    en bas (`DockNodes::left, center, inspector, bottom`). **Le menu « Fenêtre »** (`window_menu.hpp`), dans une barre
    de menus qui n'a que lui (les autres sont du morceau 12), ferme et rouvre chaque fenêtre par son drapeau
    (`windowEntriesOf`) ; les fenêtres sont fermées aussi par leur croix. La disposition n'est pas gardée (morceau 15) :
    une fenêtre fermée puis rouverte retrouve son nœud, ImGui gardant le `DockId` de la fenêtre fermée
    (`SetNextWindowDockID` en `FirstUseEver` ne sert qu'à la première ouverture). Le sandbox et *Rando* gardent leur
    disposition et leurs fenêtres sans croix (`UiLayer::editorLayout`, posé par `withEditor`) : rien n'y rouvrirait
    une fenêtre fermée. La ligne de la CI
    `editor.view width=W height=H -- vue : W×H` donne la taille demandée à la dernière image (`ci-programs.sh editor` la
    veut non nulle). Pas encore : l'onglet caché et la Vue repliée sont testés en posant l'ancrage et le repli par le
    code (`playHidden`), non à la souris ; glisser ou désancrer la Vue n'est pas testé ; la barre de menus n'existe que
    panneaux ouverts ; **`StableFramesBeforeResize` n'est pas mesuré sur un vrai glissé de séparateur à 144 Hz** (cette
    machine n'a pas d'écran à 144 Hz), à faire quand un écran rapide est sous la main.

## Points d'entrée

- `requireEditorBackend(api)` : le refus de WebGPU, une fonction pure testée (`tests/editor_test.cpp`), que le `main` de
  l'exécutable éditeur appelle avant `runApp` (sandbox/src/main.cpp).
- `withEditor(start, options)` : la fonction de démarrage du programme, enveloppée. Elle choisit l'entité de
  `--select`, en ouvre les ancêtres, ajoute la hiérarchie et l'inspecteur aux fenêtres, et écrit à la fin le
  bilan que lit la CI, « éditeur : N entités, M champs dessinés ; sélection : chemin », N étant les lignes de la
  hiérarchie et M les champs de la sélection à la dernière image.
- `Mode`, `ModeState`, `ModeRequests`, `modeRequested`, `enterMode`, `stopPressed`, `inputRouteFor`,
  `simulationPausedIn`, `programFrameRunsIn`, `otherMode`, `modeNameOf`, `modeHintOf`, `modeTokenOf`, `drawModeBar`,
  `playShortcutPressed`, `sceneHasFocus` (`mode.hpp`) : les modes, en fonctions libres. À la fin, `withEditor` écrit aussi `editor.mode mode=edit steps=0 -- mode : Édition ; pas : 0`,
  sous son nom stable (ADR-0036, décision 14), que la CI lit : l'éditeur lancé sans script est en Édition à zéro pas
  (`App::stepsPlayed`, tous les pas de la boucle).
- `Action`, `Context`, `Chord`, `Binding`, `ShortcutTable`, `defaultShortcuts`, `bindingOf`, `nameOf`, `actionFor`,
  `chordMatches`, `positionHeld`, `displayNameOf`, `tokenOf`, `chordFromToken`, `imguiChordOf` (`shortcuts.hpp`) : la
  table des raccourcis, en fonctions libres dont la disposition du clavier est un paramètre.
- `drawView(view, app, mode)`, `View`, `sceneSizeOfContent`, `sceneFocusedIn` (`view.hpp`) ; `drawWindowMenu(entries,
  layout)`, `WindowEntry` (`window_menu.hpp`) ; `windowEntriesOf(editor, app)` (`editor.hpp`) : la Vue, son menu. À la
  fin, `withEditor` écrit aussi `editor.view width=W height=H -- vue : W×H`, sous son nom stable.
- `drawConsole(console, ring, dock, open)` : la fenêtre « Console ». Son modèle est en fonctions libres sur une copie de
  l'anneau de `core` (`core/log_ring.hpp`), sans ImGui : `listConsoleRows` (niveaux, recherche, Réduire),
  `isShown`, `containsIgnoringAsciiCase`, `shouldFollowTail` (le défilement automatique, sauf remonté),
  `filterLabels` (les trois cases et leurs comptes), `fitsOnLine` (la barre d'outils qui s'enroule), `lineOf` et `singleLine`. Un message sur plusieurs lignes tient
  sur une seule (la liste suppose une hauteur fixe) ; la sélection d'une ligne par un clic n'est pas écrite.
- `takeEditorOptions(arguments, options)` : retire `--select chemin` de la ligne de commande avant que le programme
  ne lise ses options, qu'il refuserait sinon.
- `selectedIfAlive(world, selected)` : l'entité choisie si elle vit encore.
- `drawHierarchy(world, hierarchy, selected, dock, open)` : la fenêtre (`open` : le drapeau de sa croix et du menu) ; `listHierarchyRows` et `singletonsOf`, ses
  lignes, sans ImGui.
- `drawInspector(world, inspector, selected, dock, open)` : la fenêtre, dans son nœud ;
  `inspectComponent`, les champs d'un composant, rend leur nombre ; `componentLabelOf`, le nom d'un composant ;
  `commitEdit`, l'écriture d'une copie éditée ; `enumNameOf`, la constante qu'une valeur désigne.
- `createInspector(world, registry)` : les types des dessinateurs, lus une fois ; le registre d'assets de
  l'application nomme les assets, sans lui leur GUID. `eulerHint` et `rotationFromEuler` : les angles d'une
  rotation et la rotation de ces angles ; `entityFieldOf` et `entityLabelOf` : l'entité d'un champ, et son
  texte ; `assetNameOf` : le texte d'un asset.
- `flyCamera(camera, input, seconds)`, `orbitCamera(camera, pixels)`, `panCamera(camera, pixels, hauteur)`,
  `dollyCamera(camera, crans)`, `scaleFlySpeed(camera, crans)` : les gestes de la caméra, chacun rend la caméra
  suivante. `framingOf(camera, box, aspect)` la place pour qu'une boîte tienne (F), `clipPlanesFor(camera, box)` rend
  les plans proche et lointain qui la contiennent, `toRenderCamera(camera, plans)` la `render::Camera` que prend
  `App::cameraOverride`. `editorCameraFrom(render::Camera)` en est l'inverse, pour partir de la caméra du jeu : le lacet
  et le tangage se retrouvent par `atan2` (jamais NaN, tangage borné), mais pas la distance du pivot, la cible d'une
  caméra du jeu étant à une unité devant elle. `clampEditorPitch`, `wrapYawDegrees` et `clampFlySpeed` nomment les
  bornes.

## Pièges connus

- **`selectedIfAlive`** : la sélection garde l'identifiant complet de l'entité, génération comprise, et ne le
  relit qu'à travers `is_alive`. Une entité détruite ne laisse pas de poignée morte, et flecs, qui recycle son
  index avec une autre génération, ne fait pas passer la nouvelle entité pour la sélection.
- **`isEngineInternal`** : modules, systèmes, observateurs, requêtes, composants et prefabs sont des entités
  comme les autres. Un singleton est rangé sur l'entité de son composant : `world.set(Transform{})` ferait passer
  le composant `Transform` pour une racine de la scène. La hiérarchie les tait, une table à la fois ; les
  singletons ont leur nœud, sans les modules (flecs garde leur instance sur leur entité) ni les composants de
  flecs (`Component` se décrit lui-même) : `isEngineSingleton`.
- **`isShownChild`** : un enfant n'est montré que rangé par `flecs::Parent`. Créé par son chemin (`a::b`), il
  l'est par `ChildOf` : c'est une racine, que l'arbre ne répète pas sous son parent. La flèche d'un nœud suit le
  même filtre (`hasShownChildren`) : un observateur ou un enfant sans `Transform` n'en donnent pas.
- **Une entité placée sous un parent qui ne l'est pas n'apparaît pas** : elle a un `flecs::Parent`, ce n'est
  pas une racine, et son parent n'a pas de ligne. Aucune vue du sandbox n'en a ; les entités sans `Transform`
  viennent en M7.3.
- **`pushEntityId`** : l'identifiant ImGui d'une ligne vient de l'identifiant complet de l'entité, jamais de son
  nom, qui se répète (`crate_0` sous deux parents), ni de son index, que flecs recycle.
- **Les lignes changent d'ordre quand une entité change de table** : la hiérarchie suit l'ordre des tables de
  flecs, sans tri, qui coûterait à chaque image. Un composant ajouté déplace l'entité dans la liste.
- **`componentLabelOf`** : un identifiant de composant n'est pas toujours une entité. `flecs::Parent` apporte
  `(ParentDepth,@1)`, à drapeaux (ni paire ni entité), sur lequel `ecs_get_symbol` arrête le programme : seule
  une entité (`flecs::id::is_entity`) a une clé, le reste prend le texte de flecs (« (Identifier,Name) »).
- **`createInspector`** lit les identifiants des feuilles glm une fois, au démarrage : `world.id<T>()` pendant le
  dessin enregistrerait un type absent du monde au milieu du parcours.
- **Une enum se lit par les octets de ses constantes** : flecs ne remplit `ecs_enum_constant_t::value` que pour
  un type sous-jacent signé, et `value_unsigned` sinon. La valeur de chaque constante est aussi sur son entité,
  dans son type (la paire `(Constant, type)`) : `enumNameOf` et la liste déroulante la comparent et la copient
  octet par octet, sans regarder le signe. Une valeur hors des constantes s'affiche « ? ».
- **`commitEdit`** : `sameValue`, feuille par feuille, jamais `memcmp` (le remplissage diffère) ; une copie qu'un
  widget a « changée » vers la même valeur (une borne qui ramène à l'identique) n'écrit rien.
- **`eulerHint`** : une rotation n'a pas d'angles uniques. L'ordre est celui de `applyFpsInput` : lacet, puis
  tangage, puis roulis (R = Ry·Rx·Rz), comme Unity et Unreal. Le lacet couvre ±180°, le tangage ±90° ; au-delà
  (200° de tangage), le quaternion se relit autrement (-20°, le lacet et le roulis retournés), et le relire à
  chaque image ferait sauter ce qu'on tape : on garde les angles tapés (par identifiant du widget) tant qu'ils
  redonnent la rotation du composant, et on relit dès que quelqu'un d'autre l'a changée. À ±90° de tangage,
  lacet et roulis se confondent : le roulis est relu après le lacet, et les angles rendus donnent encore la
  rotation. `glm::eulerAngles` (Rz·Ry·Rx, lacet limité à ±90°) lisait -95° de lacet (180°, -85°, 180°), ce
  qu'un personnage qui tourne écrit tous les jours. Un angle qui s'arrondit à zéro à l'écran vaut 0 (pas de
  « -0.000 »). Ce qui s'écrit est normalisé (`rotationFromEuler`).
- **`entityFieldOf`** : un `flecs::entity` (16 octets : le monde et l'identifiant) est un type opaque pour flecs,
  lu ici comme tel, jamais par `EcsOpaque::serialize` ni `assign_*`, que flecs appelle par un pointeur de
  fonction d'un autre type (UBSan, `linux-asan`). Une entité détruite se lit « (détruite) », la nulle « aucune ».
- **`clampPitch`** : `FpsController` laisse régler `minPitchDegrees` et `maxPitchDegrees` l'un sans l'autre ;
  l'inspecteur peut donc les inverser, et `std::clamp` d'un min au-dessus du max est indéfini (la libstdc++ en
  Debug s'arrête). `clampPitch` remet les bornes dans l'ordre.
- **Un onglet caché ne dessine rien** : `Begin` rend faux pour une fenêtre derrière un autre onglet, et ses champs
  ne sont pas parcourus. À plusieurs sur un nœud, c'est ImGui qui met un onglet devant, sans règle qu'il documente :
  l'inspecteur a donc son nœud (`DockNodes::inspector`), toujours visible, sans quoi la CI pourrait compter zéro
  champ. La hiérarchie, onglet d'« Image », dresse sa liste avant `Begin` : le compte de ses lignes n'en dépend pas.
- **`clampEditorPitch`** : ±89° et non ±90°. À 90° exactement l'avant est parallèle à la verticale et `lookAtRH`
  (render/src/camera.cpp:10) n'a plus de droite ; au-delà, l'avant passe de l'autre côté et l'image se retourne.
  `viewDirectionOf` borne aussi : un tangage lu dans un fichier ne retourne pas la vue. `std::clamp(NaN)` rend NaN,
  qui resterait dans la position : un NaN vaut 0.
- **`dollyCamera` ne se coince pas** : la distance au pivot a un plancher (0,05) ; un zoom purement géométrique
  s'y arrêterait, chaque cran n'avançant plus que de 20 % de rien. Le pas ne descend donc pas sous une unité : collé
  au pivot, l'œil avance de 0,2 par cran et le pivot reste devant lui. Au-dessus d'une unité, un cran avant puis un
  cran arrière ramènent où l'on était ; en dessous, non. Au plafond (10⁶) c'est l'inverse : l'œil s'arrête. Sinon
  une molette libre l'enverrait à 10⁷ unités avec son pivot, et le pas de 0,2 du retour, perdu dans l'ulp d'un
  `float` à cette distance, ne le ramènerait jamais.
- **`panCamera` suit le curseur** : l'échelle est `2 · distance · tan(champ / 2) / hauteur`, donc le point au pivot reste
  sous la souris à toute distance, mais un point plus proche ou plus loin glisse (la parallaxe). Le haut du glissé est
  le haut de l'**écran** (incliné par le tangage), les touches Monter et Descendre de `flyCamera` la verticale du
  **monde**.
- **`framingOf` entoure la boîte d'une sphère** : une boîte allongée (une dalle, une tour vue de bout) garde de
  l'air autour d'elle, le prix d'un cadrage qui tient sous tous les angles sans tourner la vue. Au-delà de
  `MaxPivotDistance` (10⁶), elle ne tient plus.
- **`toRenderCamera` vise loin** : `lookAtRH` retrouve le regard par `cible - position`, en `float`. Une cible à une
  unité d'un œil à 600 unités de l'origine hérite de l'arrondi de la position : au tangage de 89° (0,017 d'horizontale),
  la vue tourne autour de son axe d'un dixième de degré, et le bord de l'image tremble à chaque coup de souris. La
  cible est donc au moins aussi loin de l'œil que la position l'est de l'origine, et que le pivot.
- **`clipPlanesFor` prend la scène, pas la boîte cadrée** : la boîte est ce qui doit rester visible. F n'agit que par la
  position de l'œil ; des plans calculés sur la boîte cadrée mettraient le lointain à quelques mètres, et la vallée
  derrière un cube de 1 m disparaîtrait (testé : un cube de 1 m et un objet de 5 cm cadrés au milieu de la vallée de
  512 m, plans de la vallée).
- **`clipPlanesFor` borne le rapport** : lointain sur proche entre 2 et 10 000. Caméra dans la boîte (ou scène de 10⁶
  unités de profondeur), le plan proche monte au plancher du rapport et rogne le premier plan, sans quoi le
  tampon de profondeur perdrait sa précision. Les plans ne contiennent donc la boîte que si l'œil est dehors et
  la profondeur raisonnable. `near` et `far` sont des macros de `minwindef.h` (SDK de Windows) : jamais de variable
  de ce nom.
- **Les options vont par paires** : `takeEditorOptions` lit nom et valeur comme `app::parseCommonOption`. Un
  « --select » en valeur d'une autre option n'est pas pris ; seul, sans valeur, il reste dans la ligne de
  commande, et le programme la refuse.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Les modules `Editor` d'un plugin, et l'Unreal Editor | Un module Editor « will only be loaded when the editor is starting up » (**documenté**, ADR-0034 [12]) ; l'éditeur est un exécutable à part du jeu livré (**supposé**, d'après la structure des cibles `*Editor.Target.cs`). |
| **Unity** | Les scripts du dossier `Editor` | Ils « aren't available in Player builds at runtime » (**documenté**, ADR-0034 [13]) : rien de l'éditeur dans le jeu livré, comme ici. |
| **Godot** | L'éditeur, un programme du moteur | Il tourne sur le moteur et son UI (**documenté**, ADR-0032) ; le jeu s'exporte avec des modèles d'export compilés sans l'éditeur (**supposé**). |
| **Unreal**, la console | L'Output Log | Les messages du journal par catégorie, les erreurs en rouge (**documenté**, *Logging in Unreal Engine*) ; ses filtres de catégorie et de verbosité sont **supposés** : ici, trois niveaux et une recherche qui lit aussi la catégorie. |
| **Unity**, la console | La fenêtre Console | Des filtres par type avec leurs compteurs, une recherche et *Collapse* (**documenté**, manuel, *Console Window*) : Réduire ici fusionne les messages identiques *qui se suivent*, là où Unity fusionne tous les identiques (**supposé** : non relu dans le manuel). |
| **Unreal**, la hiérarchie | L'Outliner (*World Outliner* jusqu'à UE4) | « Hierarchical tree view of all Actors within the current Level. Used for selection, attachment, and more » (**documenté**, *Outliner in Unreal Engine*). Un acteur sans position y figure aussi ; ici, seules les entités placées, les autres en M7.3. |
| **Unity**, la hiérarchie | La fenêtre Hierarchy | Pour « arrange the GameObjects in your scenes, group them into parent-child hierarchies » (**documenté**, manuel, *Hierarchy window*). |
| **Godot**, la hiérarchie | Le dock Scène | Il « lists the active scene's nodes » (**documenté**, *First look at the editor*) ; un nœud est un objet, pas une entité d'ECS : pas de singletons à part. |
| **Unreal**, l'inspecteur | Le panneau Details | « display properties and customized editing tools for selected Actors » (**documenté**, *Level Editor Details Panel*). Ses champs viennent de la réflexion `UPROPERTY`, comme ici de celle de flecs ; un tableau de la réflexion s'y déplie (**supposé**). |
| **Unity**, l'inspecteur | La fenêtre Inspector | Elle « displays the properties of the current selection of one or more GameObjects, assets, or components » (**documenté**, manuel, *The Inspector window*), un composant sous son en-tête, comme ici. |
| **Godot**, l'inspecteur | Le dock Inspector | Il « lists all properties of an object, resource, or node » (**documenté**, *Inspector dock*) : les propriétés exportées d'un nœud, pas des composants. |
| **Unreal**, les dessinateurs | `IPropertyTypeCustomization` | La personnalisation du panneau Details « for structs » (d'après un résultat de recherche, **non relu** : la page d'Epic ne s'affiche pas sans JavaScript) ; la rotation d'un composant y est un `FRotator`, des angles, sans quaternion à relire (**supposé**). |
| **Unity**, les dessinateurs | `PropertyDrawer` | Il fixe l'aspect d'un champ dans l'Inspector (**documenté**, *PropertyDrawer*). L'Inspector garde un indice d'angles à côté du quaternion, `m_LocalEulerAnglesHint` dans les scènes YAML (**supposé**, vu dans des fichiers `.unity`, non documenté) : le nom `eulerHint` en vient. |
| **Godot**, les dessinateurs | `EditorInspectorPlugin` | La rotation d'un `Node3D` est « edited in degrees in the inspector », en angles d'Euler ou en quaternion selon `rotation_edit_mode` (**documenté**, *Node3D*) ; le dessinateur de propriété d'un plugin est `EditorProperty` (**supposé**). |
| **Unreal**, la caméra | La caméra de la vue de niveau | Ses gestes (clic droit tenu, Alt+clic gauche, bouton du milieu, molette, F) : **documenté** (*Viewport Controls*, ADR-0036 [1]). Elle n'est pas un acteur : **supposé** (`FEditorViewportClient`). Levain en prend les gestes, mais **le sens du pan est un choix, pas une copie** : le contenu suit le curseur (la vue va à l'opposé), comme la main d'Unity et de Godot (**supposé**, non relu). Unreal a, je crois, un réglage pour l'inverser (*Invert Middle Mouse Pan*, **supposé**, non relu) : le morceau 11 le lit avant de le retourner. |
| **Unity**, la caméra | La caméra de la vue Scène | Une vitesse bornée et le *Dynamic Clipping*, qui « calculate the Camera's near and far clipping planes relative to the viewport size of the Scene » (**documenté**, ADR-0036 [4]). Cette « viewport size » est, je crois, `SceneView.size`, le zoom autour du pivot, et non la taille de la fenêtre en pixels (**supposé**, UnityCsReference, non relu) : Levain fait donc de même, ses plans suivent la vue sans réglage, mais ils se mesurent sur la profondeur de la scène (`clipPlanesFor`) et non sur le zoom. |
| **Godot**, la caméra | La vue 3D de l'éditeur | Clic droit tenu pour regarder et voler en WASD, E et Q, la molette pour la vitesse, le **bouton du milieu pour l'orbite** (**documenté**, ADR-0036 [7]) : Levain suit Unreal, où le bouton du milieu fait glisser et Alt+clic gauche tourne. |
| **Unreal**, les modes | Édition, puis *Play* (PIE) | Échap arrête le jeu, Alt+S simule (**documenté**, ADR-0036 [2]), Alt+P joue (**supposé**) ; PIE joue une copie du niveau, que notre « Jouer (sans retour) » ne fait pas avant M7.5 (**supposé**). |
| **Unity**, les modes | Le mode Play | Un mode Édition où rien ne tourne, Play à part, et les changements faits en jeu perdus à l'arrêt (**documenté**, GESTES.md [U20]) ; ici, rien n'est restauré avant M7.5. |
| **Godot**, les modes | Jouer la scène | Le jeu tourne dans une fenêtre à part, l'éditeur ne change pas de mode (**supposé**) : l'écart est assumé. |
