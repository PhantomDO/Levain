# ADR-0037 — Play/Stop : jouer dans la Vue, puis rendre la scène en place

- **Statut** : accepté le 2026-10-15 par sondage : « J'accepte l'ADR-0037 ? » : « Accepter (Recommandé) » ;
  « Faut-il aussi le Simuler d'Unreal (Alt+S) dans M7.5 ? » : « Play et Simuler (Recommandé) ». Amendé le même
  jour par la relecture, contre flecs 4.1.6, avant tout code ; **réalisé** le 2026-10-16, M7.5 clos (journal) : le
  texte dit maintenant ce que le code fait (« Amendements du 2026-10-16 » : le refus du mode différé dans tous les
  builds, la garde avant l'étape 1, les noms reposés sans effacement, la table des refus, les contrôles, les
  morceaux tels que fusionnés). Deux cases du tableau de la décision 6 ont changé en route : le `frame` du programme
  tourne pour Image suivante (morceau 5) et en Simuler, sous l'input vide (morceau 6, note ¹)
- **Date** : 2026-10-15
- **Milestone** : M7.5 (issue #258)
- **Modifie** : l'ADR-0036, décision 3 (« Jouer (sans retour) » disparaît) et la table de sa décision 13 (Pause,
  Image suivante) ; la passation de l'ADR-0034 dans #258 (l'instantané garde toute la scène, pas seulement les
  octets `Authored`)

## Contexte

M7.7 a posé le mode Édition : la simulation à l'arrêt, le jeu sans input, la caméra de l'éditeur. Pour jouer, il
n'y a que « Jouer (sans retour) » : ce que le jeu déplace reste déplacé. M7.5 doit rendre la scène telle qu'elle
était, **sans relire de fichier** (ROADMAP). Le format de scène n'arrive qu'en M7.3, et surtout, le code garde des
identifiants d'entités qu'un rechargement casserait :

- le sandbox capture le joueur et la caméra dans un système (sandbox/src/main.cpp:670-672) ;
- chaque corps de Jolt porte l'identifiant de son entité (engine/physics/src/physics_world.cpp:298) ;
- l'éditeur garde sa sélection par identifiant (editor/include/levain/editor/editor.hpp:100-102).

**La génération** : flecs réutilise l'index d'une entité détruite et augmente alors un compteur, la génération, qui
fait partie de l'identifiant. Un identifiant gardé ne vaut que pour la même génération. Stop doit donc rendre
chaque entité **avec son identifiant exact**, génération comprise.

Le critère (ROADMAP) : après Stop, la scène est identique à l'état d'avant Play (test). Le code d'aujourd'hui aide :
en Édition, aucun pas ne tourne (engine/scene/include/levain/scene/fixed_step.hpp:50-53), donc aucun corps Jolt
n'existe (`BuildBodies` ne s'exécute que dans un pas : engine/physics/src/physics.cpp:362-372) ; les changements de
mode passent déjà **hors du mode différé**, le mode où flecs met les écritures en file pendant un parcours (`frame` :
editor/src/editor.cpp:482-494 ; après le `defer_end` de `ui` : editor.cpp:529-534, 565-580) ; et flecs 4.1.6 sait
réveiller une entité avec sa génération (`ecs_make_alive`, flecs.h:3662-3687), copier une valeur par les crochets
de son type (`ecs_value_copy`, flecs.h:6600, déjà dans inspector.cpp:614-615), réordonner des enfants
(`ecs_set_child_order`, flecs.h:3133-3155) et séparer des plages d'identifiants (flecs.h:2700-2737).

## Options envisagées

- **A. Un instantané en mémoire à Play, rendu en place à Stop** : retenue. C'est le Play d'Unity sans rechargement
  de scène, qui « only resets the scene's modified contents » (documenté [2]).
- B. Recharger la scène d'un fichier, le défaut d'Unity (documenté [2]) : pas de format avant M7.3, des identifiants
  neufs qui cassent le joueur du sandbox, Jolt et la sélection, et le JSON de flecs arrondit (ADR-0034).
- C. Jouer dans une copie du monde, comme Unreal (supposé) : flecs ne copie pas un monde avec ses systèmes et ses
  observateurs, et `App` n'a qu'un monde (engine/app/include/levain/app/app.hpp:262) et un monde Jolt.
- D. Jouer dans un autre processus, comme Godot (documenté [4]) : un second programme à lancer et à piloter, et un
  protocole à écrire pour montrer et inspecter le jeu dans l'éditeur.
