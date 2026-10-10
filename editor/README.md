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

- **de mode Édition** : `withEditor` n'enveloppe que `ui` et `finish` (editor/src/editor.cpp:88-108), et la
  simulation avance à chaque image (engine/app/src/app.cpp:875-877). Le clavier va au jeu dès qu'aucun widget ne le
  prend (app.cpp:851-852) : les touches de vol, ZQSD sur un AZERTY, déplacent la caméra pendant qu'on édite, et
  Ctrl+Z y appuie aussi sur « avancer » ;
- **de Vue** : la scène couvre la fenêtre, vue par le centre du docking (engine/app/src/panels.cpp:148), mais sa
  projection est celle de la fenêtre entière (engine/render/src/renderer.cpp:139) ; la caméra est celle du jeu, une
  entité que déplace un système du pas fixe (sandbox/src/main.cpp:641-647 ; engine/scene/src/scene.cpp:111-117), et
  panneaux ouverts, la souris n'est jamais capturée (engine/app/include/levain/app/ui_layer.hpp:93-96) ;
- **d'écriture vue dans son image** : les panneaux passent après les pas et le choix de la caméra (app.cpp:875-891) ;
  une valeur tapée dans l'inspecteur se voit à l'image suivante ;
- **d'enregistrement** : rien n'écrit la scène, et fermer la fenêtre arrête la boucle sans rien demander
  (app.cpp:106-107) ;
- **d'annulation** : `commitEdit` écrit la copie par `setComponentValue` sans garder l'ancienne valeur
  (editor/src/inspector.cpp:543-552) ;
- ni menu, ni raccourci, ni console.

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
4. **La boucle ne change pas** : `withEditor` enveloppe la fonction de démarrage du programme et ses points
   d'accroche (ADR-0029).
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
    replié à ±180°, 0,1 à 1000) : un état lu d'un fichier avec un tangage de 120° regarde comme à 89°.

## Points d'entrée

- `withEditor(start, options)` : la fonction de démarrage du programme, enveloppée. Elle choisit l'entité de
  `--select`, en ouvre les ancêtres, ajoute la hiérarchie et l'inspecteur aux fenêtres, et écrit à la fin le
  bilan que lit la CI, « éditeur : N entités, M champs dessinés ; sélection : chemin », N étant les lignes de la
  hiérarchie et M les champs de la sélection à la dernière image.
- `takeEditorOptions(arguments, options)` : retire `--select chemin` de la ligne de commande avant que le programme
  ne lise ses options, qu'il refuserait sinon.
- `selectedIfAlive(world, selected)` : l'entité choisie si elle vit encore.
- `drawHierarchy(world, hierarchy, selected, dock)` : la fenêtre ; `listHierarchyRows` et `singletonsOf`, ses
  lignes, sans ImGui.
- `drawInspector(world, inspector, selected, dock)` : la fenêtre, dans son nœud ;
  `inspectComponent`, les champs d'un composant, rend leur nombre ; `componentLabelOf`, le nom d'un composant ;
  `commitEdit`, l'écriture d'une copie éditée ; `enumNameOf`, la constante qu'une valeur désigne.
- `createInspector(world, registry)` : les types des dessinateurs, lus une fois ; le registre d'assets de
  l'application nomme les assets, sans lui leur GUID. `eulerHint` et `rotationFromEuler` : les angles d'une
  rotation et la rotation de ces angles ; `entityFieldOf` et `entityLabelOf` : l'entité d'un champ, et son
  texte ; `assetNameOf` : le texte d'un asset.
- `flyCamera(camera, input, seconds)`, `orbitCamera(camera, pixels)`, `panCamera(camera, pixels, hauteur)`,
  `scaleFlySpeed(camera, crans)` : les gestes de la caméra, chacun rend la caméra suivante.
  `clipPlanesFor(camera, box)` rend les plans proche et lointain qui contiennent une boîte, et
  `toRenderCamera(camera, plans)` la `render::Camera` que prend
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
- **`panCamera` suit le curseur** : l'échelle est `2 · distance · tan(champ / 2) / hauteur`, donc le point au pivot reste
  sous la souris à toute distance, mais un point plus proche ou plus loin glisse (la parallaxe). Le haut du glissé est
  le haut de l'**écran** (incliné par le tangage), les touches Monter et Descendre de `flyCamera` la verticale du
  **monde**.
- **`toRenderCamera` vise loin** : `lookAtRH` retrouve le regard par `cible - position`, en `float`. Une cible à une
  unité d'un œil à 600 unités de l'origine hérite de l'arrondi de la position : au tangage de 89° (0,017 d'horizontale),
  la vue tourne autour de son axe d'un dixième de degré, et le bord de l'image tremble à chaque coup de souris. La
  cible est donc au moins aussi loin de l'œil que la position l'est de l'origine, et que le pivot.
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
