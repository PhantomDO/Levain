# `editor`

## Rôle

La bibliothèque de l'éditeur ([ADR-0018](../docs/adr/0018-moteur-plugins-et-jeu.md),
[ADR-0034](../docs/adr/0034-reflexion-des-composants.md), choix 6B) : ce que l'éditeur ajoute à un programme du
moteur, au-dessus d'`app` (SPECS §7). Le jeu en construit l'exécutable : son `main.cpp` compilé une seconde fois,
avec une définition qui enveloppe son démarrage par `withEditor`, et lié à `levain::editor`. Levain construit
ainsi `levain_sandbox_editor` (dans `sandbox/`) pour ses démos et sa CI ; *Rando* construira `rando_editor` à côté
de `rando`, dont la cible ne change pas.

**État en M7.2 (en cours)** : le branchement sur la boucle, l'option `--select`, le bilan que lit la CI, **la
hiérarchie** : les entités placées (`Transform`), rangées par `flecs::Parent` (ADR-0015), et un nœud
*Singletons*, qui sélectionne seulement ; et **l'inspecteur** : les composants de l'entité choisie, un widget par
champ, lus dans la description de flecs, aux valeurs de l'image. Une donnée d'auteur (`Authored`) s'édite, le reste est grisé.

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
   enum, nœud d'un agrégat, lignes de quatre
   nombres d'un tableau (une colonne de `glm::mat4` par ligne), et `vec2`/`vec3` d'un bloc. Un composant non
   décrit, une étiquette ou une paire : une ligne à leur nom, jamais leurs octets ; un type sans widget (texte,
   entité, opaque) : le nom du type. Le nom de l'entité (`Identifier`) est en tête de la fenêtre, pas
   un composant.
10. **Les widgets éditent une copie**, jamais la table. Si elle a changé, `commitEdit` l'écrit par
    `setComponentValue` : un seul `OnSet`, la seule écriture de l'inspecteur. Un composant sans `Authored` est
    grisé, et `commitEdit` le refuse encore si une copie a changé : deux gardes, la seconde testée seule.
11. **Les bornes sont imposées par l'inspecteur** (`ImGuiSliderFlags_AlwaysClamp`, sans lequel Ctrl+clic tape
    au-delà) : flecs ne borne rien. Une borne posée par `.range` s'applique à tous les nombres, entiers 64 bits
    compris.
12. **Les panneaux passent entre `defer_begin` et `defer_end`** (`withEditor`) : les observateurs d'une écriture
    passent après le parcours des composants de l'entité, qu'ils feraient changer de table. `drawInspector`
    refuse un monde non différé (assertion).

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
- **Un onglet caché ne dessine rien** : `Begin` rend faux pour une fenêtre derrière un autre onglet, et ses champs
  ne sont pas parcourus. À plusieurs sur un nœud, c'est ImGui qui met un onglet devant, sans règle qu'il documente :
  l'inspecteur a donc son nœud (`DockNodes::inspector`), toujours visible, sans quoi la CI pourrait compter zéro
  champ. La hiérarchie, onglet d'« Image », dresse sa liste avant `Begin` : le compte de ses lignes n'en dépend pas.
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