- E. Ne garder que les octets `Authored`, la passation de l'ADR-0034 dans #258 : `Collider` (non décrit), `MeshRef`
  (en lecture seule), les étiquettes comme `BodyDirty`, les paires et l'état du jeu garderaient leurs valeurs de jeu.

## Décision

**flecs ne vérifie qu'en Debug** plusieurs conditions dont Stop dépend (`ecs_check`, retiré sous `FLECS_NDEBUG`) :
en Release, une faute corromprait le monde sans rien dire. Là où ce texte dit « flecs le refuse en Debug », **Levain
le vérifie lui-même**, dans tous les builds (règle n°7).

1. **Ce que garde l'instantané.** À Play, pour chaque **entité de la scène** : son identifiant et sa génération, son
   type complet (composants, étiquettes, paires), la valeur de chaque composant copiée par ses crochets
   (`ecs_value_copy`), son nom, son parent, l'ordre de ses enfants, et ses enfants internes (les observateurs d'un
   `onEnter`).
   - *Une entité de la scène* est ce que la Hiérarchie pourrait montrer : ni module, ni système, ni observateur, ni
     requête, ni composant, ni prefab (`isEngineInternal`, editor/src/hierarchy.cpp:166-170) ; rien de ce que flecs
     range sous un module (sa **table** porte `(ChildOf, *)` ; on lit la table, car un enfant de `flecs::Parent`
     répond oui à `has(ChildOf, *)`) ; ni flecs lui-même (un index sous `EcsFirstUserEntityId`, flecs.h:2027). La
     scène n'emploie que `flecs::Parent` (ADR-0015).
   - *Pas copiés* : ce que flecs tient lui-même, le nom (reposé par `ecs_set_name`) et `(ParentDepth, n)` ; et ce
     qu'un module marque `scene::Transient`, une étiquette sur le composant comme `Authored` : les handles de Jolt
     (`BodyHandle`, `CharacterHandle`), qui désigneraient des corps détruits.
   - *Refusés* : un composant de la scène qui ne se copie pas (`ECS_TYPE_HOOK_COPY_ILLEGAL`, flecs.h:928) ou sans
     constructeur par défaut (`CTOR_ILLEGAL` : flecs s'arrête dans `ecs_value_new`) ; une instance de prefab,
     `(IsA, *)`, dont les valeurs héritées ne se copient pas entité par entité ; deux frères du même nom ; une
     entité à `Transform` rangée par `ChildOf` sous autre chose qu'un module (la Hiérarchie la montre, l'instantané
     ne la garde pas). Play ne part pas, et la console nomme l'entité et la cause. Il n'y en a aucun aujourd'hui
     (*amendé le 2026-10-16*, A4).
   - *Hors de la scène* : les singletons, sur l'entité de leur composant ; ils gardent leur valeur de jeu, comme les
     champs non sérialisés d'Unity sans rechargement (documenté [2]). Le code vit dans `scene` (`snapshot.hpp`),
     testé sans éditeur ni GPU, web compris ; seul l'éditeur l'appelle (ADR-0034).
