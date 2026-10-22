# ADR-0038 — L'annulation et les scènes : des commandes en valeurs, une suppression qui réserve son index, un fichier à nous

- **Statut** : accepté le 2026-10-22 par sondage : « L'annulation et les scènes (ADR-0038), pour M7.3 […] Je
  l'accepte ? » : « Accepter (Recommandé) » ; « Qu'est-ce qu'une scène, par rapport au code qui la construit
  aujourd'hui ? » : « Le fichier remplace le code (Recommandé) ». Amendé le même jour, avant le sondage, par la
  relecture (flecs 4.1.6 et ImGui 1.92.8 essayés en Debug et en Release : « accepter après corrections », la pierre
  tombale confirmée).
- **Date** : 2026-10-22
- **Milestone** : M7.3 (issues #254 et #255), deux branches : `m7.3/annulation`, puis `m7.3/scenes`
- **Modifie** : l'ADR-0034, sa passation « Pour M7.3 » (le JSON de flecs ne sert qu'aux valeurs) ; l'ADR-0037,
  « Ce qui reste » (M7.3 reprend le réveil, les plages et `differencesFrom`) et l'instantané, qui garde `Environment`
  (décision 19) ; l'ADR-0036, décision 12 (Annuler, Rétablir et Enregistrer cessent d'être grisés). L'étude du
  2026-10-10 l'appelait « ADR-0037 » : Play/Stop a pris ce numéro, les gizmos auront le 0039.

## Contexte

L'éditeur ne sait ni annuler ni enregistrer. Deux manques, deux branches : l'annulation (le premier geste du
quotidien), puis les scènes. L'étude du 10/10 les a décidées **avant M7.5**. M7.5 a changé les données du problème
(editor/src/play.cpp, engine/scene/include/levain/scene/snapshot.hpp) :

- le **réveil** d'une entité avec son identifiant et sa génération existe, testé (`ecs_make_alive`, flecs 4.1.6
  src/entity.c:3108-3150, gardé par `refuseTakenIndexes`) ;
- les **plages d'identifiants** (`IdRanges`) existent, mais protègent Play de l'Édition, **pas l'Édition d'elle-même** ;
- `EntityRecord` et `takeSnapshot` savent lire une entité (type, valeurs par `ecs_value_copy`, nom, parent), et
  `restoreSnapshot` sait la rendre ; mais `readRecord` et les étapes de la restauration sont locales à
  `snapshot.cpp:148-803`, pour toute la scène ;
- l'ordre des enfants se lit et se rend (`orderedChildrenOf`, `restoreChildOrder`, `ecs_set_child_order`) : ce que
  l'étude demandait d'essayer est **prouvé avec `flecs::Parent`** ;
- `differencesFrom` compare le monde à une copie, par identifiant ; `Transient` marque ce qui n'est pas la scène ;
  `holdAssetsInUse` retient les assets pendant le jeu ; les changements de structure sortent déjà du mode différé.

Trois choses du code d'aujourd'hui pèsent encore :

- l'inspecteur écrit **à chaque image d'un glissé** (`commitEdit`, editor/src/inspector.cpp:591-600, appelé par
  `inspectComponent`, :602-621), dans le mode différé (editor/src/editor.cpp:668-673) ;
- `Save`, `Undo` et `Redo` sont grisés « M7.3 » (editor/include/levain/editor/actions.hpp:39-52), avec leurs accords
  (editor/src/shortcuts.cpp:178-180) ;
- le sandbox construit toutes ses scènes en code, et garde des poignées : `FollowPlayer` (sandbox/src/main.cpp:670),
  `lookup("lac")` (:1176), la grille de 10 000 cubes (`spawnCubeGrid`, :127).

## Options envisagées

| Décision | Choix | Écarté |
|---|---|---|
| Ce que garde une commande | **Des valeurs** (`ValueBox`), copiées par les crochets du type | Du JSON (arrondi à 10 décimales : 100 annulations ne rendraient pas l'état d'après le 50e) ; un instantané du monde par geste |
| Supprimer | **Détruire pour de vrai, réserver l'index** par une pierre tombale (c'est le choix qu'Unity documente, [1]) | La suppression douce (`Disabled` + étiquette) ; une plage réservée ; réveiller en refusant si l'index est pris |
| Frères | **L'ordre de flecs** (M7.5), tel quel | Un composant d'ordre à nous |
| `Generated` / `Transient` | **Deux notions**, deux axes | Une seule |
| Scène | **Le fichier remplace le code** (sondage « Scène ») | Un fichier d'overrides au-dessus du code |
| Lire le JSON | **simdjson**, déjà compilé par fastgltf | Un lecteur à nous ; pas de contrôle des champs inconnus |

## Décision

### A. L'annulation (branche `m7.3/annulation`)

