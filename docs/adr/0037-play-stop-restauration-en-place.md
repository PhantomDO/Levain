# ADR-0037 — Play/Stop : jouer dans la Vue, puis rendre la scène en place

- **Statut** : proposé
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

Le critère (ROADMAP) : après Stop, la scène est identique à l'état d'avant Play (test).

Le code d'aujourd'hui aide :

- en Édition, aucun pas ne tourne (engine/scene/include/levain/scene/fixed_step.hpp:50-53), donc aucun corps Jolt
  n'existe : `BuildBodies` ne s'exécute que dans un pas (engine/physics/src/physics.cpp:362-372) ;
- les changements de mode passent déjà **hors du mode différé** (le mode où flecs met les écritures en file pendant
  un parcours) : dans `frame` (editor/src/editor.cpp:482-494) et après le `defer_end` de `ui` (editor.cpp:529-534,
  565-580) ;
- flecs 4.1.6 sait réveiller une entité avec sa génération (`ecs_make_alive`, flecs.h:3662-3687), copier une valeur
  par les crochets de son type (`ecs_value_copy`, flecs.h:6600, que l'inspecteur emploie : inspector.cpp:614-615),
  réordonner des enfants (`ecs_set_child_order`, flecs.h:3133-3155) et séparer des plages d'identifiants
  (`ecs_entity_range_new`, flecs.h:2700-2737).

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

1. **Ce que garde l'instantané.** À Play, pour chaque **entité de la scène** : son identifiant et sa génération, son
   type complet (composants, étiquettes, paires), la valeur de chaque composant copiée par ses crochets
   (`ecs_value_copy`), son nom, son parent et l'ordre de ses enfants.
   - *Une entité de la scène* : un identifiant d'utilisateur (au moins `EcsFirstUserEntityId`, flecs.h:2027), hors
     des internes du moteur (`isEngineInternal`, editor/src/hierarchy.cpp:166-170, élargi aux pipelines) et hors
     d'une portée de flecs, une paire `ChildOf` : le contenu d'un module, les champs décrits, les constantes d'une
     enum. La scène n'emploie que `flecs::Parent` (ADR-0015).
   - *Pas copiés* : ce que flecs tient lui-même, le nom (rendu par `ecs_set_name`) et `(ParentDepth, n)` ; et ce
     qu'un module marque `scene::Transient`, une étiquette sur le composant comme `Authored` : les handles de Jolt
     (`BodyHandle`, `CharacterHandle`), qui désigneraient des corps détruits.
   - *Refusé* : un composant de la scène qui ne se copie pas (`ECS_TYPE_HOOK_COPY_ILLEGAL`, flecs.h:928). Play ne
     part pas, et la console nomme l'entité et le composant (règle n°7). Aucun ne l'est aujourd'hui.
   - *Hors de la scène* : les singletons, qui vivent sur l'entité de leur composant. Ils gardent leur valeur de jeu,
     comme les champs non sérialisés d'Unity sans rechargement (documenté [2]).
   - Le code vit dans `scene` (`snapshot.hpp`), testé sans éditeur ni GPU, web compris ; seul l'éditeur l'appelle
     (ADR-0034).
2. **Une plage d'identifiants pour le jeu.** Play passe flecs sur une plage à part (au-dessus de 2^24 : seize
   millions d'identifiants restent à la scène), Stop revient à celle de l'édition. Chaque plage a sa propre liste
   d'index à réutiliser : un index détruit pendant le jeu n'est pas repris avant Stop (flecs,
   src/storage/entity_index.c:263-285). Sans cela, un composant enregistré pendant le jeu, qui reste après Stop,
   pourrait prendre l'index d'une caisse détruite, et `ecs_make_alive` refuserait de la réveiller (flecs,
   src/entity.c:3133-3139). La plage se crée une fois, dans `withEditor`, avant le démarrage du programme : flecs la
   refuse après une destruction (src/world.c:1518-1520), et le morceau 2 vérifie qu'aucune n'a eu lieu avant.
3. **Stop, dans cet ordre, hors du mode différé** (une assertion le vérifie, `outsideDeferredMode`) :
   1. détruire les entités nées pendant le jeu ; leurs enfants partent avec elles, et ceux de la scène reviennent à
      l'étape 3 ;
   2. revenir à la plage de l'édition ;
   3. réveiller les entités détruites, avec leur génération (`ecs_make_alive`) ;
   4. rendre les types et les valeurs. Les parents d'abord, par profondeur croissante : flecs refuse un cycle
      (src/storage/non_fragmenting_childof.c:217-222), et cet ordre n'en crée jamais. Puis les noms, effacés puis
      reposés, pour que deux frères qui ont échangé leurs noms ne se heurtent pas. Puis les composants manquants
      ajoutés, ceux de trop retirés (leurs observateurs détruisent les corps Jolt), et les valeurs réécrites par
      `setComponentValue` (ADR-0034) quand elles ont changé (`sameValue`) ; une valeur non décrite, qu'on ne sait pas
      comparer, toujours ;
   5. rendre l'ordre des enfants (`ecs_set_child_order`). Les enfants d'un `flecs::Parent` sont toujours ordonnés
      (non_fragmenting_childof.c:78-81) ; la liste donnée garde après eux les enfants internes, comme l'observateur
      d'un volume (physics.cpp:165-169), car flecs veut la liste entière ;
   6. **vérifier** : `differencesFrom(instantané)` doit être vide, sinon la console nomme les premières différences.
      Le test du critère appelle la même fonction.

   Stop part d'où partent déjà les changements de mode, `frame` et la fin de `ui` : la file `applyEditorRequests`
   de l'étude du 10/10 n'est pas nécessaire en M7.5.
4. **La physique, la même à chaque Play.** Play remplace le monde Jolt par un neuf (`freshPhysicsWorld`) : les
   identifiants de corps de Jolt repartent de zéro, `OverlapState` est vide, et un handle encore posé l'arrête
   bruyamment. Les corps et les personnages se construisent dans l'ordre des identifiants (`inIdOrder`), non dans
   celui des tables (physics.cpp:362-395), que la restauration change. Jolt étant déterministe pour un même binaire
   et une même suite d'appels (ADR-0026), jouer N pas, Stop, puis rejouer N pas donne les mêmes résultats, au bit
   près.
5. **Ce que Play garde aussi, et que Stop rend** : l'accumulateur du pas fixe (fixed_step.hpp:13) ; l'horloge des
   squelettes et les animateurs (engine/app/include/levain/app/models.hpp:71-73, 136-161), pour que le renard
   reprenne sa pose ; et **les assets en usage, retenus** pendant tout le jeu (`holdAssetsInUse`). Sans cela, un
   modèle dont la dernière entité est détruite en jeu serait déchargé en fin d'image
   (engine/app/src/app.cpp:1154-1158), et l'entité réveillée resterait invisible. L'input est oublié à chaque
   changement de mode (`enterMode`, editor/src/mode.cpp:56-71). Ne reviennent pas, car ce n'est pas la scène : la
   caméra de l'éditeur (le jeu ne la touche pas), le compteur de pas, le temps réel, les préférences.
6. **L'éditeur en jeu**, celui d'Unreal (sondage « Modèle ») :

   | Mode | Simulation | `frame` du programme | Le jeu reçoit l'input | Caméra et gestes |
   |---|---|---|---|---|
   | Édition | arrêtée | jamais | non | de l'éditeur |
   | Jouer | tourne | oui | Vue focalisée seulement | du jeu |
   | Simuler, si le sondage le garde | tourne | jamais | non | de l'éditeur |
   | Pause, en Jouer ou en Simuler | arrêtée | non | non | ceux du mode |

   - **Les touches** : Alt+P joue (supposé), Alt+S simule, Échap arrête, Maj+F1 rend la souris jusqu'au prochain
     clic dans la Vue (Unreal, documenté [1]). Pause : la touche Pause (Unreal, supposé) et le bouton. Image
     suivante : le bouton seul, car Unreal n'en documente pas et celle d'Unity, Ctrl+Alt+P [3], tombe sur AltGr+P
     sous Windows en AZERTY ; la fenêtre Raccourcis en pose une. Elle joue exactement un pas (`App::singleStep`, le
     pas de `--steps`), puis la pause reprend.
   - **La teinte** : en jeu, la barre d'outils et le bord de la Vue prennent la couleur du jeu, comme Unity assombrit
     ce qui n'est pas la vue Game (documenté [2]). La barre montre Jouer et Simuler en Édition ; Pause (puis
     Reprendre), Image suivante et Arrêter en jeu.
   - **La Hiérarchie et l'Inspecteur** montrent le monde vivant ; ce qu'on y change est perdu à Stop, comme dans Unity
     et Unreal (documenté [1], [2]). La sélection garde son identifiant : elle survit à Stop, sauf celle d'une entité
     née en jeu. La Hiérarchie range ses racines par identifiant, pour qu'une racine réveillée reprenne sa place :
     l'ordre des tables de flecs, lui, ne se rend pas.
   - **Quitter en jeu fait d'abord Stop**, la fin d'une exécution scriptée aussi : M7.3 proposera d'enregistrer la
     scène d'édition, jamais celle du jeu. Les préférences, elles, s'enregistrent en jeu comme ailleurs.
   - « Jouer (sans retour) » disparaît.