2. **Une plage d'identifiants pour le jeu.** En jeu, flecs prend les nouveaux identifiants au-dessus de 2^24, et
   chaque plage a sa liste d'index à réutiliser : l'index d'une caisse détruite en jeu ne peut pas être repris par un
   observateur ou une requête créés ensuite, qui restent après Stop. La caisse peut donc toujours revenir.
   - *Né en jeu* : une entité de la scène d'index au moins 2^24. Le reste de ce que crée le jeu reste : observateurs,
     systèmes, requêtes (dans la plage du jeu), composants (flecs leur donne d'abord un petit index, sous 256). La
     physique ne crée aucune entité pendant un pas ; un `onEnter` est un observateur rangé sous son volume par
     `ChildOf`, donc un interne.
   - *Où* : dans `createApp`, juste après le monde ; flecs crée lui-même la plage de l'édition, [1, 2^24 − 1]
     (src/world.c:1535-1540), que `ecs_entity_range_get` rend. flecs refuse en Debug une plage créée après une
     destruction (world.c:1518-1520) ; Levain vérifie qu'il n'y en a eu aucune (`ecs_get_entities`, flecs.h:2151).
     Le jeu livré a les deux plages, et reste dans celle de l'édition.
3. **Stop.** Avant tout, **la physique finit le jeu** (`physics::endPlay`, que l'éditeur appelle) : elle désactive
   les observateurs des déclencheurs (une étiquette du module sur ceux d'`onEnter` et `onExit` ; `ecs_enable`, que
   flecs respecte : src/observer.c:342), retire toutes les paires `(InsideOf, *)` (`ecs_remove_all`, flecs.h:3284),
   vide `OverlapState`, puis les réactive. Aucun `onExit` du jeu ne part, ni là, ni pendant les destructions qui
   suivent : il ne reste plus de paire `InsideOf`. Puis, hors du mode différé (un refus dans tous les builds, le monde
   intact : A1) :
   1. détruire les entités nées en jeu ; leurs enfants partent avec elles, et ceux de la scène reviennent à
      l'étape 3 ;
   2. revenir à la plage de l'édition, avant le réveil : un index de la scène détruit en jeu attend dans la liste de
      cette plage (src/storage/entity_index.c:275-285), et ne retrouve sa place qu'une fois elle active ;
   3. réveiller les entités détruites, avec leur génération (`ecs_make_alive`). flecs refuse en Debug un index vivant
      d'une autre génération (src/entity.c:3133-3139) ; Levain le vérifie **avant l'étape 1** (`ecs_get_alive(index)`
      vaut 0 ou la génération d'avant Play), pour qu'un refus laisse le monde tel quel (A2) ;
   4. rendre les types et les valeurs :
      - les parents, par profondeur croissante : flecs refuse un cycle en Debug
        (src/storage/non_fragmenting_childof.c:217-222), et cet ordre n'en crée jamais ;
      - les composants manquants ajoutés, ceux de trop retirés (leurs observateurs détruisent les corps Jolt) ;
      - les valeurs, par `setComponentValue` (ADR-0034), quand elles ont changé (`sameValue`) ; une valeur non
        décrite, qu'on ne sait pas comparer, toujours ;
      - **le nom de chaque entité de la scène, reposé après les parents** (`ecs_set_name`, même pour un nom qui n'a
        pas changé), puis vérifié par `lookup(chemin) == id`. L'index des noms de flecs peut perdre une entrée sans
        que le nom change : un enfant devenu racine puis remis sous son parent
        (src/storage/non_fragmenting_childof.c:345), ou un frère qui prend le nom d'un autre puis est détruit (A3) ;
   5. rendre l'ordre des enfants (`ecs_set_child_order`). La liste part de `ecs_get_ordered_children` et garde, après
      les enfants de la scène, les enfants internes (physics.cpp:165-169) ; flecs ne compare sa taille qu'en Debug,
      Levain la vérifie (A6) ;
   6. **vérifier** : `differencesFrom(instantané)` doit être vide, `lookup` de chaque chemin compris, sinon la
      console nomme les premières différences. Le test du critère appelle la même fonction. Seul un enfant interne
      perdu y fait exception (A8).

   **L'invariant de l'Édition**, dont dépend l'étape 4 : aucun pas n'y tourne, donc tout `Collider` y garde
   `BodyDirty`, et tout `PreviousTransform` y égale son `Transform`. Les observateurs du moteur qui partent alors
   (`MarkColliderDirty`, `RecheckRefusedBody`, `ResetPreviousTransform`) ne recréent que ce que l'instantané
   contient ; `differencesFrom` le prouve à chaque Stop. Stop part d'où partent déjà les changements de mode, `frame`
   et la fin de `ui` : la file `applyEditorRequests` de l'étude du 10/10 n'est pas nécessaire en M7.5.
4. **La physique, la même à chaque Play.** Play remplace le monde Jolt par un neuf (`freshPhysicsWorld`) : les
   identifiants de corps de Jolt repartent de zéro, `OverlapState` est vide, et un handle encore posé l'arrête
   bruyamment. Les corps et les personnages se construisent, et les personnages avancent, dans l'ordre des index
   (`inIdOrder`, les 32 bits bas de l'identifiant), non dans celui des tables (physics.cpp:362-407), que la
   restauration change. Jolt étant déterministe pour un même binaire et une même suite d'appels (ADR-0026), jouer
   N pas, Stop, puis rejouer N pas donne les mêmes résultats, au bit près, **pour une scène qui ne fait pas naître
   de corps en jeu et garde son état de jeu dans ses entités** : les index nés en jeu reviennent au Play suivant en
   ordre inverse, génération plus un, et les singletons ne sont pas rendus.
5. **Ce que Play garde aussi, et que Stop rend** : l'accumulateur du pas fixe (fixed_step.hpp:13) ; l'horloge des
   squelettes et les animateurs (engine/app/include/levain/app/models.hpp:71-73, 136-161), pour que le renard
   reprenne sa pose ; **les assets en usage, retenus** tout le jeu (`holdAssetsInUse`), sans quoi le modèle d'une
   entité détruite en jeu serait déchargé en fin d'image (engine/app/src/app.cpp:1154-1158) et la réveillée
   invisible. L'input est oublié à chaque changement de mode (editor/src/mode.cpp:56-71). La caméra de l'éditeur, le
   compteur de pas, le temps réel et les préférences ne sont pas la scène.
6. **L'éditeur en jeu**, celui d'Unreal (sondage « Modèle ») :

   | Mode | Simulation | `frame` du programme | Le jeu reçoit l'input | Caméra et gestes |
   |---|---|---|---|---|
   | Édition | arrêtée | jamais | non | de l'éditeur |
   | Jouer | tourne | oui | Vue focalisée seulement | du jeu |
   | Simuler | tourne | oui, sous l'input vide¹ | non | de l'éditeur |
   | Pause, en Jouer ou en Simuler | arrêtée | non, sauf pour Image suivante (input vide, la durée d'un pas) | non | ceux du mode |

   ¹ Amendé au morceau 6 : le tableau disait « jamais ». `FpsInput` du sandbox est un singleton du monde que le `frame` du
   programme pose et que Stop ne rend pas (`WalkInput` est un composant du joueur, que Stop rend) ; sans `frame`, il garde
   la dernière image de Jouer (W tenue, Échap, Alt+S : la caméra du jeu avançait toute seule). Le `frame` tourne donc à chaque image de Simuler, sous la route de
   l'éditeur, un input vide : le jeu ne reçoit rien (`tests/editor_pause_gpu.cpp simulate`).

   - **Les touches** : Alt+P joue (supposé), Alt+S simule, Échap arrête, Maj+F1 rend la souris jusqu'au prochain
     clic dans la Vue (Unreal, documenté [1] ; refusée en Simuler, où la souris est déjà à l'éditeur) ; Pause, la
     touche Pause (supposé) et le bouton. Image suivante, le
     bouton seul : Unreal n'en documente pas, et Ctrl+Alt+P d'Unity [3] est AltGr+P sous Windows en AZERTY. Elle
     joue exactement un pas (`App::singleStep`, le pas de `--steps`), puis la pause reprend.
   - **La teinte** : en jeu, la barre d'outils et le bord de la Vue prennent la couleur du jeu, comme Unity assombrit
     ce qui n'est pas la vue Game (documenté [2]) ; Simuler a la même teinte que Jouer. La barre montre Jouer et
     Simuler en Édition ; Pause (puis Reprendre), Image suivante et Arrêter en jeu.
   - **La Hiérarchie et l'Inspecteur** montrent le monde vivant ; ce qu'on y change est perdu à Stop, comme dans Unity
     et Unreal (documenté [1], [2]). La sélection garde son identifiant : elle survit à Stop, sauf celle d'une entité
     née en jeu. La Hiérarchie range ses racines par index (retriées quand sa requête des racines a changé), pour
     qu'une racine réveillée reprenne sa place : l'ordre affiché change donc une fois pour toutes, celui de la
     création au lieu de celui des tables, qui ne se rend pas.
   - **Quitter en jeu fait d'abord Stop**, la fin d'une exécution scriptée aussi : M7.3 proposera d'enregistrer la
     scène d'édition, jamais celle du jeu. Les préférences, elles, s'enregistrent en jeu comme ailleurs.
   - « Jouer (sans retour) » disparaît.
7. **La ligne de la CI**, sous son nom stable (ADR-0036, décision 14), à chaque Stop :
   `editor.stop steps=120 differences=0 ms=… from=play -- Stop : …` (`from=simulate` depuis Simuler : sans lui, un Alt+S
   qui jouerait rendrait la même ligne). Une session scriptée sur la vue physics joue 120 pas, les caisses tombent, puis
   Stop : la CI exige **exactement** 120 pas et zéro différence (A7). Les millisecondes ne vont au journal que mesurées sur
   la machine de référence (règle n°6).
8. **Simuler** : le Simulate d'Unreal (documenté [1]). La physique et le gameplay tournent sous la caméra et les
   gestes de l'éditeur, sans input du jeu ; la sélection et l'inspecteur marchent ; Stop restaure comme après Jouer.

## Conséquences

- `scene` gagne `snapshot.hpp` et `Transient`, et `isEngineInternal` y descend depuis l'éditeur. `physics` gagne
  `endPlay` et construit ses corps dans l'ordre des index, en jeu aussi : les résultats chiffrés de la physique
  changent une fois. `app` gagne les plages, `singleStep` et la sauvegarde de l'animation ; `assets`,
  `holdAssetsInUse`. La scène tient en 16 millions d'identifiants, jeu livré compris ; ce qui naît en jeu se
  reconnaît à son grand identifiant.
- **Ne reviennent pas à Stop** : les singletons, ce que le programme garde hors du monde (ses captures), et ce qui
  s'enregistre pendant le jeu (composants, systèmes, observateurs : un `onEnter` posé en jeu sur un volume de la scène
  reste). **Un déclencheur détruit en jeu revient sans ses rappels** : ses observateurs, des internes, partent avec
  lui, et leur code ne se réveille pas ; la console le dit. Un crochet viendra si un programme en a besoin.
- **M7.3** reprendra le réveil, les plages et `differencesFrom` pour l'annulation et l'aller-retour des scènes, et y
  refusera d'enregistrer en jeu.

## Les contrôles

Chaque contrôle a son contre-test : la faute injectée, le test rouge, puis vert (règle n°7).

| Ce qui est promis | Test | Contre-test |
|---|---|---|
| **Le critère** : après Stop, la scène est identique | `scene`, sans GPU, web compris : « le jeu » crée, détruit un parent et ses enfants, renomme, donne à un frère le nom d'un autre puis le détruit, inverse un parent et son enfant, ajoute et retire composants, étiquettes et paires, change des valeurs, réordonne des enfants ; après Stop, `differencesFrom` est vide, identifiants, générations, `lookup` et ordre des enfants égaux | Une restauration sans le réveil, ou sans reposer tous les noms : rouge |
| Le critère dans l'éditeur | Le banc d'essai, vue physics : Alt+P, 120 pas, Échap ; la ligne `editor.stop` en CI (Linux et ASan par le test `editor.play-stop`, Windows par `ci-programs.sh editor-play`) | Stop sans restauration, ou Play sans un seul pas : la CI échoue |
| Rejouer donne la même chose | N pas, Stop, N pas : les `Transform` égaux au bit près, une caisse **du milieu** de la pile détruite puis réveillée entre les deux (elle revient en fin de table) | Les corps construits dans l'ordre des tables : rouge |
| Un index détruit revient | Une caisse détruite en jeu, puis un `onEnter` posé en jeu sur un autre volume, qui reste après Stop : la caisse revient avec sa génération | Sans plage de jeu, le test voit l'index de la caisse pris par l'observateur |
| Les gardes de Levain, en Release aussi | Un réveil sur un index vivant, une liste d'enfants de la mauvaise taille, une plage créée après une destruction : erreur nommée, le monde intact | Une garde retirée : en Release, le test voit le monde faux |
| Stop ne réveille aucun rappel du jeu | Un corps dans un volume à Stop : son `onExit` n'est pas appelé, `InsideOf` et `OverlapState` vides | Sans désactiver les observateurs : `onExit` appelé, rouge |
| Un déclencheur détruit en jeu | Stop le rend, et la console nomme l'observateur qui manque | Sans la note des enfants internes : aucun message, rouge |
| Ce que Stop ne saurait rendre : un composant qui ne se copie pas ou sans constructeur par défaut, une instance de prefab, deux frères du même nom, une entité à `Transform` sous `ChildOf` hors d'un module | Play refusé, l'entité et la cause nommées, l'Édition reste (A4) ; une entité née en jeu sous une entité de la scène par `ChildOf` : Stop la nomme | Le composant marqué `Transient` : Play part, Stop le retire |
| Stop hors du mode différé | Appelé sous `defer_begin` : `restoreSnapshot` et `stopPlay` refusent, le monde intact et l'éditeur toujours en jeu, avec ou sans physique (A1) | La garde retirée : le test voit Stop passer sous `defer_begin`. Pas de test de processus (A5) |
| Les assets tiennent, la pose revient | Le renard détruit en jeu, une image passée, Stop : il se dessine, ses matrices d'os égales à celles d'avant Play | Sans `holdAssetsInUse`, ou sans l'animation gardée : rouge |
| L'éditeur en jeu | Le banc : Pause, 0 pas ; Image suivante, 1 pas ; le jeu n'a les touches que Vue focalisée ; Maj+F1, puis un clic dans la Vue ; Échap depuis Pause ; quitter en jeu passe par Stop | Un clic hors de la Vue : rien n'atteint le jeu |
| Simuler | Alt+S : des pas, `PlayerInput` vide touches tenues, la caméra de l'éditeur vole ; Stop identique | La route *jeu* en Simuler : `PlayerInput` se remplit, rouge |

Les gestes 16, 17, 36 et 38 d'`editor/GESTES.md` passent à « couvert » aux morceaux 4 et 5, le 37 (Simuler) au
morceau 6.

## Les morceaux

Chacun d'environ 400 lignes au plus (règle n°2), tests compris, relu, vérifié par `tools/verify.sh`, fusionné dans
`m7.5/play` (règle n°1).

*Note du 2026-10-16, ce qui a été livré* : le tableau du plan est remplacé par celui des PR fusionnées, l'estimation
du plan à côté. Les lignes sont les ajouts plus les suppressions de chaque PR (`gh pr view <n> --json
additions,deletions`).

| n° | Ce qui est entré | PR (lignes de chacune) | Estimé | Fusionné |
|---|---|---|---:|---:|
| 0 | Cet ADR, le lien de la ROADMAP | #517 | 230 | 235 |
| 1 | `isSceneEntity`, `Transient` et `isEngineInternal` descendu de l'éditeur (1a) ; `takeSnapshot` (1b) ; ses refus (1c) ; `differencesFrom` (1d) | #521 à #524 (189, 350, 195, 307) | 350 | 1 041 |
| 2a | Les plages (`createApp`) et leur garde ; `restoreSnapshot`, étapes 1 à 3 | #525 | 300 | 338 |
| 2b | Étapes 4 et 5 : types, valeurs, noms, ordre des enfants | #526 | 350 | 342 |
| 2c | *Ajouté.* L'étape 6 : le résultat vérifié, les enfants internes perdus, les entités nées en jeu sous la scène par `ChildOf` | #527 | — | 189 |
| 3 | Les corps et les personnages dans l'ordre des index (3a) ; `freshPhysicsWorld` et un `endPlay` silencieux (3b) ; `holdAssetsInUse` (3c) | #518 à #520 (296, 236, 119) | 350 | 651 |
| 4 | `Play` à la place de « Jouer (sans retour) » (4a) ; ce que Stop demande hors de l'éditeur : la mémoire d'animation, la comparaison, un seul ordre d'index, les handles `Transient` (4b) ; `startPlay`, `stopPlay` et la ligne `editor.stop` (4c) ; Jouer et Arrêter dans l'éditeur, quitter et finir par Stop (4d) ; le critère par le banc (4e) ; la sélection, le renard, quitter, la fin scriptée, Play refusé (4f) | #528 à #533 (218, 359, 391, 358, 414, 266) | 400 | 2 006 |
| 5 | ImGui en pas fixes sous un script, `App::singleStep` (5a) ; les touches du jeu lues dans les événements bruts (5b) ; Pause, Image suivante, Maj+F1 (5c) ; la pause par le banc (5d) ; les boutons (5e) ; la teinte (5f) ; le bandeau de la souris rendue, les deux dispositions, les docs (5g) ; le `frame` du programme voit la durée d'un pas, après la seconde relecture (5h) | #535 à #541 (418, 393, 385, 433, 363, 401, 321), #542 (181) | 350 | 2 895 |
| 6 | `Mode::Simulate` (6a) ; Alt+S, le bouton, les refus (6b) ; `editor.stop` dit le mode quitté (6c) ; Simuler par le banc, les docs (6d) | #543 à #546 (252, 289, 67, 345) | 250 | 953 |
| 7 | La session scriptée de la CI sur la vue physics ; le script de coût sur les 10 000 cubes | #534 | 200 | 151 |
| fin | Journal, critère, gestes couverts, READMEs, cet amendement ; PR vers `main` et sa CI | cette clôture | — | — |
| | **Total, sans la clôture** | 30 PR, #517 à #546 | **2 780** | **8 801** |

**Les morceaux ont dépassé leur estimation, ceux de l'éditeur de très loin** : 5 fois pour le morceau 4, 8 fois pour le
5 et 4 fois pour le 6, 3 fois pour le plan entier (le 1 à 3 fois, le 3 à près de 2). Seuls les morceaux 0, 2a, 2b et 7
sont à moins de 15 % au-dessus de leur estimation. Les tests sont 57 % des lignes livrées (4 673 sur 8 247,
`git diff --numstat 594f418 c3e5e3e`), les bancs d'essai de l'App en tête (`tests/editor_pause_gpu.cpp`, 1 211 lignes) :
chaque comportement de l'éditeur coûte un scénario de bout en bout, que le plan n'avait pas compté.
Les morceaux 1 à 6 ont été écrits d'un bloc puis découpés en PR qui compilent chacune avec leurs tests ; la plus grande
en fait 433 (#538, un banc), les autres 418 ou moins. Le prochain ADR d'un milestone d'éditeur estimera ses bancs à part.

## Amendements du 2026-10-16

Ce que le code fait et que le texte d'acceptation ne disait pas, ou disait autrement. Le texte plus haut est corrigé ;
voici pourquoi, un par un. Les relectures sont dans le journal de M7.5.

- **A1. Le mode différé est refusé dans tous les builds** (décision 3, « hors du mode différé »). Le texte disait « une
  assertion, `outsideDeferredMode` ». C'est un refus : `restoreSnapshot` rend une erreur avant de toucher au monde
  (`ecs_is_deferred`), `stopPlay` refuse dès sa première ligne, avec ou sans module de physique, et `physics::endPlay`
  aussi ; l'éditeur reste en jeu (`StopReport::refused`). Pourquoi : en Release l'assertion disparaît, et flecs met
  alors les écritures de Stop en file : une restauration à moitié faite (règle n°7 ; relectures des morceaux 2, 3 et 4).
- **A2. La garde de l'index vient avant l'étape 1** (décision 3, étape 3). Le texte la mettait « avant chaque réveil »,
  après les destructions des étapes 1 et 2. `refuseTakenIndexes` vérifie tout d'abord : un index pris par une autre
  génération arrête Stop, le monde intact.
- **A3. Les noms sont reposés après les parents, sans passe d'effacement** (décision 3, étape 4). Le texte disait « effacé
  puis reposé ». Aucun scénario n'avait besoin de l'effacement (son contre-test restait vert) ; `ecs_set_name` pour tous
  les noms, même inchangés, suffit à ranger l'entrée qui manquait. Le piège exact, relu dans les sources de flecs :
  l'index des noms range un nom sous le parent qu'il avait quand on l'a posé ; un enfant qui perd son `flecs::Parent`
  en sort sans être rangé à la racine (src/storage/non_fragmenting_childof.c:345). `differencesFrom` vérifie
  `lookup(chemin) == id`.
- **A4. Play refuse plus que le texte ne disait** (décision 1, « Refusés » ; les contrôles). Un composant sans
  constructeur par défaut (`CTOR_ILLEGAL` : flecs s'arrête dans `ecs_value_new`), deux frères du même nom (`lookup` n'en
  rend qu'un, et Stop échouerait à chaque fois), une entité à `Transform` rangée par `ChildOf` sous autre chose qu'un
  module (la Hiérarchie la montre, l'instantané ne la garde pas). Une entité née en jeu sous une entité de la scène par
  `ChildOf` ne peut pas être refusée avant le jeu : elle n'est pas de la scène, Stop ne la détruirait pas, et il échoue
  donc en la nommant.
- **A5. Plus de test de processus** (les contrôles, « Stop hors du mode différé »). Le refus de A1 se teste dans le
  processus des tests : « restoreSnapshot en mode différé est refusé, le monde intact », « Stop en mode différé est
  refusé : on reste en jeu, rien n'a changé », et son cousin sans module de physique.
- **A6. `ecs_get_ordered_children` s'arrête sur une assertion pour un parent qui n'a aucun enfant `flecs::Parent`**
  (décision 3, étape 5), par exemple un volume dont les seuls enfants sont ses observateurs `ChildOf`. `orderedChildrenOf`
  ne l'appelle qu'avec un enfant `Parent` (vérifié sur flecs 4.1.6 ; `engine/scene/README.md`, `build/GOTCHA.md`).
- **A7. La CI exige exactement 120 pas** (décision 7). Un seuil « au moins un » laissait vert un Échap cassé sous
  Windows : la fin scriptée passe par Stop et dit environ 127 pas, zéro différence (relecture du morceau 7). Linux lit la
  ligne par ctest (`editor.play-stop`, `editor.simulate-stop`, Linux seulement) ; Windows par `tools/ci-programs.sh
  editor-play`. La ligne dit aussi d'où Stop part (`from=play` ou `from=simulate`).
- **A8. L'étape 6 laisse passer une seule sorte de différence, l'enfant interne perdu** (décision 3, étape 6). Le texte voulait `differencesFrom` vide.
  Un volume détruit en jeu revient sans l'observateur d'`onEnter` ou d'`onExit` rangé dessous, et Stop ne peut pas le
  rendre : `RestoreReport` le nomme, la console en fait un avertissement, et toute autre différence reste une erreur.
- **A9. Les morceaux, tels que fusionnés** : le tableau plus haut, avec un morceau 2c que le plan n'avait pas, et les
  morceaux 5 et 6 découpés en 5a à 5h et 6a à 6d.
- **Les décisions 6, 7 et 8** ont été amendées dans leur texte aux morceaux 5 et 6 : la ligne Pause du tableau
  (« non, sauf pour Image suivante »), le `frame` du programme qui tourne en Simuler sous l'input vide (le tableau disait
  « jamais » : la décision qui a le plus changé, note ¹), `from=` dans la ligne `editor.stop`, Maj+F1 refusée en
  Simuler. Elles ne sont pas répétées ici.

## Ce qui reste (note du 2026-10-16)

- **Le coût de Stop sur la machine de référence** : `tools/play-stop-cost.sh` (règle n°6). Les temps mesurés au portable
  pendant les morceaux sont des ordres de grandeur, aucun n'est au journal.
- **Choisir une entité d'un clic dans la Vue** (geste n° 2 d'`editor/GESTES.md`) est à M7.4, en Édition comme en
  Simuler ; le banc choisit par la Hiérarchie.
- **« Keep Simulation Changes » (K)** d'Unreal : après la phase 7, non demandé.
- **Les limites de Stop ne bougent pas** (Conséquences) : les singletons et ce que le programme garde hors du monde ne
  reviennent pas, un déclencheur détruit en jeu revient sans ses rappels, la scène tient en 16 millions d'identifiants.
- **M7.3** reprend le réveil, les plages et `differencesFrom` pour l'annulation et l'aller-retour des scènes.

## Ce que font les autres moteurs

- **Unity** : par défaut, Play recharge la scène du disque ; sans rechargement, il « only resets the scene's
  modified contents » sans recréer les objets, et les champs non sérialisés gardent leur valeur de jeu : notre
  choix, aux identifiants près. En jeu, l'éditeur assombrit ce qui n'est pas la vue Game, et les changements sont
  perdus à la sortie (documenté [2]) ; Ctrl+P, Ctrl+Maj+P, Ctrl+Alt+P (documenté [3]).
- **Unreal** : Play dans la Vue ; Simulate (Alt+S), Pause, Frame Skip, Échap, Maj+F1 ; les changements perdus, sauf
  « Keep Simulation Changes » (K) (documenté [1]). Alt+P et la touche Pause sont supposés. Le jeu tourne dans une
  copie du monde de l'éditeur (supposé, d'après le code source, `CreatePIEWorldByDuplication`, non revérifié).
- **Godot** : le jeu tourne toujours dans un autre processus, « no matter the embedding mode used » (documenté [4]) ;
  l'arbre « Remote » inspecte et modifie le jeu en cours (documenté [5]).

## Sources

1. Epic Games, *Playing and Simulating* — https://dev.epicgames.com/documentation/en-us/unreal-engine/playing-and-simulating-in-unreal-engine
2. Unity, *Game view* et *Scene reloading* — https://docs.unity3d.com/Manual/GameView.html,
   https://docs.unity3d.com/Manual/scene-reloading.html
3. Unity 2018.3, *Unity Hotkeys* — https://docs.unity3d.com/2018.3/Documentation/Manual/UnityHotkeys.html
4. Godot, *Game embedding* — https://docs.godotengine.org/en/stable/tutorials/editor/game_embedding.html
5. Godot, *Overview of debugging tools* — https://docs.godotengine.org/en/stable/tutorials/scripting/debug/overview_of_debugging_tools.html