1. **Des commandes en valeurs, 100 niveaux.** Une commande garde l'entité (identifiant complet), le composant, la
   valeur d'avant et celle d'après, chacune une `ValueBox` (`ecs_value_copy`, qui copie aussi un type qui possède de la
   mémoire). Elle s'applique par `setComponentValue` (ADR-0034), les observateurs partent comme pour une écriture
   ordinaire. La pile garde 100 commandes (ROADMAP) ; une 101e fait sortir la plus ancienne ; une commande nouvelle
   vide les refaisables. **Une commande dont l'avant égale l'après (`sameValue`) est jetée** :
   `IsItemDeactivatedAfterEdit`
   rend des faux positifs (liste déroulante, sélectionnable). Les autres sortes de commandes (créer, supprimer,
   renommer, déplacer, ajouter ou retirer un composant) gardent un `EntityRecord`, ou deux.
   - *Écarté* : le JSON de flecs, qui arrondit (« 10 décimales fixes », #254) et demande un lecteur ; un instantané du
     monde (10 000 cubes) par geste.
2. **Un geste, une commande ; pas de clé de fusion.** Le geste s'ouvre à `IsItemActivated` (la valeur d'**avant** est
   celle du monde au début de l'image : l'inspecteur n'y a pas encore écrit), le monde est écrit **en direct** à chaque
   image du glissé, et la commande se pose à `IsItemDeactivatedAfterEdit`. Le geste ouvert est rangé sous l'identifiant
   actif d'ImGui ; un geste dont l'entité change ou disparaît est fermé à la fin de l'image, et **tout geste ouvert est
   fermé avant Play**.
   - **Une écriture sans geste ouvert est une commande d'une image** : l'avant est copié avant `commitEdit`. Les essais
     l'imposent : une liste déroulante (`BeginCombo` + `Selectable`, `comboEnum`) change la valeur sans jamais donner
     `IsItemActivated` ni `IsItemDeactivatedAfterEdit` ; et `flecs_defer_set` écrit **tout de suite** un composant que
     l'entité a déjà (seuls les observateurs attendent), donc la copie d'avant se prend avant l'écriture.
   - **Fermer avant d'ouvrir** : en passant de x à y d'un Tab, une même image porte la fin de x et le début de y ; deux
     commandes.
   - *Écarté* : la clé de fusion de l'étude (entité, composant, identifiant actif), le `MERGE_ENDS` de Godot : relâcher
     puis reprendre le champ garde le même identifiant actif et fusionnerait deux gestes (le glissé, la reprise, le
     Ctrl+clic qui écrit à chaque frappe, le double-clic et la case à cocher donnent chacun un geste, mesuré). C'est la
     règle d'Unity (« mouse down events split undo groups », documenté [1]) ; les gizmos de M7.4 ont leurs bords.
3. **Supprimer détruit pour de vrai, et réserve l'index.** C'est la décision qui change avec M7.5.
   - *Le problème* : `ecs_make_alive` échoue si l'index est vivant avec une autre génération. Après une destruction,
     flecs redonne l'index à la **prochaine** création, et pas seulement à celles de l'éditeur : une requête, un
     observateur, un nœud `Generated` suffisent. M7.5 l'a réglé pour Play par une plage à part ; **l'Édition n'a pas de
     deuxième plage**. L'ordre « dernier fait, premier défait » aide (une entité créée par une commande est détruite
     avant qu'on défasse la suppression), mais pas contre une entité de l'infrastructure, qui ne revient jamais.
   - *La parade* : à la suppression, l'éditeur réveille sur l'index libéré une **pierre tombale** (`Tombstone`, une
     étiquette) à la génération suivante : l'index est vivant, personne ne le reprend. Annuler détruit la pierre et
     réveille l'entité avec sa génération d'avant (le chemin de M7.5, qui rabaisse déjà une génération). Refaire détruit
     de nouveau et repose la pierre. Une **création** annulée suit le même chemin (refaire la réveille à son
     identifiant). **Avant chaque annulation ou reprise, `ecs_get_alive(index)` doit être la pierre de la commande**,
     sinon
     un refus nommé, **dans tous les builds** : en Release `ecs_make_alive` sur un index pris l'écrase sans rien dire
     (le
     nouveau meurt, `lookup` rend l'ancien identifiant mort). La commande possède ses pierres ; elles disparaissent
     quand elle sort de la pile (débordement, refaisables vidés, scène chargée). `isSceneEntity` les écarte : ni
     instantané, ni Hiérarchie, ni fichier.
   - *Le sous-arbre* : détruire un parent détruit ses enfants ; la commande garde un `EntityRecord` par descendant,
     **sans les nœuds `Generated`** (décision 10), et le réveil rend parents (par profondeur), types, valeurs, noms, et
     l'ordre des enfants du parent (décision 5). Il réutilise les étapes de `restoreSnapshot`, descendues de
     `snapshot.cpp` en fonctions publiques qui prennent une liste d'enregistrements.
   - *Les enfants `ChildOf`* (l'observateur d'un `onEnter`) partent avec l'entité et ne reviennent pas : le volume
     revient
     sans ses rappels, en silence (essai). **Comme l'ADR-0037, A8** : la suppression et l'annulation les nomment en
     avertissement dans la console (`internalChildren` de l'enregistrement). On ne refuse pas de supprimer un volume,
     geste courant.
   - *Ce qui ne revient pas* : une paire `(R, E)` d'une autre entité vers l'entité détruite (flecs la retire). La scène
     n'en porte pas ; les paires d'exécution sont `Transient` ou refaites (`InsideOf`). Un champ d'entité d'un composant
     d'auteur garde l'identifiant mort, et redevient valide au réveil.
   - **À prouver d'abord** (le morceau A1, comme celui de M7.5 : un test de flecs avant tout code, qui doit **d'abord
     montrer le problème**, et qui tourne **en Debug et en Release**) : a. détruire une entité puis créer mille entités,
     une requête et un observateur reprend
     l'index ; b. avec la pierre, aucun des mille ne le reprend ; c. détruire la pierre puis réveiller la génération
     d'avant donne une entité vivante, `lookup(chemin)` juste, ses références valides ; d. le sous-arbre, avec une
     création et une destruction intercalées ; e. une création annulée, puis refaite à son identifiant ; f. un nom
     « Cube 2 » (espace, chiffre) se retrouve par `lookup`.
   - *Écarté, la suppression douce du plan* (`Disabled` + une étiquette, entité cachée) : elle garde tout sans rien
     copier (paires et rappels compris), son seul avantage. Mais, essayé : `Disabled` cache l'entité des observateurs,
     **pas ses enfants `flecs::Parent`** (une étiquette de sous-arbre à tenir) ; les crochets (`on_remove` de `MeshRef`)
     tournent encore ; le nom reste pris ; la Hiérarchie, la sauvegarde, l'instantané, le picking et chaque `world.each`
     apprendraient à sauter la supprimée ; une poignée du code (`FollowPlayer`) suivrait un fantôme vivant. *Écarté, une
     plage
     réservée* : l'index d'une entité qui existait déjà (un cube chargé) est dans la plage de l'édition, et le
     protéger demande de changer de plage à chaque suppression. *Écarté, réveiller en refusant si l'index est pris* :
     l'annulation serait impossible de temps en temps, sans cause visible, la leçon même de l'ADR-0037 (décision 2).
   - `Disabled` reste la case « Activer » de l'inspecteur (geste 30), qui n'a plus rien à voir avec la suppression : son
     test (requêtes, systèmes, observateurs, physique) passe en tête de son morceau.
4. **Les commandes retiennent leurs assets.** Sans cela, supprimer la seule instance d'un modèle le fait décharger en
   fin d'image (engine/app/src/app.cpp:1174, `takeUnusedAssets`) et l'entité réveillée est invisible. `AssetUsage` gagne
   `pinned` (un compte par asset) : une commande épingle les `AssetRef` de ce qu'elle peut rendre (`MeshRef`,
   `ModelInstance`, `MeshCollider`, avant et après) et les rend en sortant de la pile ; `takeUnusedAssets` ne décharge
   pas un asset épinglé ; **rendre une épingle remet l'asset dans la liste à décharger** s'il tombe à zéro (test).
   *Écarté* : recharger le modèle à l'annulation (il faut `App&`, le GPU, et l'animateur rangé par
   asset) ; `holdAssetsInUse` (le drapeau de Play : tout retenu, et refusé deux fois), trop gros pour une session
   d'édition entière.
