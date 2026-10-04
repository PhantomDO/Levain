# ADR-0026 — Intégrer Jolt : des corps qui suivent les entités, au pas fixe

- **Statut** : accepté le 2026-10-04 (options choisies par Donnovan sur sondage ; forme finale relue par un
  subagent, en mode autonome)
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
| B. Le jeu déclare jusqu'à 16 couches et leur matrice (Unity) | Souple | Du code et des tests dès M6.1, pour un seul jeu |

**5. Les threads** (sondage)

| Option | Pour | Contre |
|---|---|---|
| **A. Le pool de threads de Jolt en natif, un seul thread dans le navigateur** | La marge pour le monde ouvert ; Jolt reste déterministe quel que soit le nombre de threads | Deux configurations à tester |
| B. Un seul thread partout | Identique sur les deux cibles | La marge laissée sur la table |
| C. Un job system maison | Partagé plus tard avec le rendu | Un candidat v2 de la roadmap : beaucoup de travail maintenant |

## Décision

**Un module `engine/physics`, qui cache Jolt derrière des composants flecs et des fonctions libres.**

1. **Deux composants de données.** `physics::Collider` porte la forme (une boîte, une sphère, une capsule en
   M6.1 ; le maillage et le heightfield du terrain en M6.2) et la couche. `physics::RigidBody` porte le type de
   mouvement (`Dynamic` ou `Kinematic`), la masse, le frottement et le rebond. **Un `Collider` sans `RigidBody`
   est statique.** Les deux ne contiennent que des types de glm : rien de Jolt n'en sort.
2. **Un corps est créé par un observateur** quand une entité a un `Collider` et un `Transform`, et recréé quand
   son `Collider` ou son `RigidBody` change, arrive ou part. Il est détruit avec son `Collider` ou son entité.
   Le corps garde l'identifiant de l'entité dans ses *user data* ; l'entité garde l'identifiant du corps dans
   un composant `physics::BodyHandle`, posé par le module.
