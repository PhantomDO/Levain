# ADR-0026 — Intégrer Jolt : des corps qui suivent les entités, au pas fixe

- **Statut** : accepté le 2026-10-04 (options choisies par Donnovan sur sondage ; forme finale relue par un
  subagent, en mode autonome, et corrigée selon sa relecture)
- **Date** : 2026-10-04
- **Milestone** : M6.1

## Contexte

La phase 6 apporte la physique : des caisses qui tombent (M6.1), des colliders, des raycasts et des volumes
déclencheurs (M6.2), le personnage (M6.3), puis la caméra, la nage et le planeur de *Rando* (M6.4, M6.5). La
bibliothèque est choisie depuis le début : **Jolt Physics** (SPECS §6), visible uniquement dans `physics/`
(SPECS §7). Reste à fixer comment elle s'intègre au modèle objet :

1. **comment une entité devient un corps physique**, et ce que voit le code qui l'écrit ;
2. **qui fait autorité** sur la position d'un corps, Jolt ou le `Transform` de flecs, et quand l'un recopie
   l'autre au pas fixe de l'ADR-0016 ;
3. **dans quel ordre** tournent le gameplay, le pas de physique et ce qui lit son résultat ;
4. **qui touche qui** : les couches de collision ;
5. **sur combien de threads**, en natif et dans le navigateur (ADR-0023) ;
6. **ce que voit un plugin** : l'herbe, l'eau ou la nage de *Rando* doivent pouvoir s'en servir sans voir Jolt.

## Options envisagées

Les quatre premières questions ont été posées à Donnovan en sondage le 04/10 ; l'ordre des systèmes vient d'un
prototype (même jour), qui a écarté la solution évidente.

**1. Déclarer un corps** (sondage)

| Option | Pour | Contre |
|---|---|---|
| **A. `Collider` + `RigidBody`** : un collider seul est un corps statique ; un `RigidBody` en plus le rend dynamique ou cinématique | Le modèle d'Unity, connu de Donnovan ; le décor ne déclare que sa forme | Deux composants qui se combinent : le corps se recrée quand l'un arrive ou part |
| B. Un seul composant `Body` (forme, mouvement, masse) | Une seule combinaison possible ; le `BodyInstance` d'Unreal | Un mur doit dire qu'il est statique |
| C. Les formes en entités enfants (Godot) | Plusieurs formes par corps, par la hiérarchie | Un corps devient un assemblage d'entités à reconstituer |

**2. L'autorité sur la position** (sondage)

| Option | Pour | Contre |
|---|---|---|
| **A. Jolt pour les dynamiques, flecs recopie** : après chaque pas, les seuls corps actifs écrivent leur `Transform` ; un `Transform` posé à la main téléporte le corps ; un cinématique va dans l'autre sens | Le coût suit ce qui bouge, pas ce qui existe ; aucune ambiguïté sur qui a raison | Un système qui écrit le `Transform` d'un corps dynamique par référence est écrasé au pas suivant |
| B. Comparer les deux à chaque pas | Un système peut écrire `Transform` librement | Une comparaison par corps et par pas, et les conflits sont masqués au lieu d'échouer |
| C. Des corps dans la hiérarchie, convertis monde ↔ local | Le plus permissif (Unity le tolère) | La source classique des objets qui tremblent ; plus de code |

**3. L'ordre dans le pipeline de simulation** (prototype)

Aujourd'hui, les systèmes de simulation tournent dans **l'ordre de leur déclaration** (ADR-0016). Un plugin
gameplay qui se sert de la physique importe le module physique avant de déclarer ses systèmes : ils tourneraient
donc toujours **après** le pas, et ce qu'ils écrivent (une poussée, la cible d'une plateforme) attendrait le pas
suivant, 16 ms plus tard.

| Option | Pour | Contre |
|---|---|---|
| **A. Des phases de simulation ordonnées** : `Simulation` (le gameplay), puis `Physics`, puis `PostPhysics`, rangées par `cascade(DependsOn)` | L'ordre d'Unity (`FixedUpdate`, puis le pas) ; les systèmes existants (`.kind<Simulation>()`) ne changent pas d'une ligne | Une notion de plus, et un piège trouvé au prototype (ci-dessous) |
| B. Garder l'ordre de déclaration | Rien à écrire | Un pas de retard sur tout ce que le gameplay demande à la physique, et l'ordre réel dépend de l'ordre des imports |
| C. Des phases marquées `flecs::Phase` | La façon documentée de créer des phases | **Écartée au prototype** : le pipeline par défaut prend toute phase marquée `flecs::Phase`, et rejouerait la simulation une fois de plus à chaque image, avec le temps réel |

**4. Les couches de collision** (sondage)