5. **L'ordre des frères est celui de flecs**, comme M7.5. `orderedChildrenOf` et `ecs_set_child_order` deviennent
   publics. Une commande qui change l'ordre garde la liste complète du parent, avant et après ; supprimer garde la
   place de l'entité par cette liste. Les **racines** n'ont pas de parent : la Hiérarchie les range par index (ADR-0037,
   décision 6), donc par ordre d'**index** (qui n'est l'ordre de création que tant que les index ne sont pas recyclés) ;
   une racine
   réveillée reprend sa place. **On ne réordonne pas les racines en
   M7.3** (supposé : l'Outliner d'Unreal n'offre pas non plus d'ordre manuel). *Écarté* : un composant d'ordre à nous,
   une seconde vérité à tenir d'accord avec la liste que Stop rend déjà.
6. **Les noms** sont uniques entre frères (racines comprises) et sans « . » ni « :: » (un « . » éclate au rechargement,
   #254) ni vides, ni « # » suivi de chiffres (`lookup` rend l'entité n° 12 de flecs), ni « _ », « * », « $ » seuls à la
   racine
   (`EcsAny`, `Wildcard`, variable : essayé) : `isValidEntityName`, `uniqueNameAmong` (« Entité », « Entité 2 »…), dans
   `scene`, car le chargeur
   et `takeSnapshot` (qui refuse deux frères du même nom) s'en servent. Renommer refuse sans rien changer, et le champ
   le dit.
7. **Ajouter un composant s'annule en entier.** `Velocity` amène `PreviousTransform` (engine/scene/src/scene.cpp:159),
   `Transform` amène `WorldTransform` (:163). La commande garde le **type complet** (celui de `sceneTypeOf`) avant et
   après ; annuler retire tout ce que l'ajout a amené, refaire ajoute de nouveau et les traits rejouent. Retirer, et
   « Réinitialiser » (la valeur par défaut), sont des commandes de la même forme.
8. **La sélection ne s'annule pas.** L'entité réveillée par une annulation redevient la sélection ; celle qu'une
   suppression refaite détruit se désélectionne (`selectedIfAlive`).
9. **Le moment, et le mode.**
   - *Hors du mode différé* : annuler, refaire, créer, supprimer, coller, charger sont mis en file pendant le dessin
     (la Hiérarchie et l'inspecteur dessinent entre `defer_begin` et `defer_end`) et exécutés en un point nommé,
     `applyEditorRequests`, juste après `defer_end` ; les raccourcis et les menus, déjà hors du `defer`, l'appellent
     directement. La file que l'ADR-0037 jugeait inutile pour Stop devient nécessaire ici.
   - *Annuler n'est actif qu'en Édition.* En Jouer et en Simuler, Ctrl+Z, Ctrl+Y et le menu sont grisés, la raison dite.
     La pile est **gelée, pas vidée** : Stop rend la scène identique (identifiants compris), donc les commandes d'avant
     Play restent valables. Ce qu'on change en jeu, valeur ou structure, **n'est pas enregistré** : c'est perdu à Stop
     (ADR-0037, décision 6), et il n'y a donc rien à jeter. Les opérations sont les mêmes fonctions libres dans tous les
     modes ; seul l'historique décide de garder la commande. **Amendement du 2026-10-22** : « les mêmes
     fonctions » ne vaut pas pour supprimer. `removeKeepingIndex` refuse en jeu toute entité de l'édition (son index n'est
     pas dans la plage du jeu : Stop s'arrêterait ou donnerait l'identifiant de la pierre à une autre entité), donc en
     Jouer et en Simuler **supprimer est un `ecs_delete` simple, jamais une commande** ; Stop réveille l'entité avec son
     identifiant (`snapshot_test`, « l'index d'une caisse détruite en jeu revient »). `removeKeepingIndex` le refuse
     donc en jeu, même pour une entité née en jeu : sa pierre survivrait à Stop. (Unreal : les transactions de PIE sont
     séparées, supposé ; Unity : que l'historique serve aussi en jeu est supposé ; que ses changements soient perdus à
     la sortie est documenté [3].)
   - *Ctrl+Z pendant qu'un élément est actif* (un glissé en cours) est refusé : ImGui le laisse passer
     (`WantTextInput` à 0, `Shortcut(RouteGlobal)` vrai, essayé).
   - *« Modifié »* se lit dans la pile : chaque commande a un numéro, la sauvegarde note celui du sommet ; modifié si le
     sommet n'est plus ce numéro. Si le point de sauvegarde est sorti de la pile (débordement, refaisables vidés), la
     scène est modifiée sans retour possible. Le « * » va dans la barre de menus (le titre de la fenêtre reste ASCII,
     ADR-0036). La leçon de Godot : un undo qui ne marque pas la scène laisse fermer sans prévenir [6].
   - *La pile est vidée au chargement d'une scène et à « Nouveau »* : pierres détruites, assets rendus.
10. **`Generated` et `Transient` sont deux notions.** `Transient` (M7.5) est posé sur un **composant** : sa valeur
    d'exécution n'est pas la scène (`BodyHandle`), jamais copiée, retirée à Stop. `Generated` est posé sur une
    **entité** : elle naît d'un composant de sa racine (`CubeGrid`, `ModelInstance`) et se recrée depuis lui ; elle ne
    s'enregistre pas, ne s'inclut pas dans le sous-arbre d'une commande, se montre en lecture seule sous « généré, non
    enregistré », et un clic dans la Vue choisit d'abord l'ancêtre non généré (ADR-0039). Ni l'une ni l'autre ne change
    l'instantané : il copie tout, `Generated` compris, pour que Stop rende les mêmes identifiants. *Écarté, une seule
    notion* : « ne pas copier » et « ne pas enregistrer » ne coïncident pas (un cube généré doit revenir à Stop avec son
    identifiant ; un handle de Jolt ne le doit pas).
    **Les générateurs** (`ModelInstance`, `CubeGrid`) sont des observateurs, **coupés pendant une restauration** : Stop
    et
    les réveils des commandes les désactivent (`ecs_enable`, comme `physics::endPlay` coupe les siens), puisque les
    nœuds `Generated` reviennent par leurs enregistrements (Stop) ou se régénèrent d'un coup à la fin (annulation d'une
    suppression). Tout autre changement de la racine régénère : la commande qui change le modèle détruit d'abord les
    anciens nœuds. **Filet** : un générateur qui trouve déjà un enfant `Generated` ne fait rien. Il est sûr pour Stop
    parce que `Generated` est une étiquette, rendue par `restoreTypes`, et que les nœuds sont réveillés et rattachés
    avant que `restoreValues` ne réécrive `ModelInstance` ; hors de cela c'est un silence (le code qui pose un autre
    modèle garde les anciens nœuds), d'où la coupure, et un contre-test (le filet retiré : Stop double les nœuds). **Une
    entité faite à la main sous un nœud `Generated`** (une épée sur un os) est refusée, nommée : annuler la réveillerait
    sous un parent mort, et le fichier ne saurait pas nommer ce parent. Supprimer, renommer (F2) et glisser un nœud
    `Generated` sont refusés de même.