7. **La ligne de la CI**, sous son nom stable (ADR-0036, décision 14), à chaque Stop :
   `editor.stop steps=120 differences=0 ms=… -- Stop : …`. Une session scriptée sur la vue physics, où les caisses
   tombent, joue 120 pas puis Stop. La CI exige des pas, et zéro différence. Les millisecondes ne vont au journal que
   mesurées sur la machine de référence (règle n°6).
8. **Simuler, au choix de Donnovan** (sondage) : le Simulate d'Unreal (documenté [1]). La physique et le gameplay
   tournent sous la caméra et les gestes de l'éditeur, sans input du jeu ; la sélection et l'inspecteur marchent, et
   Stop restaure comme après Jouer. Son coût : un mode de plus dans les fonctions de `mode.hpp` et un bouton, un
   morceau d'environ 250 lignes tests compris. Aucune mécanique neuve, donc peu de risque.

## Conséquences

- `scene` gagne `snapshot.hpp` et `Transient`, et `isEngineInternal` y descend depuis l'éditeur. `physics` construit
  ses corps dans l'ordre des identifiants, en jeu aussi : les résultats chiffrés de la physique changent une fois.
  `app` gagne `singleStep` et la sauvegarde de l'animation ; `assets`, `holdAssetsInUse`.
- **Ne reviennent pas à Stop** : les singletons, ce que le programme garde hors du monde (ses captures), et ce qui
  s'enregistre pendant le jeu (composants, systèmes, observateurs : un `onEnter` posé en jeu sur un volume de la scène
  reste). Un crochet viendra si un programme en a besoin.