| Option | Pour | Contre |
|---|---|---|
| **A. Une table fixe du moteur** | Ce dont *Rando* a besoin, rien à configurer | Un jeu qui voudrait d'autres couches demandera un ADR |
| B. Le jeu déclare jusqu'à 32 couches et leur matrice (Unity) | Souple | Du code et des tests dès M6.1, pour un seul jeu |

**5. Les threads** (sondage)

| Option | Pour | Contre |
|---|---|---|
| **A. Le pool de threads de Jolt en natif, un seul thread dans le navigateur** | La marge pour le monde ouvert ; Jolt reste déterministe quel que soit le nombre de threads | Deux configurations à tester |
| B. Un seul thread partout | Identique sur les deux cibles | La marge laissée sur la table |
| C. Un job system maison | Partagé plus tard avec le rendu | Un candidat v2 de la roadmap : beaucoup de travail maintenant |

## Décision

**Un module `engine/physics`, qui cache Jolt derrière des composants flecs et des fonctions libres.**

1. **Deux composants de données.** `physics::Collider` porte la forme (une boîte, une sphère, une capsule en
   M6.1 ; le maillage et le heightfield du terrain en M6.2) et, s'il le faut, la couche. `physics::RigidBody`
   porte le type de mouvement (`Dynamic` ou `Kinematic`), la masse, le frottement et le rebond. **Un `Collider`
   sans `RigidBody` est statique.** Aucun type de Jolt n'en sort. Une grande forme (les 263 000 hauteurs du
   terrain, un maillage) ne tiendra pas dans un composant de valeurs : en M6.2, elle passera par une donnée
   partagée et immuable, dont le module garde la forme Jolt en cache.
2. **Les corps se construisent en lot, au début du pas de physique.** Un observateur marque l'entité dont le
   `Collider` ou le `RigidBody` arrive, change ou part ; le premier système de la phase `Physics` (re)construit
   les corps marqués. Un corps ne naît donc pas statique pour être aussitôt recréé dynamique selon l'ordre des
   `set`, et un chargement de milliers de corps pourra passer par l'ajout groupé de Jolt. Le corps garde
   l'identifiant de l'entité dans ses *user data* ; l'entité garde celui du corps dans un composant
   `physics::BodyHandle`, posé par le module. Le corps est détruit avec son `BodyHandle`, donc avec son
   `Collider` ou son entité.
3. **Qui écrit la position** :
   - **un corps dynamique** : Jolt. Après chaque pas, les seuls corps dynamiques **actifs** (ceux que Jolt n'a
     pas endormis) recopient leur position et leur rotation dans le `Transform`, **par référence** : la
     recopie ne déclenche pas les observateurs ;
   - **un corps cinématique** : le gameplay, qui écrit son `Transform` **par référence**. Le module demande à
     Jolt de l'y amener pendant le pas (`MoveKinematic`), pour que ce qu'il rencontre soit poussé. Il n'est
     jamais recopié : la pose que Jolt intègre diffère de la cible de quelques ulps ;
   - **tout corps** : un `Transform` **posé** (`set`) le téléporte, sans rien pousser, et le réveille s'il est
     mobile. Téléporter un corps statique réveille aussi ce qui reposait dessus et ce qu'il recouvre, ce que
     Jolt ne fait pas seul.
4. **Un corps est une entité racine, sans échelle.** Une entité qui a un `flecs::Parent` (ADR-0015, et non
   `ChildOf`), ou dont le `Transform` a une échelle autre que 1, est **refusée bruyamment** (règle n°7) :
   une erreur au journal, pas de corps, et une assertion en Debug. Le contrôle est refait quand le corps est
   téléporté ou que l'entité change de parent. La taille se donne dans la forme ; un enfant d'un corps le suit
   normalement, par la hiérarchie.
5. **Trois phases dans le pipeline de simulation**, déclarées par `scene` et ordonnées par profondeur de
   `DependsOn` : `Simulation`, le gameplay, inchangée ; `Physics`, où le module construit les corps, pousse
   les cinématiques, avance Jolt d'un pas et recopie les dynamiques ; `PostPhysics`, pour ce qui lit le
   résultat du pas (les événements des volumes déclencheurs en M6.2). Le personnage de M6.3 choisira sa phase :
   les exemples de Jolt le mettent à jour avant le pas. Les phases portent l'étiquette `SimulationPhase`, et
   non `flecs::Phase`, pour rester hors du pipeline par défaut.
6. **Un pas de Jolt par pas de simulation** : `Update(1/60 s, 1 étape de collision)`, la recommandation de Jolt
   pour 60 Hz. **Tout `RigidBody`**, dynamique ou cinématique, reçoit un `PreviousTransform` (trait `With`, comme
   `Velocity`) : le rendu l'interpole entre deux pas, comme le prévoyait l'ADR-0016. Une plateforme non
   interpolée ferait trembler les caisses interpolées qu'elle porte.
