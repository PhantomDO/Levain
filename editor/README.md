# `editor`

## Rôle

La bibliothèque de l'éditeur ([ADR-0018](../docs/adr/0018-moteur-plugins-et-jeu.md),
[ADR-0034](../docs/adr/0034-reflexion-des-composants.md), choix 6B) : ce que l'éditeur ajoute à un programme du
moteur, au-dessus d'`app` (SPECS §7). Le jeu en construit l'exécutable : son `main.cpp` compilé une seconde fois,
avec une définition qui enveloppe son démarrage par `withEditor`, et lié à `levain::editor`. Levain construit
ainsi `levain_sandbox_editor` (dans `sandbox/`) pour ses démos et sa CI ; *Rando* construira `rando_editor` à côté
de `rando`, dont la cible ne change pas.

**État après M7.2 : l'éditeur, c'est le jeu avec deux panneaux de plus**, plus, depuis M7.7, des modes, la Vue et une
caméra qui se pilote comme celle d'Unreal (invariant 20). Il a le branchement sur la boucle,
l'option `--select`, le bilan que lit la CI, **la hiérarchie** : les entités placées (`Transform`), rangées par
`flecs::Parent` (ADR-0015), et un nœud *Singletons*, qui sélectionne seulement ; et **l'inspecteur** : les
composants de l'entité choisie, un widget par champ, lus dans la description de flecs, aux valeurs de l'image. Une
donnée d'auteur (`Authored`) s'édite, le reste est grisé ; trois dessinateurs lui donnent des angles, des noms
d'entité et d'asset. Il n'a pas encore :

- **de menu contextuel riche** : le clic droit sur la Vue (sans vol) n'a qu'une entrée, « Cadrer la sélection » ; les
  autres (Entité, M7.3) viennent avec leur milestone ;
- **d'écriture vue dans son image** : les panneaux passent après les pas et le choix de la caméra (app.cpp:875-891) ;
  une valeur tapée dans l'inspecteur se voit à l'image suivante ;
- **d'enregistrement** : rien n'écrit la scène, et fermer la fenêtre arrête la boucle sans rien demander
  (app.cpp:106-107) ;
- **d'annulation** : `commitEdit` écrit la copie par `setComponentValue` sans garder l'ancienne valeur
  (editor/src/inspector.cpp:543-552) ;
- ni raccourci qui fasse quoi que ce soit d'autre que jouer, arrêter ou régler le temps réel : Ctrl+S, Ctrl+Z et les
  outils sont grisés jusqu'à leur milestone (invariant 16 et le paragraphe des menus ci-dessous).