11. **Les gestes de la branche** (`editor/GESTES.md`, 14 lignes) : annuler, refaire, un glissé une étape, l'historique
    (fenêtre qui liste la pile ; un clic ramène à ce point, les refaisables marqués ; Unity [3]), créer, supprimer,
    renommer (F2), dupliquer, copier-coller (des `EntityRecord` en mémoire), reparenter (position monde gardée, un
    corps refusé : body_rules.hpp), ajouter, retirer et réinitialiser un composant, activer, chercher. Chaque ligne
    passe à « couvert » avec le nom de son test.

### B. Les scènes (branche `m7.3/scenes`)

12. **Ce que M7.3 reprend de M7.5** : le réveil (pour créer et défaire), `EntityRecord`, `differencesFrom`, `Transient`,
    l'ordre des enfants, `applyEditorRequests`. Ce qu'il ne peut **pas** reprendre tel quel : `compareWithSnapshot`. Une
    scène chargée a des identifiants neufs et des flottants relus à 10 décimales. La comparaison est généralisée :
    `differencesFrom` prend un mode (`ById` pour Stop, `ByPath` pour un fichier, nœuds `Generated` sautés, leur racine
    comparée) et une tolérance. **`ByPath` est une fonction à part** (deux mondes : composants par symbole, références
    par
    chemin) ; le code d'`ById` n'est pas touché, et un test garde son exactitude (« Stop, valeur changée de 1e-12 :
    une différence »).
13. **Ce qu'est une scène** : « Le fichier remplace le code », la réponse au sondage « Scène » du 2026-10-22.
    - Les vues `demo`, `physics`, `terrain` et `hike` du sandbox deviennent des fichiers `data/scenes/*.scene.json`. Au
      démarrage, le code crée les systèmes et les passes, puis charge le fichier. `--model` et la vue `khronos` restent
      du code : la CI y charge des glTF quelconques (`runSandbox`, `runCooked`, tools/ci-programs.sh).
    - **Le code retrouve ses entités par composant, jamais par nom ni par poignée** : un composant `Player` pour
      `FollowPlayer`, un autre pour le lac, relus à chaque pas ou dans un crochet `sceneLoaded`. Renommer une entité que
      le code cherche doit échouer bruyamment (règle n°7).
    - Ce que le code génère en masse porte `Generated` (décision 10) : la grille (`CubeGrid`), les nœuds anonymes d'un
      glTF
      (engine/assets/src/gltf.cpp:391 et 406).
    - *Écarté* : un fichier d'overrides par chemin au-dessus du code. Pas de refonte des démarrages, mais un fichier ne
      sait ni supprimer ni réordonner, et « la vallée faite entièrement dans l'éditeur » (critère de M7.6) ne serait
      vraie
      qu'à moitié.
14. **L'enveloppe** : `{format, version, environment, entities:[{name, parent, components:{symbole: valeur}}]}`. Les
    entités sont en pré-ordre (un parent avant ses enfants, les enfants dans l'ordre de flecs, les racines par index) ;
    `parent` est l'indice dans le tableau, jamais un chemin (un « . » éclaterait). Les clés sont les symboles
    (`stableKeyOf`, `componentOfKey`, sans repli sur le chemin). Les références d'entité s'écrivent par chemin
    (ADR-0034) ; une référence vers une entité sans nom est **refusée à l'enregistrement**, en nommant l'entité et le
    champ. Une entité sans nom (hors `Generated`) l'est aussi.
15. **Quoi enregistrer** : une entité de la scène, non `Generated`. Un de ses composants est **enregistré** s'il est
    `Authored`, **sauté** s'il est dérivé (décrit en lecture seule : `WorldTransform`, `PreviousTransform`), `Transient`
    ou marqué **`Derived`**, et **refusé en le nommant** sinon. `Derived` est un marqueur de composant comme
    `Transient`,
    mais **copié par l'instantané** (Stop doit le rendre) et jamais enregistré : c'est le `Collider`, que la physique
    reconstruit des formes d'auteur ; sans lui, toute entité à forme serait refusée. Les **étiquettes** s'écrivent comme
    des clés à `true` (`"Cube": true`, `Cube` étant décrit) et l'état **activé** (`Disabled`, la case de l'inspecteur)
    comme `"enabled": false` sur l'entité, absent s'il est vrai.