3. **Jolt fait autorité sur les corps dynamiques.** Après chaque pas, seuls les corps **actifs** (ceux que Jolt
   n'a pas endormis) recopient leur position et leur rotation dans le `Transform`, **par référence** : la
   recopie ne déclenche pas les observateurs. Un `Transform` **posé** (`set`) sur un corps le téléporte et le
   réveille. Un corps **cinématique** va dans l'autre sens : le gameplay écrit son `Transform`, et le module
   demande à Jolt de l'y amener pendant le pas (`MoveKinematic`), pour que ce qu'il pousse soit poussé. Un corps
   statique ne bouge pas : le téléporter reste possible, mais coûte une mise à jour de l'arbre du décor.
4. **Un corps est une entité racine, sans échelle.** Un `Collider` sur une entité qui a un parent, ou dont le
   `Transform` a une échelle autre que 1, est **refusé bruyamment** (règle n°7) : la taille se donne dans la
   forme. Un enfant d'un corps le suit normalement, par la hiérarchie.
5. **Trois phases dans le pipeline de simulation**, ordonnées par profondeur de `DependsOn` : `Simulation`, le
   gameplay, inchangée ; `Physics`, où le module pousse les cinématiques, avance Jolt d'un pas et recopie les
   corps actifs ; `PostPhysics`, pour ce qui lit le résultat du pas (les événements des volumes déclencheurs en
   M6.2, le personnage en M6.3). Les phases portent l'étiquette `SimulationPhase`, et non `flecs::Phase`, pour
   rester hors du pipeline par défaut.
6. **Un pas de Jolt par pas de simulation** : `Update(1/60 s, 1 étape de collision)`, la recommandation de Jolt
   pour 60 Hz. Les corps dynamiques reçoivent automatiquement un `PreviousTransform` (trait `With`, comme
   `Velocity`) : le rendu les interpole entre deux pas, comme le prévoyait l'ADR-0016.
7. **Une table fixe de couches**, avec leur matrice :

   | Couche | Touche |
   |---|---|
   | `Static` (le décor) | `Dynamic`, `Character`, `Debris` |
   | `Dynamic` | tout |
   | `Character` | `Static`, `Dynamic`, `Sensor` |
   | `Sensor` (les volumes déclencheurs) | `Dynamic`, `Character` |
   | `Debris` (ce qui tombe pour le décor) | `Static`, `Dynamic` |

   Deux couches de *broad phase*, `NonMoving` et `Moving` : la recommandation de Jolt pour commencer, chaque
   couche de broad phase ayant un coût.
8. **Le monde physique est un singleton flecs**, `physics::PhysicsWorld`, posé par `PhysicsModule` : il possède le
   `PhysicsSystem` de Jolt, son allocateur temporaire et son job system, derrière un pointeur opaque. Les
   fonctions libres le prennent en paramètre (ADR-0011) : `createBody`, `stepPhysics`, `readBackActiveBodies`…
   La glu flecs, une instruction par système, appelle ces fonctions.
9. **Threads** : le `JobSystemThreadPool` de Jolt en natif (cœurs − 1), le `JobSystemSingleThreaded` sous
   Emscripten, où les threads exigeraient des en-têtes HTTP (COOP et COEP) qu'un hébergement simple n'envoie pas.

## Conséquences

- **Ce que voit un plugin** : les en-têtes de `levain/physics/`, sans aucun type de Jolt. La visibilité est
  contrôlée par un test, comme fastgltf et ozz (`deps.jolt-visibility`). L'herbe pourra demander la hauteur du
  sol, l'eau déclarer un volume `Sensor`, la nage lire ce qui y entre.
- **Écrire le `Transform` d'un corps dynamique par référence ne sert à rien** : il est réécrit au pas suivant.
  Pour pousser un corps, il faudra des fonctions (`addImpulse`, `setLinearVelocity`), ajoutées quand un besoin
  les demandera ; pour le déplacer, `set<Transform>`. C'est la même règle que Godot, qui déconseille d'écrire
  la position d'un corps rigide.
- **Le coût de la recopie suit ce qui bouge** : un décor de dix mille corps endormis ne coûte rien par pas.
  L'ordre dans lequel Jolt rend les corps actifs n'est pas déterministe (ils sont réveillés depuis plusieurs
  threads), mais chacun écrit sa propre entité : le résultat ne dépend pas de l'ordre.
- **Le déterminisme promis par l'ADR-0016 tient** : Jolt est déterministe pour un même binaire et une même suite
  d'appels, quel que soit le nombre de threads. Un test le vérifiera, au bit près, avec un et plusieurs threads.
  Les rappels de contact de Jolt arrivent, eux, dans un ordre qui varie : les événements des volumes
  déclencheurs devront être triés avant d'être livrés au gameplay (M6.2).
- **Jolt doit être compilé avec les mêmes options que le moteur** (ses `JPH_*`) : un écart casse la mémoire
  sans prévenir. Le module appelle `JPH::VerifyJoltVersionID()` au démarrage et s'arrête en cas d'écart
  (règle n°7).
- **La physique tourne aussi dans le navigateur**, sur un thread : à vérifier en M6.1, Jolt n'ayant jamais été
  compilé par notre triplet Emscripten.
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
- **Pourquoi toujours une bibliothèque** : aucun des trois n'écrit son solveur depuis zéro pour le jeu — Unity
  prend PhysX, Godot a pris Jolt ; Unreal, qui a écrit Chaos, l'a fait sur plusieurs années, avec une équipe
  dédiée. L'étude E6 (M6.3) développera ce point.

## Sources

1. Unity, *Introduction to collider types* —
   https://docs.unity3d.com/Manual/collider-types-introduction.html
2. Unity, *Event function execution order* — https://docs.unity3d.com/Manual/execution-order.html
3. Unity, *Layer-based collision detection* — https://docs.unity3d.com/Manual/LayerBasedCollision.html
4. Godot, *Godot 4.6 release* — https://godotengine.org/releases/4.6/
5. Godot, *Using Jolt Physics* — https://docs.godotengine.org/en/4.6/tutorials/physics/using_jolt_physics.html
6. Godot, *Physics introduction* —
   https://docs.godotengine.org/en/stable/tutorials/physics/physics_introduction.html
7. Epic Games, *Collision Overview* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/collision-in-unreal-engine---overview
8. Jolt Physics, *Architecture* (sections « Deterministic Simulation », « Broad Phase », « Sensors ») —
   https://jrouwe.github.io/JoltPhysics/
9. flecs, *Systems Manual*, section « Custom pipeline » — https://www.flecs.dev/flecs/md_docs_2Systems.html