**La console est écrite** (M7.7, morceau 14) : la fenêtre « Console » lit le journal du processus par un récepteur de
`core::log` (`withEditor` le pose avant `runApp` : ce que la fenêtre et le device écrivent en s'ouvrant y est), dans un
anneau de 2 000 messages ; niveaux, compteurs de tous les messages reçus, recherche, Effacer, Réduire. Elle s'ancre
au nœud du bas (`app.ui.dock.bottom`, avec Image, Passes et Scène : la disposition du morceau 15), dessinée *avant* elles :
après, ImGui lui donnerait l'onglet visible du nœud à l'ouverture. Dans un nœud étroit, la barre d'outils s'enroule
(`fitsOnLine`) et le champ de recherche a sa propre ligne. De bout en bout : `gpu.editor-console.*`
(`tests/editor_console_gpu.cpp`), qui lit ce que les fenêtres ont dessiné par `EditorOptions::afterUi`.

**La table des raccourcis est écrite, défauts seuls** (M7.7, morceau 10, `shortcuts.hpp`, ADR-0036, décision 13) : par
action son nom (clé du catalogue), au plus deux accords, un contexte (vol, Vue, global, jeu ; le vol passe avant la
Vue, le jeu vaut seul sous sa route). Le vol et les outils vont **par position** (le scancode d'USB), les accords à
Ctrl ou Alt **par lettre**. `displayNameOf` montre la touche de la disposition d'aujourd'hui (`App::keyAtPosition` :
« Z » à la place du W sur un AZERTY) ; `tokenOf` et `chordFromToken` fixent le nom stable des préférences (`pos:26`,
`Ctrl+S`, jamais un nom de SDL ou d'ImGui). **Branchée** (morceaux 11 et 12) : les gestes lisent le vol et F par
`positionHeld` et `actionFor` (`gestures.cpp`), Alt+P par `letterChordPressed` et Échap par `positionHeld` de l'action
« Arrêter » (`mode.cpp`), les raccourcis des menus par `letterChordPressed` et `toolActionOf` (`actions.cpp`). **En vol,
la Vue se tait** (`actionFor`) : sous le clic droit tenu, ni R ni Espace ne changent d'outil, F ne recadre pas ; seul le
global vaut encore (Ctrl+S). La fenêtre « Raccourcis » (morceau 16, invariant 22) les change et les enregistre. Équivalents : les raccourcis d'éditeur d'Unreal, le *Shortcuts
Manager* d'Unity, les *Editor Settings > Shortcuts* de Godot. Test : `tests/editor_shortcuts_test.cpp`, et de bout en
bout `gpu.editor-shortcuts.*`. **Le contrat avec ImGui** : `actionFor` sert les accords de position et le contexte *jeu* ; les accords de lettre passent par `ImGui::Shortcut(imguiChordOf(...))`, et un champ de texte actif (`ActiveContexts::textInput`) ne laisse rien au vol, à la Vue ni au global. Alt-Gr tenu ne vaut aucun accord. **Fait au morceau 11** : le relâchement d'un vol est un clic pour ImGui, et le menu contextuel de la Vue ne s'ouvre que si le clic droit n'a pas volé (`GestureState::flew` : bougé au-delà de `ClickSlopPixels`, une touche de vol ou la molette) ; `playFlightReleaseIsAClick` épingle toujours le comportement d'`app` seul, sans l'éditeur.

**Les polices** (morceau 17, `withEditor`) : Inter, Noto Sans derrière et Material Symbols pour les icônes (`ui/fonts.hpp`,
`ui/icons.hpp`), lues dans `assets-cache/Fonts/` par les racines d'assets de l'`App` (ou `EditorOptions::fontRoots`) ; la console garde ProggyForever,
Inter derrière (`Console::font`). **Une police absente arrête l'éditeur avant sa première image**, en nommant le fichier et
`tools/fetch-assets.sh` (`gpu.editor-console.*`, `playMissingFont`) : les tests de l'éditeur posent `fontRoots = {assets-cache}`
et seulement elle : leurs racines d'assets restent `data/`, sans le scan et les `.meta` des 86 fichiers d'`assets-cache`. Le bouton de mode et son bandeau portent une icône (`modeButtonLabel`).

**Le style et les repères d'axe** (morceau 17b, `withEditor`, ADR-0036 décision 15) : `withEditor` donne à l'`App`
`ui::applyEditorStyle(style, displayScale)` comme style (`app::setUiStyle`) : réglé à l'ouverture, après les polices, puis
refait par la boucle à chaque changement de zoom du système (`WindowEventType::ScaleChanged` ; `gpu.editor-modes.*`, ligne
`window scale`) ; engine/ui/README : sombre, plat, un seul accent bleu pour l'actif ou le choisi, fonds opaques, rondeurs de
2 à 4 px, pensé pour Inter à 15 px. Le sandbox seul et *Rando* gardent le style d'ImGui. **Les boutons grisés de la barre
d'outils** (`disabledButton`) sont dessinés tels que le test des contrastes les mesure : `TextDisabled` plein (`DisabledAlpha`
poussé à 1 le temps du bouton, sans quoi `BeginDisabled` le multiplierait) et sans contour (`FrameBorderSize` à 0 : un
contour redessinerait la forme retirée ; les champs de saisie gardent le leur) ; `tests/editor_menus_test.cpp` lit ce que la
barre dessine. **Les entrées de menu grisées** (`greyedMenuItem`) sont dessinées de même (`TextDisabled` plein, libellé et
raccourci), non à `DisabledAlpha` : à 0,8 sur la 4070 le libellé ne se distinguait pas du texte. Le bandeau de la barre de
mode et celui d'« Afficher », dans la Vue, prennent `ui::overlayBackground` (un gris de la palette, à 0,93) et le rappel de touche
est en `Text`, lisible sur une scène blanche. Le menu « Afficher » s'ouvre sous son bouton. **Le champ de l'inspecteur** garde
la place de trois nombres avant le libellé (`labelColumnFor`), et une matrice passe à moins de quatre nombres par ligne quand
la fenêtre est étroite (`numbersPerRow`) : à 125 % et 150 % la colonne fixe de 10 em coupait les valeurs. **La hiérarchie** colorie sa ligne choisie en accent (`pushSelectedRowColors`, comme la
constante courante d'une liste déroulante), les en-têtes de composant restent gris. **L'inspecteur** pose
`ImGuiSliderFlags_ColorMarkers` (`AxisMarkers`) sur chaque `vec2`, `vec3` et sur les trois angles d'une rotation : X rouge,
Y vert, Z bleu (X et Y seuls pour un `vec2`), comme les champs de position d'Unreal ; **jamais** sur un nombre seul (un
repère rouge sans axe) ni sur une ligne de tableau ou de matrice (`[0-3]`, quatre nombres qui ne sont pas des axes).
ImGui choisit les couleurs lui-même (`GDefaultRgbaColorMarkers`) : le test les cherche dans la liste de dessin
(`tests/editor_test.cpp`, « un vecteur de trois composantes porte les repères… », trois cas) et échoue si le drapeau
manque, ou s'il fuit sur un nombre seul. Que `withEditor` règle bien le style, c'est `gpu.editor-modes.*` (sans l'appel,
l'éditeur resterait au style d'ImGui, et rien d'autre ne le dirait) ; la ligne choisie de la hiérarchie,
« la ligne choisie de la hiérarchie prend l'accent… ». Équivalents : le panneau Details d'Unreal montre X, Y, Z en rouge,
vert et bleu ; les libellés X, Y, Z des champs de vecteur d'Unity ne sont pas colorés, ceux de Godot 4 le sont (**supposé**
pour les trois : l'aspect est connu de l'interface, non relu dans une documentation pour ce morceau).

**Les menus, les barres et la fenêtre Aide sont écrits** (M7.7, morceau 12, `menus.hpp`, `bars.hpp`, `actions.hpp`,
ADR-0036, décisions 12 et 13). **La barre de menus** : Fichier, Édition, Entité, Fenêtre (celui du morceau 8, au
milieu de la barre) et Aide ; chaque entrée montre le raccourci de la table, selon la disposition du clavier. **Une
action grisée tant que son milestone n'est pas là** (`availabilityOf` : Enregistrer, Annuler, Rétablir en M7.3, les
outils en M7.4, Simuler en M7.5, Entité > Créer… en M7.3) : le menu la montre
éteinte, dit au survol « Arrive en M7.3 », et le clic ne fait rien. **Le choix pour Ctrl+S** : l'entrée est grisée
*et* le raccourci reste routé vers l'action, qui, grisée, le dit à la console (« Enregistrer : arrive en M7.3 » ; la
barre d'état le montre) et compte le refus (`ActionCounters::declined`). Un Ctrl+S qui ne répondrait rien laisserait
croire qu'une scène est enregistrée (règle n°7) ; c'est aussi ce qui permet au test « Ctrl+S atteint son action » de
l'observer. **La barre d'outils** (sous les menus) : le bouton de mode, Simuler (grisé), les trois outils (grisés), la
vitesse de la caméra de l'éditeur (`Editor::camera.flySpeed`, un curseur logarithmique borné par `clampFlySpeed`) et
la case **« Temps réel »**, la même action que Ctrl+R (`App::realTime` : décochée, le temps de la scène que lisent l'eau,
l'herbe, les matériaux et les squelettes ne court plus, et repart d'où il s'était arrêté). **Le temps réel ne se règle qu'en Édition** : jouer le force à vrai et garde le choix de l'Édition, revenir le rend (`switchRealTime`) ; en jeu la case est grisée et Ctrl+R le dit à la console. Livré ici (morceau 12) ; le morceau 17 garde le menu Afficher. **La barre d'état** (au
bas) : le dernier message du journal et les compteurs d'avertissements et d'erreurs, lus dans l'anneau de la console
sous son mutex (`statusOf`). **La fenêtre Aide** (Aide > Raccourcis) liste une ligne par entrée de la table (l'action,
son accord tel que la disposition d'aujourd'hui l'écrit, son contexte, la touche d'Unreal et celle d'Unity), avec une
recherche (`helpRowsOf`, `helpRowMatches`). **Les raccourcis** (`shortcutsPressed`, appelé une fois par image après
les fenêtres) : les accords de lettre par `ImGui::Shortcut` en route globale (la route du *focus* d'ImGui veut la
fenêtre courante, que la Vue ne prête pas depuis `ui` : la Vue « active » est `viewActiveIn`, avec le survol d'ImGui de
`View::hovered` : une fenêtre, un menu ou un popup sous la souris, par-dessus la Vue, la prive de W et de Ctrl+R), les
touches d'outil par position, lues dans les événements bruts de l'image (`keyPressesOf` sur `App::rawFrameEvents` : la
position, la lettre de la disposition, les modificateurs ; **une répétition n'est pas un appui**, une touche tenue ne
prend l'outil qu'une fois). **Une seule règle pour les touches de la Vue** : `viewActiveIn` décide pour les outils,
Ctrl+R et le F des gestes ; un geste de souris, lui, commence sous le pointeur (`GestureContext::sceneHovered` et le
rectangle de la Vue), le focus n'y comptant pas : la Vue focalisée, un clic droit sur la hiérarchie ne vole pas.
**Un champ de texte actif garde son Ctrl+Z** (l'élément actif passe avant la route globale ; Ctrl+Maj+Z de même, le test le montre), Alt+P ne joue pas non plus pendant qu'on tape, mais **Ctrl+S atteint Enregistrer même dans un champ** (voulu ; au M7.3 on valide le champ avant d'enregistrer), **en vol,
aucune touche d'outil** (`toolActionOf`) : W vole (`actionFor` rend Avancer), R et Espace se taisent, le vol étant celui
des gestes (`inFlight`, qui commande aussi la capture), non le bouton droit tel qu'ImGui le voit ; avec Ctrl, la touche
ne prend pas d'outil. Alt+P reste lue par `playShortcutPressed` (mode.cpp), la table n'en compte pas deux. **Une seule
caméra** (`Editor::camera`) : le curseur de la barre d'outils et la molette en vol règlent la même vitesse, bornée par
`clampFlySpeed`, et le vol suivant va à cette vitesse. **La modale « Quitter ? »** ouverte, ni les menus, ni la barre
d'outils, ni les raccourcis ne font rien (`globalChordsAllowed`, et la modale qui bloque les clics derrière elle).
**Pas encore** : la touche d'outil n'a que le « refus à la console » jusqu'à M7.4. Équivalents : la barre
de menus et la barre d'outils d'Unity et d'Unreal, « Editor Preferences > Keyboard Shortcuts » d'Unreal, le Shortcuts
Manager d'Unity, le menu Aide de Godot. Tests : `tests/editor_actions_test.cpp` et `tests/editor_menus_test.cpp` (un
ImGui sans fenêtre : Ctrl+S, le champ de texte, les clics des menus et des barres), et de bout en bout
`gpu.editor-menus.*` (`tests/editor_menus_gpu.cpp`).

**Sa caméra se pilote** (M7.7, morceaux 9 et 11, `camera.hpp`, `gestures.hpp`, invariant 20) : la logique est en
fonctions libres, les gestes lisent l'input brut (`App::rawFrameEvents`) et `App::cameraOverride` en est la sortie.

**La grille, le menu « Afficher » et la vue de jeu** (M7.7, morceau 17, `grid.hpp`, `show.hpp`, décisions 13 et 15) : la
grille est le plan du sol (Y en haut, main droite), des lignes de debug que l'éditeur dessine dans l'étape Transparent **par sa
propre `DebugLinesPass`**, créée une fois pour la cible de la scène : un appel par command list, et le contour de la
sélection du programme a la sienne (`render/debug_lines.hpp`). `gridLinesFor(GridView)` (la position de la caméra,
l'exposition, le plan lointain, et si on le lui donne l'image) rend 6 060 tronçons au plus (`MaxGridLines` : trois familles
de 202 lignes en 10 tronçons). **L'écart suit la
hauteur de la caméra par puissances de 10, et la grille passe de l'une à l'autre sans saut** (`gridLevelsFor`) : trois
familles à 10^(L−1), 10^L et 10^(L+1) mètres (`L` la partie entière du logarithme de la hauteur), la plus fine s'efface
quand la caméra monte, la plus grosse apparaît, celle de la hauteur passe de majeure à mineure ; à 9,9 m des lignes tous
les mètres et tous les 10 m, à 10,1 m les mêmes, au fondu près (avant, un seul écart par ordre : le sol couvert passait
de 50 m à 500 m d'un coup, et l'axe X à 60 m disparaissait). Une ligne que deux familles ont en commun est tracée par les
deux, leurs opacités se composent : c'est ce qui la fait majeure. **Chaque famille est un disque** autour du point du sol
sous la caméra, calé sur son pas pour que les lignes ne glissent pas : 50 pas, **et pas plus que les neuf dixièmes du plan
lointain** (`gridReachFor`, `farDistanceOf` lit le plan dans le frustum), chaque ligne s'arrêtant à la corde du disque,
là où son opacité est nulle : rien n'est coupé net par le plan lointain (une caméra à 100 m avait un pas de 100 m, une
portée de 5 km et une clôture à 1 km). **Mêlée par son opacité** (la passe a une variante `DebugBlend::Alpha`, opacité par
sommet) dans l'étape Transparent après le ciel, la grille s'estompe avec la distance au centre sans jamais noircir, **et
avec l'écart que deux lignes voisines ont à l'écran** (`projectedSpacing`, mesuré perpendiculairement à la ligne ;
`spacingFade` : rien sous 2 pixels, tout à partir de 6) : une ligne de 1 pixel sans anti-crénelage, que sa voisine suit à
2 ou 3 pixels, fait des marches d'escalier qui crépitent. **Ses couleurs sont divisées par l'exposition** du tonemapper :
un gris moyen (0,33, 70 % d'opacité, 95 % pour les majeures) qui assombrit une case claire et éclaircit une case sombre,
parce qu'une ligne plus claire que le sol se perd dans la case claire (l'AgX écrase les hautes lumières) ; les axes sont
saturés à luminance moyenne (l'AgX délave le reste), X rouge, Z bleu. Chaque ligne est coupée au volume de vue avant le
dessin (`clipToFrustum`, appliqué par `drawDebugLines` à toutes les lignes de debug) : une ligne de 100 pas qui passe sous
la caméra a un bout derrière l'œil. Relevées d'un rien (`GridLift`) : dans le plan même d'un sol (y = 0, un monde vide, un terrain), elles perdraient le test de profondeur à
égalité ; le sol du bac à sable est à y = −1, la grille y flotte un mètre au-dessus, ce qui est voulu (elle est le plan
y = 0 du monde). Le menu
**Afficher** est dans la barre de la Vue, à côté du bouton de mode, en Édition : « Grille » et « Vue de jeu » (son raccourci
vient de la table, comme dans les autres menus). **La vue de jeu cache les aides de l'éditeur** : la grille, et le contour de
la sélection du programme, par un seul drapeau, `App::editorHelpersVisible` (engine/app/README.md) que l'éditeur pose à la fin
de `ui` (`helpersVisibleIn` : Édition et pas de vue de jeu ; **Jouer cache aussi les aides**, comme le viewport de PIE
d'Unreal). **G** passe par `viewKeyActionOf` : à la position G, dans la Vue active (survolée ou focalisée), jamais en vol ni
dans un champ de texte ; le menu atteint la même action (`runAction` puis `applyShowAction`, un seul chemin).
**Pas encore** : l'état de « Grille » et de « Vue de jeu » est celui de la session, il n'est pas dans le fichier des
préférences (une clé de plus demande une décision de version) ; le contour de la sélection du bac à sable n'apparaît pas
dans l'éditeur avant M7.4 (la sélection au clic), son arrêt en vue de jeu est donc écrit mais sans scénario. Équivalents :
le menu *Show* du viewport d'Unreal (la grille, *Game View* sous G : **documenté**, GESTES.md [E1]), les *Gizmos* et la
grille de la Scene View d'Unity (**supposé**), View > Grid et View > Gizmos de Godot (**supposé**). Tests :
`tests/editor_grid_test.cpp` (la logique : nombre de lignes, familles et leur continuité à 10 m, portée, écart à l'écran, gris, axes, centre, G, aides) et de bout en bout
`gpu.editor-grid-*.*` (`tests/editor_grid_gpu.cpp`, un ctest par scénario : le menu, la touche, jouer, les pixels) : ce que la grille a
dessiné est lu sur l'étape de rendu (`GridState::framesDrawn`, `lastLines` : les lignes envoyées après la coupe), non sur
la case ; le scénario des pixels laisse de côté les barres que le texte d'ImGui recouvre (`View::modeBarRect`,
`ShowMenuLayout::bar`), dont les pixels changent d'une exécution à l'autre.

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
    Échap est lue sur l'input brut par la position que la table donne à « Arrêter » (`stopPressed`) : elle vaut partout où le jeu reçoit le clavier,
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
    en bas (`DockNodes::left, center, inspector, bottom`). **Le menu « Fenêtre »** (`window_menu.hpp`), un des cinq de
    la barre de menus (morceau 12, `drawMenuBar`), ferme et rouvre chaque fenêtre par son drapeau
    (`windowEntriesOf`) ; les fenêtres sont fermées aussi par leur croix. Une fenêtre fermée puis rouverte
    retrouve son nœud, ImGui gardant le `DockId` de la fenêtre fermée (`SetNextWindowDockID` en `FirstUseEver` ne sert
    qu'à la première ouverture). La disposition est gardée d'une session à l'autre : voir le point 20. Le sandbox et *Rando* gardent leur
    disposition et leurs fenêtres sans croix (`UiLayer::editorLayout`, posé par `withEditor`) : rien n'y rouvrirait
    une fenêtre fermée. La ligne de la CI
    `editor.view width=W height=H -- vue : W×H` donne la taille demandée à la dernière image (`ci-programs.sh editor` la
    veut non nulle). Pas encore : l'onglet caché et la Vue repliée sont testés en posant l'ancrage et le repli par le
    code (`playHidden`), non à la souris ; glisser ou désancrer la Vue n'est pas testé ; la barre de menus n'existe que
    panneaux ouverts ; **`StableFramesBeforeResize` n'est pas mesuré sur un vrai glissé de séparateur à 144 Hz** (cette
    machine n'a pas d'écran à 144 Hz), à faire quand un écran rapide est sous la main.
20. **La disposition et les préférences** (M7.7, morceau 15, ADR-0036, décision 10 ; `preferences.hpp`, `layout.hpp`,
    `platform::prefPath`). L'éditeur garde, dans `preferences.cfg` sous `SDL_GetPrefPath("Levain", "Editor")`
    (`%APPDATA%\Levain\Editor\` sous Windows, `$XDG_DATA_HOME/Levain/Editor/` sous Linux), un fichier texte versionné
    (`levain-editor 1`) : `[editeur]` (la vitesse de la caméra, le choix de « Temps réel » en Édition), `[fenetres]` (les
    sept fenêtres ouvertes ou non), `[raccourcis]` (vide tant que personne n'en change : l'absence d'une action, ce sont
    les défauts ; le morceau 16 y écrit, pour les seules actions changées, les accords par leurs noms stables `tokenOf`,
    invariant 22) et, **à la fin, tel quel**, `[disposition]` : le texte d'ImGui (`SaveIniSettingsToMemory`), dont
    l'`imgui.ini` n'est pas écrit (ADR-0032 : il changerait d'une exécution à l'autre). Le lecteur refuse le fichier
    entier à la première faute (version inconnue ou plus récente, section ou clé inconnue ou en double, valeur hors
    bornes, accord illisible), avec le numéro de ligne. **Au démarrage**, `withEditor` relit le fichier avant la première
    image : la disposition d'ImGui est rechargée (`LoadIniSettingsFromMemory`), l'espace d'ancrage existe donc et
    `drawEnginePanels` ne rappelle pas `buildLayout` (il ne bâtit que ce qui manque) ; les nœuds de l'éditeur
    (`app.ui.dock`) sont **retrouvés par le nom des fenêtres** dans les réglages relus (`dockNodesOfSavedLayout`), pas
    par des identifiants gardés à part. Un fichier refusé est dit à la console (erreur), mis de côté en `.bad` et la
    disposition par défaut se construit ; une disposition sans espace d'ancrage est écartée de même, le reste des
    préférences gardé. **À la fermeture** (`FrameHooks::finish`) et aussitôt après « Réinitialiser la disposition »
    (`Fenêtre`, dernière entrée : `resetEditorLayout` rebâtit le défaut, toutes les fenêtres rouvertes et ancrées sur leurs
    nœuds, vitesse et « Temps réel » gardés), il est réécrit par un fichier voisin renommé. Un second fichier refusé remplace le
    premier `.bad` ; si la mise de côté échoue, la session n'enregistre pas. **Un fichier d'une version plus récente**
    (`isFromNewerVersion`) n'est ni mis de côté ni écrasé : une branche plus ancienne le rétrograderait en v1 ; la
    session n'enregistre pas et la console le dit. Le fichier voisin porte un nom par écriture
    (`temporaryFileFor`, `<fichier>.<id>.tmp`) : deux éditeurs lancés ensemble ne se tronquent pas l'un l'autre
    (un plantage au milieu d'une écriture laisse un `.tmp`, que rien ne nettoie). **Sous Windows, un lecteur qui tient le
    fichier ouvert en refuse le remplacement, et un remplacement en cours en refuse l'ouverture** : l'écriture et la
    lecture réessaient ce refus-là seulement (`isTransientlyBusy` : les codes Win32 5, 32 et 33), pendant 3 s mesurées
    sur l'horloge (`BusyFileRetry`, `retryWhileBusy`, comme `ImportantFileWriter` de Chromium) ; toute autre erreur est
    dite aussitôt ; un refus qui dure l'est au bout du délai, et laisse le fichier entier. La lecture lit le fichier ouvert jusqu'au bout, jamais à une taille mesurée
    par son chemin avant de l'ouvrir : un remplacement entre les deux donnait le début de l'autre fichier, valide s'il
    atteint `[disposition]` (un hybride). **Une fenêtre renommée dans une
    disposition enregistrée** (`[Window][Ancien nom]`) flotte pour toujours à côté de la nouvelle : « Réinitialiser la
    disposition » la rend à la défaut. **Une exécution scriptée n'en
    lit ni n'en écrit** (`isScriptedRun` : `--seconds`, `--steps`, `--time`, `--capture`, `--input-script`), et
    `platform::prefPath`, qui crée le dossier, n'est alors pas même appelé. Les tests posent
    `AppSettings::testInteractive` (une exécution scriptée n'a pas de préférences) **et injectent un dossier jetable**
    (`EditorOptions::preferencesDirectory`) : `preferencesFileFor(scriptedRun, testRun, ...)` ne laisse jamais une
    exécution de test (`testRun`) atteindre le dossier de l'utilisateur, un test qui oublie le dossier n'a simplement pas de
    préférences (contre-test : `tests/editor_preferences_test.cpp`). Un fichier tronqué (sans `[disposition]`) est refusé
    comme les autres, et l'écriture est à l'abri d'un plantage (fichier voisin renommé), pas d'une coupure de courant. De
    bout en bout : `gpu.editor-preferences-*.*`, un test par scénario (`tests/editor_preferences_gpu.cpp`) ; le format : `tests/editor_preferences_test.cpp` ;
    `prefPath` sous `%APPDATA%` au portable : `tests/paths_test.cpp`. Pas encore : l'écriture périodique (un plantage perd
    la session ; l'enregistrement se fait à la fermeture), la disposition d'un sandbox ou de *Rando* (leur
    `editorLayout` est faux), le choix du mode et la sélection.
21. **La caméra de l'éditeur** (ADR-0036, décisions 6, 8 et 13 ; `gestures.hpp`, `camera.hpp`, `scene_bounds.hpp`) : un
    état d'`Editor` (`Editor::camera`), jamais une entité, qui part de celle du jeu (`editorCameraFrom`) et que
    `App::cameraOverride` impose chaque image en Édition (en jeu, l'imposition est levée et la caméra du jeu voit). Les
    gestes lisent l'**input brut** (`App::rawFrameEvents`, avant le filtre de route et celui de la capture), dans `frame`
    pour que la capture soit celle de la même image : un geste ne **commence** que si le curseur est dans la Vue (et
    qu'elle est la fenêtre sous la souris), jamais ailleurs, et ne se continue pas quand la souris y entre bouton tenu.
    *Clic droit tenu* : le regard au mouvement relatif, les touches de vol **par position** (`flightMoveOf` : ZQSD sur
    un AZERTY), Maj court (×3), la molette règle la vitesse ; la souris est **capturée panneaux ouverts compris**
    (`App::mouseCaptureOverPanels`) et **seulement pendant ce vol** : l'orbite (Alt+clic gauche), le pan (bouton du
    milieu), le dolly (Alt+clic droit) lisent le mouvement relatif sans la prendre. Hors geste, la molette dans la Vue
    zoome (`dollyCamera`). F cadre la sélection là où valent les touches de la Vue (`viewActiveIn`), jamais en vol
    (`framingOf` sur `entityBoxOf` : la boîte de son mesh si le modèle est
    sur le GPU, sinon un cube de côté 1 à sa position), comme le double-clic sur une ligne de la hiérarchie et
    l'entrée du menu du clic droit. Les plans de découpe (`editorClipPlanes`) : le lointain d'abord, celui des bornes de la scène sans
    descendre sous le défaut (1000 m) ni passer 1e5 m ; le proche du seul rapport (lointain / 10 000), jamais de l'entité
    la plus proche : un terrain, l'eau ou l'herbe, tracés par leur passe, ne sont pas des entités et ne sont pas rognés. **Le clic droit qui
    n'a pas volé** (`GestureState::flew` faux) ouvre le menu de la Vue au relâchement ; à la fin d'un vol, ImGui y voit
    un clic aussi, et le menu reste fermé. Le vol désactive le champ de texte actif (`ImGui::ClearActiveID`, de
    l'API interne) : Retour arrière et les flèches ne l'éditent pas. **Fin d'un vol** : le curseur revient au point de l'appui
    (`App::mouseReturnTo`, `platform::warpMouse` avant le mode relatif : le banc hors écran ne voit pas le warp de SDL,
    le test épingle le point posé puis consommé). Un vol ne dure pas plus de `MaxFlightSeconds` par image
    (`clampFlightSeconds`), et une fenêtre cachée remet les gestes au repos (les relâchements se perdent). **En jeu**,
    ni menu de la Vue (`View::menuAllowed` faux hors Édition, le menu ouvert se ferme) ni cadrage : le clic droit est au
    jeu. Panneaux fermés, la scène est « survolée » sauf sous une fenêtre d'ImGui (`WantCaptureMouse` de l'image
    d'avant). La boîte de la scène est gardée (`SceneBoundsCache`, refaite à l'écriture de l'inspecteur, au changement
    de mode ou du nombre d'entités ou de modèles, et toutes les 60 images). **Limites** : F et le double-clic sur un
    parent cadrent **son cube unité** (la boîte de ses enfants n'est pas calculée) ; une entité sans `WorldTransform`
    ne se cadre pas ; la boîte gardée peut retarder de 60 images les plans de découpe quand un programme déplace des
    entités en Édition sans passer par l'inspecteur. Pas encore : l'orbite et le pan ne capturent pas
    la souris (le curseur butte au bord de l'écran, Unreal le cache), la touche F n'agit pas hiérarchie focalisée,
    et le sens du pan n'est pas un réglage. Tests : `tests/editor_gestures_test.cpp` et, de bout en bout,
    `gpu.editor-gestures.*`.
22. **Changer les raccourcis** (M7.7, morceau 16, ADR-0036, décision 13 ; `shortcut_edit.hpp`, `shortcut_window.hpp`).
    Édition > Préférences > Raccourcis… ouvre la fenêtre « Raccourcis » : une ligne par action de la table (son nom, ses
    deux accords selon la disposition du clavier d'aujourd'hui, son contexte), un filtre par nom d'action ou par accord
    (`shortcutRowMatches`), « Réinitialiser » par ligne et « Tout réinitialiser » (une confirmation). **Un clic sur un
    accord, puis la touche** : la saisie (`ShortcutWindow::capturing`) coupe tout le reste pour sa durée, ni
    `ImGui::Shortcut` (`ShortcutInputs::globalAllowed`), ni les touches de position (`Editor::presses` ne va qu'à la saisie),
    ni les gestes (`GestureContext`), ni Alt+P, ni Échap qui arrête le jeu, ni **F1** (`UiLayer::panelsKeyBlocked` : elle ne ferme pas
    les panneaux, donc pas la fenêtre qui la lit). **Échap annule la saisie et n'est jamais capturée** : aucune action ne peut la tenir par un clic ;
    « Arrêter » (Échap par défaut) peut s'en éloigner, et la retrouve par « Réinitialiser » (ADR-0036, décision 13). **La
    saisie ne dure que là où elle lit le clavier** (`captureAllowed`) : elle est abandonnée (`abandonCapture`) dès que la
    fenêtre n'a plus le focus (un onglet caché, la modale « Quitter ? »), qu'un champ de texte est actif (le filtre, ou
    celui d'un autre panneau : « s » tapé n'est pas un accord), que l'application perd le focus du système, ou que le mode
    change (`changeMode` : en jeu les touches de l'éditeur ne lui parviennent plus, et les cases des accords sont
    grisées). **La touche qui finit la saisie ne part pas aussi comme raccourci** : `ui` lit la saisie une fois, au
    début de l'image. Sont refusés (`captureChord`) : F1 seule
    (réservée, un message le dit), un modificateur seul (la saisie attend la touche suivante), ce que `chordFromToken` ne
    relirait pas (une flèche pour une lettre), Alt-Gr (AltGr+E d'un AZERTY tape « € », ce n'est pas un accord) **et
    Ctrl+Alt+lettre, sur toutes les plateformes** : sous Windows SDL ne distingue pas Alt-Gr d'un Ctrl+Alt (`altGr` y reste
    faux), AltGr+E y aurait été lié « Ctrl+Alt+E » là où Linux le refusait ; la même frappe donne le même résultat, au prix
    d'un Ctrl+Alt+lettre que Linux aurait accepté. **Une action tenue** (le vol, « Arrêter » : `isHeldAction`) **ne lit aucun
    modificateur** (`positionHeld` : Maj accélère le vol sans le couper) : la saisie retire ceux qui sont tenus (Maj+E pour
    « Avancer » est la touche E, en conflit avec « Monter » : le conflit est nommé, et la fenêtre dit sur sa ligne de message
    que cette action ignore les modificateurs), et une ligne de fichier qui en met un sur l'une d'elles est refusée comme un
    accord d'un autre genre (dite, gardée telle quelle, les défauts restent) : sans cela l'accord serait affiché et
    enregistré sans jamais être lu comme tel, et prendrait en silence la touche nue d'une autre action. **Le genre de
    l'accord suit celui de l'action** : une position garde le scancode, une lettre le caractère de la touche
    (`defaultKindOf`). **Un conflit** (`conflictsOf`) est
    un accord déjà pris dans le même contexte, ou entre le global et n'importe quel autre contexte (le vol et la Vue ne se
    disputent rien, W vole et prend l'outil) ; une position se compare à une lettre par `keyAtPosition` (`chordsCollide` :
    sur un AZERTY la lettre Z est la position du W). Il est **nommé** (« Ctrl+S : Enregistrer ») et rien ne change avant
    « Remplacer » (`replaceChord`, l'*Override* d'Unreal : l'accord est retiré à l'autre action, qui garde son second accord
    à la première place) ; « Annuler » l'abandonne. L'autre place de la même action n'est pas un conflit : l'accord y est
    vidé. **« Réinitialiser » suit la même règle** (`requestReset`) : si un défaut est tenu maintenant par une autre
    action, le conflit est nommé et rien ne change avant « Remplacer ». Quand la disposition change (`App::keymapChanges`), la table est
    revérifiée (`conflictsIn`) : un conflit né ainsi va à la console, **aucun accord n'est rebranché en silence** ; une
    candidate en attente est abandonnée. **Le fichier** : `[raccourcis]` tient une ligne `action = accord, accord` par action
    qui diffère des défauts (`linesOf`, le nom de l'action est sa clé de catalogue ; une action sans accord s'écrit sans
    rien après le « = »), écrite tout de suite après un changement (`savePreferencesNext`) et à la fermeture. À la lecture
    (`shortcutsFromLines`, `applyShortcutLines`) un fichier qui se lit (`parsePreferences` refuse le fichier entier s'il
    est malformé) peut encore porter une ligne que le code refuse : accords illisibles, ligne écrite deux fois, **accord
    d'un autre genre que l'action** (une lettre pour « Avancer » ne partirait jamais : `positionHeld` ne lit que les
    positions), **modificateur sur une action tenue**. Elle est écartée et dite, les défauts de l'action restent, et **la ligne est gardée telle quelle** à
    l'enregistrement (`AppliedLines::refused`, `withKeptLines`) tant que la fenêtre n'a pas écrit son action : une saisie,
    « Remplacer » (pour l'action et pour celles qu'il dépouille), « Réinitialiser » ou « Tout réinitialiser » l'oublient
    pour de bon (`forgetWrittenLines`), sans quoi « Avancer = Z » revenait après un changement puis « Réinitialiser » et se
    refusait de nouveau à chaque démarrage ; **une action inconnue (un fichier d'une autre branche) est gardée et
    réécrite** de même, jamais oubliée ; un conflit du fichier est dit sans
    rien changer. Sous une
    exécution scriptée (pas de dossier de préférences) : les défauts, jamais écrits. **La fenêtre** se cale sur la police
    (`shortcutWindowFit` : largeurs et hauteur en multiples de `ImGui::GetFontSize()`, toutes les lignes visibles à 125 %), s'ouvre
    sur l'espace libre (`WorkPos`, sous la barre d'outils), nomme dans sa phrase de conflit l'action qui voudrait l'accord,
    dit sur sa ligne de message ce que fait Échap pendant la saisie, abandonne la saisie quand « Tout réinitialiser » est
    cliqué, et grise « Simuler » et « Rendre la souris » (`rebindableFrom`, « Arrive en M7.5 ») : rien ne les lit encore.
    L'Aide écrit « (aucun) » d'une action sans accord, comme la fenêtre. Limites : les accords de
    position d'une action ne sont posés que par la touche physique (pas de saisie « par lettre » d'une action de position),
    et la fenêtre n'a pas de profils comme le Shortcuts Manager d'Unity. Tests : `tests/editor_shortcut_edit_test.cpp` (la
    logique) et, de bout en bout, `gpu.editor-shortcut-settings-*.*` (`tests/editor_shortcut_settings_gpu.cpp`, un
    ctest par scénario : changer et redémarrer, conflit, saisie, réinitialiser, menu, scripté, disposition, play, focus, leak,
    azerty, resetconflict, held, refusedreset). Équivalents :
    *Editor Preferences > Keyboard Shortcuts* d'Unreal (deux accords par commande, un conflit averti par *Override*,
    **documenté**), le *Shortcuts Manager* d'Unity (liste filtrable, conflits listés, profils, **documenté**), *Editor
    Settings > Shortcuts* de Godot (**supposé**, non relu).

## Fermer l'éditeur (ADR-0036, décision 11)

`withEditor` répond « demandons d'abord » à `FrameHooks::closeRequested` : la croix (`askToQuit`) arme la modale
« Quitter l'éditeur ? » (`drawQuitModal`, dans `ui`, au centre de la fenêtre), Quitter pose `App::quitRequested`, Annuler
garde l'éditeur. **Une seule modale** : une croix reçue modale ouverte n'en empile pas, et n'en prépare pas pour après.
`app` ne consulte jamais le programme sous `isScriptedRun` (`--seconds`, `--input-script`…) ni pour un signal. M7.3 y
ajoutera « Enregistrer ». **Sous la modale**, la route est celle de l'UI (`inputRouteWhileQuit` : ni clic ni touche n'atteint le jeu derrière)
et Alt+P ne joue pas (`globalChordsAllowed`) ; **les gestes de la caméra non plus** (`cameraGesturesAllowed`, dès la
croix reçue) : ils lisent l'input brut, que la route ne filtre pas, et un vol en cours (Alt+F4 en vol) s'arrête et rend
la souris (`gpu.editor-gestures.*`). **Échap annule** (en Édition il n'a rien d'autre à faire ; en jeu l'UI
garde le clavier, il n'arrête pas le jeu). Une croix reçue **fenêtre masquée** remet la fenêtre devant
(`platform::restoreAndRaise`), pour que la modale se voie (aucun test : le banc d'essai n'a pas d'événement
« masquée »). En jeu, souris capturée, Alt+F4 ouvre une modale qu'on ne clique qu'après Échap ou Maj+F1. `withEditor`
**remplace** le `closeRequested` du programme enveloppé. `QuitState` garde les rectangles
des boutons pour que le banc d'essai les clique (`gpu.editor-close.*`).

## Points d'entrée

- `requireEditorBackend(api)` : le refus de WebGPU, une fonction pure testée (`tests/editor_test.cpp`), que le `main` de
  l'exécutable éditeur appelle avant `runApp` (sandbox/src/main.cpp).
- `withEditor(start, options)` : la fonction de démarrage du programme, enveloppée. Elle choisit l'entité de
  `--select`, en ouvre les ancêtres, ajoute la hiérarchie et l'inspecteur aux fenêtres, et écrit à la fin le
  bilan que lit la CI, « éditeur : N entités, M champs dessinés ; sélection : chemin », N étant les lignes de la
  hiérarchie et M les champs de la sélection à la dernière image.
- `Preferences`, `formatPreferences`, `parsePreferences`, `loadPreferences`, `savePreferences`, `BusyFileRetry`,
  `isTransientlyBusy`, `retryWhileBusy`, `preferencesFileFor` (`preferences.hpp`, sans ImGui) ; `startPreferences`, `savePreferencesNow`, `preferencesOf`, `applyPreferences`,
  `dockNodesOfSavedLayout`, `resetLayout` (`layout.hpp`) : le fichier des préférences et son branchement sur ImGui.
- `QuitState`, `askToQuit`, `shouldOpenQuitModal`, `drawQuitModal` (`quit.hpp`) : la modale de fermeture.
- `Mode`, `ModeState`, `ModeRequests`, `modeRequested`, `enterMode`, `stopPressed`, `inputRouteFor`,
  `simulationPausedIn`, `programFrameRunsIn`, `otherMode`, `modeNameOf`, `modeHintOf`, `modeTokenOf`, `drawModeBar`,
  `playShortcutPressed`, `sceneHasFocus` (`mode.hpp`) : les modes, en fonctions libres. À la fin, `withEditor` écrit aussi `editor.mode mode=edit steps=0 -- mode : Édition ; pas : 0`,
  sous son nom stable (ADR-0036, décision 14), que la CI lit : l'éditeur lancé sans script est en Édition à zéro pas
  (`App::stepsPlayed`, tous les pas de la boucle).
- `Action`, `Context`, `Chord`, `Binding`, `ShortcutTable`, `defaultShortcuts`, `bindingOf`, `nameOf`, `actionFor`,
  `chordMatches`, `positionHeld`, `displayNameOf`, `tokenOf`, `chordFromToken`, `imguiChordOf`, `letterChordPressed`
  (`shortcuts.hpp`) : la
  table des raccourcis, en fonctions libres dont la disposition du clavier est un paramètre.
- `conflictsOf`, `replaceChord`, `resetAction`, `resetAll`, `conflictsIn`, `captureChord`, `linesOf`,
  `shortcutsFromLines` (`shortcut_edit.hpp`, sans ImGui) ; `ShortcutWindow`, `beginCapture`, `stepCapture`,
  `acceptPending`, `drawShortcutWindow`, `shortcutCaptureActive` (`shortcut_window.hpp`, `editor.hpp`) : changer la table.
- `drawMenuBar(inputs, windowLayout, layout, help)`, `drawHelp`, `helpRowsOf`, `helpRowMatches`, `equivalentsOf`
  (`menus.hpp`) ; `drawToolbar`, `drawStatusBar`, `statusOf` (`bars.hpp`) ; `availabilityOf`, `runAction`,
  `shortcutsPressed`, `keyPressesOf`, `toolActionOf`, `viewKeyActionOf`, `viewActiveIn`, `switchRealTime`, `ActionCounters`
  (`actions.hpp`) : les menus, les barres, la
  fenêtre Aide et ce que les raccourcis atteignent, en fonctions libres.
- `gridLinesFor`, `gridSpacingFor`, `drawGrid`, `GridState` (`grid.hpp`) ; `drawShowMenu`, `applyShowAction`, `helpersVisibleIn`,
  `gridVisibleIn`, `ShowState` (`show.hpp`) : la grille au sol, le menu « Afficher » et la vue de jeu.
- `drawView(view, app, mode)`, `View`, `sceneSizeOfContent`, `sceneFocusedIn` (`view.hpp`) ; `drawWindowMenuItems(entries,
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
- `stepGestures(state, events, context, table, seconds)`, `applyGesture(camera, frame, hauteur)`, `gestureStartedBy`,
  `flightMoveOf`, `capturesMouse`, `idleGestures`, `GestureState` (`gestures.hpp`) : les gestes de la caméra en fonctions
  libres, de l'état et des événements d'une image à la caméra suivante, sans ImGui ni flecs ; `entityBoxOf`,
  `sceneBoxOf`, `editorClipPlanes` (`scene_bounds.hpp`) : la boîte que F cadre, celle de la scène et les plans de découpe.
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
| **Unreal**, les raccourcis | Editor Preferences > Keyboard Shortcuts | Deux accords par commande, un conflit averti avec *Override* (**documenté**, *Customizing Keyboard Shortcuts*) : « Remplacer » en est la copie. Le détail de la saisie (Échap, F1) est notre choix. |
| **Unity**, les raccourcis | Le Shortcuts Manager | Une fenêtre qui liste, filtre et change les raccourcis, avec des profils (**documenté**, manuel, *Shortcuts Manager*) : pas de profil ici. |
| **Godot**, les raccourcis | Editor Settings > Shortcuts | Les raccourcis de l'éditeur se changent dans les réglages (**supposé**, non relu pour ce morceau). |
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
| **Unreal**, la grille et la vue de jeu | Le menu *Show* du viewport, la touche G | G passe le viewport en *Game View*, qui cache ce que le jeu ne montre pas (la grille, les icônes : **documenté**, GESTES.md [E1]) ; la grille se règle dans ses préférences (**supposé**). Levain suit : « Vue de jeu » cache la grille et le contour de la sélection, et Jouer les cache aussi (le viewport de PIE n'en a pas, **supposé**). |
| **Unity**, la grille | La grille de la Scene View et les *Gizmos* | Un bouton *Gizmos* montre ou cache les aides, la grille se règle dans la barre de la Scene View (**supposé**, non relu pour ce morceau) ; l'écart par puissances de 10 suivant le zoom est aussi le comportement que je lui connais (**supposé**). |
| **Godot**, la grille | View > Grid et View > Gizmos | Le menu *View* du viewport 3D active la grille et les gizmos (**supposé**, non relu pour ce morceau). |
| **Unreal**, les modes | Édition, puis *Play* (PIE) | Échap arrête le jeu, Alt+S simule (**documenté**, ADR-0036 [2]), Alt+P joue (**supposé**) ; PIE joue une copie du niveau, que notre « Jouer (sans retour) » ne fait pas avant M7.5 (**supposé**). |
| **Unity**, les modes | Le mode Play | Un mode Édition où rien ne tourne, Play à part, et les changements faits en jeu perdus à l'arrêt (**documenté**, GESTES.md [U20]) ; ici, rien n'est restauré avant M7.5. |
| **Godot**, les modes | Jouer la scène | Le jeu tourne dans une fenêtre à part, l'éditeur ne change pas de mode (**supposé**) : l'écart est assumé. |