16. **Les valeurs** passent par `ecs_ptr_to_json` et `ecs_ptr_from_json` puis `setComponentValue`, jamais par
    `world.from_json` (ADR-0034). Chaque résultat est vérifié (une enum invalide vide tout le `to_json`, #254). Un
    nombre non fini est refusé à l'enregistrement, avec l'entité et le champ. Clé, champ, type ou version inconnus :
    refus ; champ manquant : valeur par défaut et avertissement. Le chargement se fait en **deux passes**, hors du mode
    différé : les entités avec leur nom et leur parent, puis les valeurs (les références se résolvent par `lookup`).
    - **Ce que flecs 4.1.6 fait vraiment des nombres** (400 000 valeurs essayées ; la preuve « tout est idempotent après
      une sauvegarde » de la première version de cet ADR était fausse) : un flottant dans ]−5·10⁻¹¹, 0[ s'écrit « -0 »
      puis « 0 » (la deuxième sauvegarde diffère) ; `-0.0f` s'écrit « 0 » et `sameValue` (par `memcmp`) y voit un
      changement ; une valeur relue peut s'écarter de 5,82·10⁻¹¹ ; un double perd un chiffre à chaque sauvegarde (le
      tampon de chiffres de flecs, src/datastructures/strbuf.c:87-160) ; et la lecture passe par `atof`
      (src/addons/json/deserialize_value.c:145), qui lit « 1.5 » comme 1 sous `fr_FR`, en silence.
      **Ce que fait Levain** : « -0 » s'écrit « 0 » (nous passons les valeurs en revue avant d'écrire) ; la tolérance
      est
      de **5·10⁻¹¹ plus 1 ulp**, pour les flottants ; **le chargement refuse** si la locale n'a pas « . » pour virgule
      (erreur nommée, jamais une lecture fausse) ; un **double** n'est pas garanti stable : un composant d'auteur en
      `double` est écrit avec ses chiffres significatifs par nous (pas par flecs), ou refusé tant que ce n'est pas fait,
      et le test de l'inventaire le dit ; la deuxième sauvegarde égale la première pour tout ce qui est flottant, mesuré
      sur chaque composant décrit (morceau S8). Un nombre peut s'écrire « 1e4 » et une valeur être une chaîne : on
      compare
      le pointeur de fin à la longueur de `raw_json`, et un champ absent est un avertissement, jamais un défaut muet.
17. **Le fichier** : mode binaire, fins de ligne LF (`.gitattributes` : `*.scene.json eol=lf`), écriture atomique (un
    fichier temporaire **à côté de la cible**, puis `std::filesystem::rename`), indépendante de la locale, noms échappés
    en JSON (guillemets, barre, contrôles ; l'UTF-8 passe tel quel). Un chemin non ASCII s'ouvre, sous Windows aussi.
    Sous la STL de Microsoft, `rename` est `MoveFileExW` avec
    `COPY_ALLOWED`: d'un volume à l'autre ce n'est plus atomique, et une cible verrouillée échoue ; d'où le temporaire
    voisin, une **erreur nommée** à l'échec, l'ancien fichier gardé, et des tests dans la suite Windows. `fr_FR` est
    **généré sur la CI** (un changement de `ci.yml`), le test ne se saute jamais faute de locale (règle n°7).
18. **simdjson** lit l'enveloppe : fastgltf 0.9.0 le compile déjà partout (simdjson 4.6.4, Apache-2.0 ou MIT), et son
    `raw_json()` donne le texte d'une valeur à `ecs_ptr_from_json` sans le réécrire. Il entre dans `vcpkg.json` (il n'y
    est pas aujourd'hui), visible seulement de `levain_scene` (test `deps.simdjson-visibility`, sur le modèle de
    `deps.imgui-visibility`, tests/CMakeLists.txt:655). Aucune CI à la main pour lui : fastgltf le tire déjà avec les
    mêmes options.
    *Écarté* : un lecteur à nous (des bogues d'échappement à écrire), ou ne pas détecter les champs inconnus (un champ
    renommé casserait une scène en silence, règle n°7).
19. **Le contenu d'une scène** :
    - *les formes physiques* sont des composants d'auteur, un par forme comme Unity et Unreal (`BoxCollider`,
      `SphereCollider`, `CapsuleCollider`, `MeshCollider{AssetRef}`) ; deux formes sur une entité : refus nommé.
      `Collider` devient une donnée dérivée (`Derived`, décision 15). **Il reste copié par l'instantané** (une valeur
      non
      comparable, réécrite à Stop), et **ne devient pas `Transient`** : Stop ne réécrit une forme que si elle a changé,
      et
      l'observateur n'aurait alors rien à reconstruire. Le test de Stop sur une scène à formes d'auteur le garde ;
    - `ModelInstance{AssetRef}` sur la racine d'un modèle ; un modèle skinné instancié deux fois est refusé (son
      animateur
      est rangé par asset, ADR-0029) ; `loadModel` instancie sous une racine existante (aujourd'hui il la crée et refuse
      un nom pris : engine/app/src/load_model.cpp:124) ;
    - `Environment`, **singleton décrit** (soleil, ciel, fond), lu par `app` à chaque image à la place de ses champs
      (`sun`, `background`, engine/app/include/levain/app/app.hpp:339-343), enregistré sous `environment`, édité dans un
      panneau « Monde ». Un singleton n'est pas la scène, et l'instantané ne le garde pas : **il le garde pour
      `Environment`**
      (`Snapshot` gagne sa valeur, `restoreSnapshot` la rend, `differencesFrom` la compare), sans quoi un jeu qui
      déplace le soleil fuirait dans l'enregistrement. Ses changements sont des commandes ;
    - le sélecteur d'asset (une liste du registre filtrée par type, pour tout champ `AssetRef`) et « Entité > Ajouter un
      modèle… » ; ses choix sont des commandes, avec leurs épingles (décision 4).