- Une entité née en jeu se reconnaît à son grand identifiant, au-dessus de 2^24.
- **M7.3** reprendra le réveil, les plages et `differencesFrom` pour l'annulation et l'aller-retour des scènes, et y
  refusera d'enregistrer en jeu.

## Les contrôles

Chaque contrôle a son contre-test : la faute injectée, le test rouge, puis vert (règle n°7).

| Ce qui est promis | Test | Contre-test |
|---|---|---|
| **Le critère** : après Stop, la scène est identique | `scene`, sans GPU, web compris : « le jeu » crée, détruit un parent et ses enfants, renomme, échange deux noms, inverse un parent et son enfant, ajoute et retire composants, étiquettes et paires, change des valeurs, réordonne des enfants ; après Stop, `differencesFrom` est vide, identifiants, générations et ordre des enfants égaux | Une restauration sans le réveil, ou sans les noms : rouge |
| Le critère dans l'éditeur | Le banc d'essai, vue physics : Alt+P, 120 pas, Échap ; la ligne `editor.stop` en CI (Linux, ASan, Windows) | Stop sans restauration, ou Play sans un seul pas : la CI échoue |
| Rejouer donne la même chose | N pas, Stop, N pas : les `Transform` égaux au bit près, une caisse détruite puis réveillée entre les deux | Les corps construits dans l'ordre des tables : rouge |
| Un index détruit revient | Un composant décrit enregistré en jeu, une caisse détruite : la caisse revient avec sa génération, le composant garde ses champs | Sans plage de jeu, le test voit l'index repris |
| Un composant qui ne se copie pas | Play refusé, l'entité et le composant nommés, l'Édition reste | Le même type marqué `Transient` : Play part, Stop le retire |
| Stop hors du mode différé | — | Appelé sous `defer_begin` : l'assertion part (test de processus) |
| Les assets tiennent, la pose revient | Le renard détruit en jeu, une image passée, Stop : il se dessine, ses matrices d'os égales à celles d'avant Play | Sans `holdAssetsInUse`, ou sans l'animation gardée : rouge |
| L'éditeur en jeu | Le banc : Pause, 0 pas ; Image suivante, 1 pas ; le jeu n'a les touches que Vue focalisée ; Maj+F1, puis un clic dans la Vue ; Échap depuis Pause ; quitter en jeu passe par Stop | Un clic hors de la Vue : rien n'atteint le jeu |
| Simuler, si gardé | Alt+S : des pas, `PlayerInput` vide touches tenues, la caméra de l'éditeur vole ; Stop identique | La route *jeu* en Simuler : `PlayerInput` se remplit, rouge |