7. **Une table fixe de couches**, avec leur matrice et leur couche de *broad phase* :

   | Couche | Touche | Broad phase |
   |---|---|---|
   | `Static` (le décor) | `Dynamic`, `Character`, `Debris` | `NonMoving` |
   | `Dynamic` | tout | `Moving` |
   | `Character` | `Static`, `Dynamic`, `Sensor` | `Moving` |
   | `Sensor` (les volumes déclencheurs) | `Dynamic`, `Character` | `NonMoving` |
   | `Debris` (ce qui tombe pour le décor) | `Static`, `Dynamic` | `Moving` |

   Sans couche donnée, un corps prend celle de son mouvement : `Static` sans `RigidBody`, `Dynamic` avec. Une
   couche `Static` par défaut, gardée par un corps dynamique, le ferait traverser le sol. Deux couches de
   *broad phase* : la recommandation de Jolt pour commencer, chacune ayant un coût.
8. **Le monde physique est un singleton flecs**, `physics::PhysicsWorld`, posé par `PhysicsModule` dans sa
   portée : il possède le `PhysicsSystem` de Jolt, son allocateur temporaire et son job system, derrière un
   pointeur opaque. Les fonctions libres le prennent en paramètre (ADR-0011) : `createBody`, `stepPhysics`,
   `collectMovedBodies`… La glu flecs, une instruction par système, appelle ces fonctions.
   - **Les plafonds** : 65 536 corps, 65 536 paires et 10 240 contacts, 10 Mio de mémoire de travail par pas,
     de quoi tenir les 1 000 caisses de M6.1 et le décor de *Rando*. Un corps refusé faute de place, ou un pas
     que Jolt n'a pas pu finir (`EPhysicsUpdateError`), donne une erreur au journal et une assertion en Debug.
   - **L'état global de Jolt** (son allocateur, sa fabrique de types, son journal) appartient au processus,
     pas à un monde : il s'installe avec le premier `PhysicsWorld`, une seule fois, et n'est jamais
     désinstallé. Les tests créent beaucoup de mondes, l'éditeur et le jeu en auront deux ; et le
     réinstaller après l'avoir désinstallé plante en WebAssembly (vu à l'implémentation).
   - **À la fermeture du monde**, flecs supprime les entités avant les modules : le singleton survit aux
     corps. Le module ne détruit alors aucun corps un par un ; le destructeur du `PhysicsSystem` les libère
     tous.
9. **Threads** : le `JobSystemThreadPool` de Jolt en natif (cœurs − 1), le `JobSystemSingleThreaded` sous
   Emscripten, où les threads exigeraient des en-têtes HTTP (COOP et COEP) qu'un hébergement simple n'envoie pas.

## Conséquences

- **Ce que voit un plugin** : les en-têtes de `levain/physics/`, sans aucun type de Jolt. La visibilité est
  contrôlée par le même test que fastgltf et ozz (`deps.asset-libraries-visibility`), étendu aux plugins. La
  caméra de M6.4 fera un *sphere cast*, l'eau déclarera un volume `Sensor`, la nage lira ce qui y entre.
- **Écrire le `Transform` d'un corps dynamique par référence ne sert à rien** : il est réécrit au pas suivant.
  Pour pousser un corps, il faudra des fonctions (`addImpulse`, `setLinearVelocity`), ajoutées quand un besoin
  les demandera ; pour le déplacer, `set<Transform>`. C'est la même règle que Godot, qui déconseille d'écrire
  la position d'un corps rigide.
- **Ce que coûte un corps qui dort** : un corps statique ne coûte rien par pas. Un corps dynamique endormi
  n'est pas recopié, mais il a un `PreviousTransform` : environ 20 ns par pas et par image (mesures de
  l'ADR-0016), soit 0,2 ms pour dix mille. L'ordre dans lequel Jolt rend les corps actifs n'est pas déterministe
  (ils sont réveillés depuis plusieurs threads), mais chacun écrit sa propre entité : le résultat n'en dépend
  pas.
- **Le déterminisme promis par l'ADR-0016 tient** : Jolt est déterministe pour un même binaire et une même suite
  d'appels (Architecture.md), et son propre test le vérifie avec 0 et 15 threads de travail
  (`PhysicsDeterminismTests.cpp`). Le nôtre le vérifiera au bit près, avec un et plusieurs threads. Le natif et
  le navigateur sont deux binaires : ils ne donneront pas les mêmes résultats, ce que l'ADR-0016 ne promettait
  pas. Les rappels de contact arrivent, eux, dans un ordre qui varie, et sur les threads de Jolt : les
  événements des volumes déclencheurs devront être collectés, triés, puis livrés au gameplay hors de ces
  threads (M6.2).