20. **Le menu Fichier** : Nouveau, Ouvrir, Enregistrer (Ctrl+S, écrit aussi les assets modifiés qu'elle désigne),
    Enregistrer sous, Récents ; un navigateur de fichiers dans ImGui, testable sans écran ; la modale « Quitter ? »
    gagne « Enregistrer » (editor/include/levain/editor/quit.hpp:4) ; une sauvegarde de secours toutes les 10 minutes,
    scène modifiée seulement, dans le dossier de l'utilisateur, **jamais** sous une exécution scriptée. **Enregistrer,
    Ouvrir et Nouveau sont refusés en jeu** (on fait Stop d'abord ; quitter en jeu fait déjà Stop, ADR-0037).

## Conséquences

- `scene` gagne `Tombstone`, `Generated`, la lecture et le réveil d'un sous-arbre (descendus de `snapshot.cpp`),
  `isValidEntityName`, `uniqueNameAmong`, `saveScene` et `loadScene`. `assets` gagne `pinned`. `editor` gagne la pile,
  ses
  commandes, `applyEditorRequests` et les menus. `physics` change ses composants (formes d'auteur). `app` gagne
  `ModelInstance`, `Environment` et le crochet `sceneLoaded`. Le sandbox perd ses scènes en code.
- Une pierre tombale par entité détruite non générée, tant que la commande est dans la pile : au plus 100 commandes. Le
  coût d'une suppression est celui de ses enregistrements ; le sous-arbre des 10 000 cubes ne coûte rien (générés).
  Le coût de la suppression d'une grosse scène se mesure (règle n°6) sur la machine de référence, il n'est pas promis
  ici.
- **Ne revient pas à l'annulation** : une paire vers une entité détruite, les rappels d'un volume supprimé (avertis), la
  sélection.
- **Hors du périmètre** (étude du 10/10, §11) : prefabs et overrides, édition de plusieurs objets, navigateur d'assets
  complet, lumières ponctuelles en entités, réordonner les racines, « garder les changements après Stop ». *Rando* :
  `ThirdPersonCamera::target` s'écrit comme un nombre et le composant porte des `std::optional` ; à sa montée de
  version (M8.2).
- Le sondage « Scène » décide la décision 13 ; si Donnovan choisit l'autre option, les décisions 14 à 20 restent, le
  morceau S9 (la migration du sandbox) devient un fichier d'overrides, et l'ADR est amendé.

## Les contrôles

Chaque contrôle a son contre-test : la faute injectée, le test rouge, puis vert (règle n°7).

| Ce qui est promis | Test | Contre-test |
|---|---|---|
| **100 niveaux, par les valeurs** | 150 changements, 100 annulations : l'état est celui d'après le 50e ; la 101e est sans effet ; refaire rend l'état d'après le 150e | Une pile de 99 : rouge |
| Un geste, une commande | Le banc : 30 images de glisser donnent 1 commande ; relâcher puis reprendre le champ, 2 ; une liste déroulante sans changement, 0 | Un `commitEdit` à chaque image (aujourd'hui) : 30, rouge |
| **Une écriture sans geste** | Un choix dans une liste déroulante (`comboEnum`) : 1 commande ; Tab de x à y : 2 (fermer avant d'ouvrir) ; Échap : 0 | Sans l'avant copié avant `commitEdit` : l'avant égale l'après, 0 commande, rouge |
| Ctrl+Z pendant un glissé | Refusé tant qu'un élément est actif ; un geste ouvert est fermé avant Play | Sans le refus : la pile change sous le glissé, rouge |
| Une commande égale à elle-même est jetée | Retaper la valeur d'un champ : la pile ne bouge pas | Sans `sameValue` : une commande, rouge |
| **Supprimer puis annuler rend la même entité** | Identifiant et génération égaux, noms, parents, ordre, valeurs ; entre les deux, 1 000 créations, une requête et un observateur | Sans pierre tombale : l'index est repris, rouge |
| Un index pris est un refus | Une pierre détruite à la main avant l'annulation : refus nommé, **en Release aussi**, le nouveau vivant intact ; les essais a à f de la décision 3 en Debug et en Release | La garde retirée : en Release, le nouveau meurt, rouge |
| Un volume supprimé | Le volume sans son `onEnter` revient, la console nomme l'observateur perdu | Sans l'avertissement : rouge |
| Les noms piégés | « #12 », « _ », « * », « $ » refusés ; après un F2, `lookup` de Stop juste | Sans le refus : le contrôle de Stop échoue |
| Le modèle se dessine | Supprimer la seule instance d'un modèle, une image (`takeUnusedAssets`), annuler : dessiné, sans avertissement | Sans épingle : rouge |
| Annuler un ajout rend le type d'avant | `ecs_get_type` égal avant et après l'ajout de `Velocity` (qui amène `PreviousTransform`), puis refait | Sans retirer ce que le trait amène : rouge |
| Création annulée, refaite | La création refaite a l'identifiant d'origine | Créer neuf au lieu de réveiller : rouge |
| Les frères | Réordonner, supprimer et annuler, reparenter : la liste des enfants égale | Sans la liste gardée : l'entité revient en fin |
| Les noms | « a.b » et « a::b » refusés, deux frères identiques refusés, `uniqueNameAmong` | Sans le contrôle : rouge |
| « Modifié » suit la pile | Modifier, annuler : propre ; enregistrer, annuler, refaire ; le point sorti de la pile : modifié | Un « modifié » mis à la main : rouge |
| En jeu | Ctrl+Z grisé ; la pile intacte après Stop ; Stop à `differences=0` avec des pierres en place ; une suppression en jeu n'entre pas dans la pile | Une pierre comptée comme entité de la scène : Stop rouge |
| Chargement | La pile vidée, les pierres détruites, les épingles rendues | Une pile gardée : rouge |
| **La ligne de la CI** | `editor.undo changes=150 undone=100 differences=0 -- …`, nombres exacts exigés (Linux par ctest, Windows par `ci-programs.sh editor-undo`) | Un annuler cassé : la CI échoue |
| **Aller-retour d'une scène** | Un monde qui porte chaque composant décrit (dont un de module, clé ≠ chemin), `Parent`, références, enums, `Environment` ; `takeSnapshot`, enregistrer, charger dans un monde neuf, `differencesFrom` `ByPath` à 5·10⁻¹¹ près ; sous `fr_FR` | Un écrivain qui saute un composant : rouge |
| La deuxième sauvegarde est la première | Égalité au caractère | Un ordre d'entités instable : rouge |
| Les nombres de flecs | Les cas de la décision 16 sur chaque composant décrit : −0 écrit « 0 », tolérance 5·10⁻¹¹ + 1 ulp, « 1e4 » et une chaîne refusés, un champ absent averti ; le chargement **refuse** sous une locale à virgule (générée sur la CI, jamais sautée) | `atof` sous `fr_FR` sans le refus : « 1.5 » lu 1, rouge |
| Stop reste exact | `ById` à tolérance nulle : une valeur changée de 1e-12 en jeu est **une** différence | Une tolérance dans `ById` : rouge |
| `Environment` à Stop | Le soleil déplacé en jeu revient à Stop ; non enregistré | Sans l'instantané : rouge |
| `Collider` dérivé | Une entité à forme s'enregistre ; le `Collider` n'est pas dans le fichier ; Stop à zéro différence | Sans `Derived` : refus de toute forme |
| Les nœuds générés | Supprimer, renommer, glisser un nœud `Generated`, ou en faire le parent d'une entité à la main : refus nommé | Sans le refus : l'annulation réveille sous un parent mort, rouge |
| L'épingle rendue | Rendre une épingle remet l'asset à décharger s'il est à zéro | Sans la remise en file : l'asset reste chargé, rouge |
| Écrire sous Windows | Le temporaire voisin ; une cible verrouillée : erreur nommée, ancien fichier gardé (suite Windows) | Le temporaire dans un autre dossier : rouge |
| Chaque refus | Un test par refus : enum invalide, référence anonyme, non fini, clé, champ, type, version, composant non décrit, deux formes, entité sans nom | Un refus retiré : rouge |
| Écriture atomique | Un échec injecté avant le renommage laisse l'ancien fichier intact | Écrire sur place : rouge |
| simdjson reste dans `scene` | `deps.simdjson-visibility` | Un `#include` ailleurs : rouge |
| Stop avec `ModelInstance` | Une racine détruite en jeu, Stop : `differences=0`, aucun nœud en double | Le filet retiré : les nœuds doublent, rouge ; un générateur non coupé pendant la restauration : rouge |
| Les vues sont des fichiers | Pour demo, physics, terrain, hike : « scène : N entités, identique » ; chaque contrôle chiffré de `ci-programs.sh` inchangé (le renard marche vers le lac aux mêmes coordonnées) | Un fichier tronqué : la CI échoue |
| Le code ne garde pas de poignée | Charger B après A, puis jouer, sous ASan ; renommer l'entité cherchée échoue bruyamment | Une poignée gardée : ASan rouge |
| Le web | Le paquet contient les scènes, sinon la page échoue bruyamment | Un fichier absent du paquet : échec |
| Le menu Fichier | Le banc : « modifié » suit la pile ; quitter ouvre la modale ; un nom non ASCII s'ouvre (portable Windows aussi) ; Enregistrer refusé en jeu | Quitter sans demander : rouge |
| Le soleil | Changé à l'inspecteur, l'éclairage change ; l'annulation le rend | Un soleil lu des champs d'`App` : rouge |
| Le coût de l'annulation | `tools/undo-cost.sh` (règle n°6) : suppression et annulation d'un sous-arbre de 1 000 entités, les chiffres au journal seulement mesurés sur la machine de référence | — |

Les 14 gestes de l'annulation et les 4 des scènes d'`editor/GESTES.md` passent à « couvert » avec leur test. Chaque
clôture écrit « N gestes sur M couverts » (`awk` du fichier).

## Les morceaux

Chacun d'au plus 400 lignes par PR (règle n°2), relu, vérifié par `tools/verify.sh`, fusionné dans sa branche
(règle n°1). **Comment ce plan tient compte de M7.5** : ses morceaux ont coûté 3,2 fois leur estimation (2 780 pour
8 801), ceux de l'éditeur de 4 à 8 fois, et 57 % des lignes étaient des tests, bancs en tête (ADR-0037, « Les
morceaux »). Ici les **bancs de bout en bout ont leur colonne** (l'écart de M7.5), les estimations partent de ce qu'a
coûté un comportement d'éditeur avec son banc (environ 800 lignes), et la coupe en PR est faite d'avance, un
comportement et son banc par PR. Total estimé : **13 300 lignes** (8 700 de code et de tests, 4 100 de bancs, 530 de
docs), soit 38 PR ; **plus que M7.5 livré** (8 801). Si, après A6, le livré dépasse 1,5 fois l'estimé, on réestime et le
journal le dit.

| n° | Contenu | Code et tests | Banc | Tests et contre-tests |
|---|---|---:|---:|---|
| A0 | Cet ADR, les sondages, le temps de M7.5 | 380 (docs) | — | — |
| A1 | scene : `Tombstone`, retirer et réveiller, la garde de l'index ; **les essais de flecs d'abord**, Debug et Release (décision 3) ; `isValidEntityName`, `uniqueNameAmong` | 450 | — | a à f ; le refus d'un index pris |
| A2 | scene : lire et réveiller un sous-arbre (descendu de `snapshot.cpp`), `orderedChildrenOf` public, `Generated`, `Derived`, `isSceneEntity` écarte les pierres | 450 | — | Un sous-arbre revient égal ; les tests de M7.5 verts |
| A3 | assets : `pinned`, la remise en file | 200 | — | Un asset épinglé ne se décharge pas |
| A4 | editor : la pile, les commandes de valeur, `applyEditorRequests`, Annuler et Refaire | 450 | 250 | 150 / 100 / 101 ; « modifié » ; la pile gelée en jeu |
| A5 | L'inspecteur : les bords du geste, l'écriture sans geste, Ctrl+Z refusé en glissé ; la ligne `editor.undo` et `tools/undo-cost.sh` | 400 | 350 | 30 images, 1 commande ; combo, Tab, Échap |
| A6 | La Hiérarchie : Créer, Supprimer (volumes avertis), Renommer, menu contextuel | 500 | 450 | Supprimer, annuler : même identifiant, modèle dessiné |
| A7 | Dupliquer, copier-coller, reparenter, réordonner | 500 | 450 | Chaque opération et son annulation |
| A8 | Ajouter (avec recherche), retirer, réinitialiser, copier-coller les valeurs | 450 | 400 | `Velocity` puis annuler : le type d'avant |
| A9 | Activer (le test de `Disabled` d'abord ; la requête des racines saute `Disabled` : la racine désactivée reste visible), cacher dans la Vue, recherche, fenêtre Historique | 450 | 400 | Un clic de l'historique revient à ce point |
| A fin | Journal, gestes, READMEs ; PR vers `main`, sa CI | 150 (docs) | — | — |
| S1 | physics : formes d'auteur, `Collider` dérivé, deux formes refusées | 450 | — | Stop à zéro différence |
| S2 | physics et sandbox : `MeshCollider{AssetRef}`, décor et caisses migrés | 400 | 100 | La vue physics garde ses chutes |
| S3 | app : `ModelInstance`, générateurs coupés en restauration, nœuds `Generated` ; **le test de Stop d'abord** | 500 | 250 | Stop avec racine détruite |
| S4 | editor : sélecteur d'asset, « Ajouter un modèle… », refus sur les nœuds générés | 300 | 250 | Choisir crée l'instance, l'annulation la retire |
| S5 | app et editor : `Environment` (et son instantané), le panneau « Monde » | 300 | 200 | Le soleil déplacé en jeu revient |
| S6 | scene : `saveScene` ; la locale `fr_FR` générée sur la CI (`ci.yml`) ; **son passage sous Windows** | 500 | — | Chaque refus ; −0 ; l'écriture interrompue |
| S7 | scene : simdjson (déclaré, son test de visibilité), `loadScene` en deux passes | 550 | — | Chaque refus de lecture ; la virgule de locale |
| S8 | **Le critère** : `ByPath` (fonction à part), l'aller-retour, la deuxième sauvegarde | 500 | — | Le contre-test de l'écrivain ; `ById` exact |
| S9 | sandbox : demo et physics en fichiers, `CubeGrid`, le code par composant, `sceneLoaded` | 500 | 300 | « scène : N entités, identique » ; B après A sous ASan |
| S10 | sandbox : terrain et hike, le lac par composant, le paquet web | 400 | 250 | Les mêmes lignes de CI |
| S11 | Le menu Fichier, le navigateur, quitter, la sauvegarde de secours | 450 | 450 | Le banc ; nom non ASCII (portable) |
| S fin | Journal, clôture, gestes, READMEs ; PR vers `main`, sa CI | 150 (docs) | — | — |

Les morceaux de plus de 400 lignes se coupent en deux ou trois PR à l'écriture. CI à la main (règle n°1) : S1 (change
les
composants de la physique) et S6 (le changement de `ci.yml` pour `fr_FR`), pas pour simdjson (déjà construit).

## Ce que font les autres moteurs

- **Unity** : `Undo` regroupe par geste (« mouse down events split undo groups », documenté [1]) ; la fenêtre Undo
  History
  liste les étapes et un clic ramène à ce point (documenté [2]). Les scènes sont du YAML texte (documenté [7]) ; les
  changements faits en jeu sont perdus à la sortie (documenté [3]) ; une astérisque marque la scène modifiée (documenté
  [5]). `Undo.DestroyObjectImmediate` détruit l'objet et l'annulation le recrée (documenté [1]) : notre choix, aux
  identifiants
  près, qu'on garde. Que `Undo` garde un état sérialisé de l'objet : supposé.
- **Unreal** : `FScopedTransaction` ouvre et ferme une transaction au rythme de sa portée, avec `Cancel()` (documenté
  [8]) ; la sauvegarde automatique est réglable (documenté [9]) ; les scènes sont des paquets binaires (`.umap`,
  supposé) ;
  un acteur supprimé reste en vie dans le tampon de transaction pour revenir (supposé), ce qui ressemble à la
  suppression
  douce que nous écartons.
- **Godot** : `UndoRedo` fusionne par `MERGE_ENDS` et borne la pile par `max_steps` (documenté [4]) ; les scènes sont
  des `.tscn`, un texte lisible (documenté [10]) ; un module d'extension dont l'undo ne marque pas la scène modifiée
  laisse fermer sans avertir (documenté [6]).
- **Levain** garde le texte lisible (JSON, LF) pour que git diffe une scène, détruit réellement les entités comme un
  moteur à ECS le doit, et réserve l'index plutôt que de garder l'objet en vie.

## Sources

1. Unity, *Undo* — https://docs.unity3d.com/ScriptReference/Undo.html
2. Unity, *Undo window* — https://docs.unity3d.com/Manual/UndoWindow.html
3. Unity, *Configurable Enter Play Mode* —
   https://docs.unity3d.com/2022.3/Documentation/Manual/ConfigurableEnterPlayMode.html
4. Godot, *UndoRedo* — https://docs.godotengine.org/en/stable/classes/class_undoredo.html
5. Unity, *Working with multiple scenes* — https://docs.unity3d.com/cn/2022.2/Manual/setupmultiplescenes.html
6. Godot, proposition 2153 — https://github.com/godotengine/godot-proposals/issues/2153
7. Unity, *Scene format* — https://docs.unity3d.com/Manual/FormatDescription.html
8. Epic Games, *FScopedTransaction* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FScopedTransaction
9. Epic Games, *Unreal Editor Preferences* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-preferences
10. Godot, *TSCN file format* — https://docs.godotengine.org/en/stable/engine_details/file_formats/tscn.html