Les gestes 16, 17, 36, 37 et 38 d'`editor/GESTES.md` passent à « couvert » avec ces scénarios (le 37, Simuler, à
« reporté » si le sondage ne le garde pas).

## Les morceaux

Chacun sous 400 lignes, tests compris, relu, vérifié par `tools/verify.sh`, fusionné dans `m7.5/play` (règle n°1).

| n° | Contenu | Modules | Lignes | Tests et contre-tests |
|---|---|---|---:|---|
| 0 | Cet ADR, le lien de la ROADMAP | docs | 200 | — |
| 1 | `snapshot.hpp` : `isSceneEntity` (`isEngineInternal` descendu de l'éditeur), `Transient`, `takeSnapshot` et son refus, `differencesFrom` | scene, editor | 350 | Le refus nommé ; `differencesFrom` trouve chaque sorte de différence plantée |
| 2 | `restoreSnapshot` et ses six étapes ; les plages d'identifiants ; `outsideDeferredMode` | scene | 400 | Le critère, sans GPU ; l'index détruit qui revient ; l'assertion du mode différé |
| 3 | `Transient` sur les handles, `freshPhysicsWorld`, `inIdOrder` ; `holdAssetsInUse` | physics, assets | 300 | Rejouer au bit près ; les assets retenus ; les contrôles chiffrés de la CI (vues physics, character, hike) relancés en local |
| 4 | Jouer et Arrêter à la place de « Jouer (sans retour) » : l'instantané, la restauration, l'accumulateur, l'animation, les assets ; les racines par identifiant ; quitter fait Stop ; la ligne `editor.stop` | editor, app | 400 | Le critère par le banc ; le renard ; quitter en jeu |
| 5 | Pause, Image suivante (`App::singleStep`), Maj+F1, la teinte, les barres, la table des raccourcis, `GESTES.md` | editor, app | 350 | L'éditeur en jeu, par le banc |
| 6 | **Simuler, seulement si le sondage le garde** | editor | 250 | Simuler, par le banc |
| 7 | La session scriptée de la CI sur la vue physics ; le script de coût sur les 10 000 cubes de la démo | tools, tests | 200 | Les deux contre-tests de la CI |
| fin | Journal, critère, gestes couverts, READMEs de scene, physics, app et editor, PR vers `main` et sa CI | docs | — | — |

## Ce que font les autres moteurs

- **Unity** : par défaut, Play détruit les objets de la scène et la recharge du disque (documenté [2]). Sans
  rechargement, il « only resets the scene's modified contents », sans recréer les objets, et les champs non
  sérialisés gardent leur valeur de jeu (documenté [2]) : notre choix, aux identifiants près. En jeu, l'éditeur
  assombrit ce qui n'est pas la vue Game, et les changements sont perdus à la sortie (documenté [2]) ; Pause et Step,
  Ctrl+P, Ctrl+Maj+P, Ctrl+Alt+P (documenté [3]).
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