- **Jolt doit être compilé avec les mêmes options que le moteur** (ses `JPH_*`) : un écart casse la mémoire
  sans prévenir. Le port les exporte avec sa cible, et le module vérifie `JPH::VerifyJoltVersionID()` au
  démarrage, avec un message dans notre journal, avant l'arrêt. Ce contrôle ne couvre pas le jeu
  d'instructions : le port x64 compile Jolt en **AVX2**, que le moteur exige désormais du processeur. Jolt est
  lié en `PRIVATE` à `physics` : ses options ne s'appliquent qu'à nos sources de ce module.
- **La physique tourne aussi dans le navigateur**, sur un thread : le port se compile pour `wasm32-emscripten`,
  l'édition de liens et l'exécution restent à vérifier en M6.1.
- **Un volume déclencheur statique perd le contact d'un corps qui s'endort** (Architecture.md, « Sensors ») : une
  sortie fantôme est possible. Le personnage, lui, signale ses contacts par un autre canal
  (`CharacterContactListener`). À traiter en M6.2 et M6.5.
- **Les couches sont figées** : un second jeu qui en voudrait d'autres demandera un ADR, qui pourra reprendre
  l'option B.
- **À revoir** si un corps doit vraiment vivre dans une hiérarchie (un wagon accroché à un train), ou si les
  phases de simulation se multiplient au point de demander un vrai graphe d'ordre.

## Ce que font les autres moteurs

- **Unity** (PhysX) : un `Collider` sans `Rigidbody` est un collider statique, un `Rigidbody` le rend dynamique
  ou cinématique (**documenté** [1]). La simulation tourne « directement après `MonoBehaviour.FixedUpdate` »
  (**documenté** [2]) : notre phase `Simulation` puis `Physics`. Les couches sont 32 *layers* que le projet
  nomme, avec une matrice de collision (**documenté** [3]) : l'option B, que nous n'avons pas prise.
- **Godot 4** : Jolt est intégré depuis la 4.4, et c'est **le moteur physique par défaut des nouveaux projets
  depuis la 4.6** (**documenté** [4], [5]). Un corps est un nœud (`RigidBody3D`, `StaticBody3D`), et ses formes
  des nœuds enfants (`CollisionShape3D`) : notre option C. 32 couches, par masques de bits sur chaque objet. La
  documentation demande de ne jamais mettre d'échelle sur une forme de collision, et de ne pas écrire
  directement la position d'un corps rigide (**documenté** [6]) : nos points 3 et 4.
- **Unreal** (Chaos depuis Unreal 5) : la physique est portée par les *primitive components*, avec un
  `BodyInstance` qui réunit forme, masse et réglages : notre option B. Les collisions se règlent par
  *object channels* et par réponse (*Block*, *Overlap*, *Ignore*) (**documenté** [7]).
- **Une bibliothèque, ou un solveur maison** : Unity délègue à PhysX ; Godot a longtemps eu son propre solveur,
  Godot Physics, avant de passer à Jolt par défaut ; Unreal a écrit Chaos pour remplacer PhysX (**documenté**
  pour les trois, mais ce qu'a coûté chaque choix est **supposé**). L'étude E6 (M6.3) creusera ce point.

## Sources

1. Unity, *Introduction to collider types* —
   https://docs.unity3d.com/Manual/collider-types-introduction.html
2. Unity, *Event function execution order* — https://docs.unity3d.com/Manual/execution-order.html
3. Unity, *Layer-based collision detection* — https://docs.unity3d.com/Manual/LayerBasedCollision.html
4. Godot, *Godot 4.6 release* — https://godotengine.org/releases/4.6/
5. Godot, *Using Jolt Physics* — https://docs.godotengine.org/en/4.6/tutorials/physics/using_jolt_physics.html
6. Godot, *Physics introduction* (la page est écrite pour la 2D ; la règle de l'échelle vaut aussi en 3D,
   **supposé**) —
   https://docs.godotengine.org/en/stable/tutorials/physics/physics_introduction.html
7. Epic Games, *Collision Overview* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/collision-in-unreal-engine---overview
8. Jolt Physics, *Architecture* (sections « Deterministic Simulation », « Broad Phase », « Sensors ») —
   https://jrouwe.github.io/JoltPhysics/
9. Jolt Physics, test `UnitTests/Physics/PhysicsDeterminismTests.cpp` (v5.6.0) —
   https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/UnitTests/Physics/PhysicsDeterminismTests.cpp
10. flecs, *Systems Manual*, section « Custom pipeline » — https://www.flecs.dev/flecs/md_docs_2Systems.html
