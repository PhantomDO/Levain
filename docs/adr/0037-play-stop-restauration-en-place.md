# ADR-0037 — Play/Stop : jouer dans la Vue, puis rendre la scène en place

- **Statut** : accepté le 2026-10-15 par sondage : « J'accepte l'ADR-0037 ? » : « Accepter (Recommandé) » ;
  « Faut-il aussi le Simuler d'Unreal (Alt+S) dans M7.5 ? » : « Play et Simuler (Recommandé) ». Amendé le même
  jour par la relecture, contre flecs 4.1.6, avant tout code
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
   - *Refusés* : un composant de la scène qui ne se copie pas (`ECS_TYPE_HOOK_COPY_ILLEGAL`, flecs.h:928), et une
     instance de prefab, `(IsA, *)`, dont les valeurs héritées ne se copient pas entité par entité. Play ne part pas,
     et la console nomme l'entité et la cause. Il n'y en a aucun aujourd'hui.
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
   suivent : il ne reste plus de paire `InsideOf`. Puis, hors du mode différé (une assertion, `outsideDeferredMode`) :
   1. détruire les entités nées en jeu ; leurs enfants partent avec elles, et ceux de la scène reviennent à
      l'étape 3 ;
   2. revenir à la plage de l'édition, avant le réveil : un index de la scène détruit en jeu attend dans la liste de
      cette plage (src/storage/entity_index.c:275-285), et ne retrouve sa place qu'une fois elle active ;
   3. réveiller les entités détruites, avec leur génération (`ecs_make_alive`). flecs refuse en Debug un index vivant
      d'une autre génération (src/entity.c:3133-3139) ; Levain vérifie `ecs_get_alive(index) == 0` avant chaque
      réveil ;
   4. rendre les types et les valeurs :
      - les parents, par profondeur croissante : flecs refuse un cycle en Debug
        (src/storage/non_fragmenting_childof.c:217-222), et cet ordre n'en crée jamais ;
      - les composants manquants ajoutés, ceux de trop retirés (leurs observateurs détruisent les corps Jolt) ;
      - les valeurs, par `setComponentValue` (ADR-0034), quand elles ont changé (`sameValue`) ; une valeur non
        décrite, qu'on ne sait pas comparer, toujours ;
      - **le nom de chaque entité de la scène, effacé puis reposé.** flecs accepte deux frères du même nom (seuls un
        symbole ou un alias l'arrêtent : src/entity_name.c:436-444), mais son index des noms peut perdre une
        entrée : un frère prend le nom d'un autre puis est détruit, et `lookup` ne trouve plus le premier ;
   5. rendre l'ordre des enfants (`ecs_set_child_order`). La liste part de `ecs_get_ordered_children` et garde, après
      les enfants de la scène, les enfants internes (physics.cpp:165-169) ; flecs ne compare sa taille qu'en Debug,
      Levain la vérifie ;
   6. **vérifier** : `differencesFrom(instantané)` doit être vide, `lookup` de chaque chemin compris, sinon la
      console nomme les premières différences. Le test du critère appelle la même fonction.

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
   `editor.stop steps=120 differences=0 ms=… -- Stop : …`. Une session scriptée sur la vue physics joue 120 pas, les
   caisses tombent, puis Stop : la CI exige des pas et zéro différence. Les millisecondes ne vont au journal que
   mesurées sur la machine de référence (règle n°6).
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
| Le critère dans l'éditeur | Le banc d'essai, vue physics : Alt+P, 120 pas, Échap ; la ligne `editor.stop` en CI (Linux, ASan, Windows) | Stop sans restauration, ou Play sans un seul pas : la CI échoue |
| Rejouer donne la même chose | N pas, Stop, N pas : les `Transform` égaux au bit près, une caisse **du milieu** de la pile détruite puis réveillée entre les deux (elle revient en fin de table) | Les corps construits dans l'ordre des tables : rouge |
| Un index détruit revient | Une caisse détruite en jeu, puis un `onEnter` posé en jeu sur un autre volume, qui reste après Stop : la caisse revient avec sa génération | Sans plage de jeu, le test voit l'index de la caisse pris par l'observateur |
| Les gardes de Levain, en Release aussi | Un réveil sur un index vivant, une liste d'enfants de la mauvaise taille, une plage créée après une destruction : erreur nommée, le monde intact | Une garde retirée : en Release, le test voit le monde faux |
| Stop ne réveille aucun rappel du jeu | Un corps dans un volume à Stop : son `onExit` n'est pas appelé, `InsideOf` et `OverlapState` vides | Sans désactiver les observateurs : `onExit` appelé, rouge |
| Un déclencheur détruit en jeu | Stop le rend, et la console nomme l'observateur qui manque | Sans la note des enfants internes : aucun message, rouge |
| Un composant qui ne se copie pas, une instance de prefab | Play refusé, l'entité et la cause nommées, l'Édition reste | Le composant marqué `Transient` : Play part, Stop le retire |
| Stop hors du mode différé | — | Appelé sous `defer_begin` : l'assertion part (test de processus) |
| Les assets tiennent, la pose revient | Le renard détruit en jeu, une image passée, Stop : il se dessine, ses matrices d'os égales à celles d'avant Play | Sans `holdAssetsInUse`, ou sans l'animation gardée : rouge |
| L'éditeur en jeu | Le banc : Pause, 0 pas ; Image suivante, 1 pas ; le jeu n'a les touches que Vue focalisée ; Maj+F1, puis un clic dans la Vue ; Échap depuis Pause ; quitter en jeu passe par Stop | Un clic hors de la Vue : rien n'atteint le jeu |
| Simuler | Alt+S : des pas, `PlayerInput` vide touches tenues, la caméra de l'éditeur vole ; Stop identique | La route *jeu* en Simuler : `PlayerInput` se remplit, rouge |

Les gestes 16, 17, 36 et 38 d'`editor/GESTES.md` passent à « couvert » aux morceaux 4 et 5, le 37 (Simuler) au
morceau 6.

## Les morceaux

Chacun sous 400 lignes, tests compris, relu, vérifié par `tools/verify.sh`, fusionné dans `m7.5/play` (règle n°1).

| n° | Contenu | Modules | Lignes | Tests et contre-tests |
|---|---|---|---:|---|
| 0 | Cet ADR, le lien de la ROADMAP | docs | 230 | — |
| 1 | `snapshot.hpp` : `isSceneEntity` (`isEngineInternal` descendu de l'éditeur), `Transient`, `takeSnapshot` et ses refus, `differencesFrom` | scene, editor | 350 | Les refus nommés ; `differencesFrom` trouve chaque sorte de différence plantée |
| 2a | Les plages (`createApp`) et leur garde ; `restoreSnapshot`, étapes 1 à 3 ; `outsideDeferredMode` | scene, app | 300 | L'index détruit qui revient ; la garde du réveil ; l'assertion du mode différé |
| 2b | Étapes 4 à 6 : types, valeurs, noms, ordre des enfants | scene | 350 | Le critère, sans GPU ; la garde de la liste des enfants |
| 3 | `Transient` sur les handles, `endPlay`, `freshPhysicsWorld`, `inIdOrder` ; `holdAssetsInUse` | physics, assets | 350 | Rejouer au bit près ; aucun rappel à Stop ; le déclencheur détruit ; les assets retenus ; les contrôles chiffrés de la CI (vues physics, character, hike) relancés en local |
| 4 | Jouer et Arrêter à la place de « Jouer (sans retour) » : l'instantané, `endPlay` et la restauration, l'accumulateur, l'animation, les assets ; les racines par index ; quitter fait Stop ; la ligne `editor.stop` | editor, app | 400 | Le critère par le banc ; le renard ; quitter en jeu |
| 5 | Pause, Image suivante (`App::singleStep`), Maj+F1, la teinte, les barres, la table des raccourcis, `GESTES.md` | editor, app | 350 | L'éditeur en jeu, par le banc |
| 6 | Simuler | editor | 250 | Simuler, par le banc |
| 7 | La session scriptée de la CI sur la vue physics ; le script de coût sur les 10 000 cubes de la démo | tools, tests | 200 | Les deux contre-tests de la CI |
| fin | Journal, critère, gestes couverts, READMEs de scene, physics, app et editor, PR vers `main` et sa CI | docs | — | — |

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
