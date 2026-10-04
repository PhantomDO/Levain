# `engine/physics`

## Rôle

La physique des corps rigides, sur **Jolt Physics** ([ADR-0026](../../docs/adr/0026-integration-de-jolt.md)) :
des entités flecs qui tombent, se heurtent et se poussent, au pas fixe de la simulation (ADR-0016). Jolt fait le
calcul ; ce module est la frontière entre Jolt et le reste du moteur.

**État en M6.1** : des boîtes, des sphères et des capsules, statiques, dynamiques ou cinématiques, sur cinq
couches fixes. Les maillages, le heightfield du terrain, les raycasts et les volumes déclencheurs viennent en
M6.2, le personnage en M6.3.

## Invariants

1. **Aucun type de Jolt ne sort du module.** `PhysicsWorld` le cache derrière un pointeur vers `PhysicsState`,
   défini dans `src/`. Seul `src/` inclut Jolt (`deps.asset-libraries-visibility`), qui est lié en `PRIVATE`.
2. **Un `Collider` seul est statique** ; un `RigidBody` en plus le rend dynamique ou cinématique. Sans couche
   donnée, un corps prend celle de son mouvement (`effectiveLayer`) ; un corps mobile sur `Static` est refusé
   (`whyNotThisLayer`), il traverserait le décor.
3. **Qui écrit la position** :
   - un corps **dynamique** : Jolt, et le module la recopie dans le `Transform`, par référence, pour les seuls
     corps éveillés (`collectMovedBodies`) ;
   - un corps **cinématique** : le gameplay, qui écrit son `Transform` par référence dans la phase
     `Simulation` ; le module le transmet à Jolt (`moveKinematic`), qui pousse ce qu'il rencontre ;
   - **tout corps** : un `set<Transform>` le téléporte, sans rien pousser, et garde sa vitesse. Un statique
     déplacé réveille ses voisins, ce que Jolt ne fait pas seul.
4. **Un corps est une racine sans échelle** (`whyNotABody`) : une entité qui a un `flecs::Parent`, ou une
   échelle autre que 1, n'a pas de corps, et le journal le dit (assertion en Debug). La taille est dans la forme.
   Le refus n'est pas définitif : l'échelle revenue à 1, ou le parent retiré, le corps revient. Une forme sans
   épaisseur ou une masse nulle sont refusées de même (`whyNotThisShape`) : Jolt ne vérifie rien en Release.
5. **Les corps se construisent en lot**, au début de la phase `Physics` : un observateur pose `BodyDirty` (à
   l'arrivée, au changement ou au départ d'un `Collider` ou d'un `RigidBody`, `add` compris), le système
   `BuildBodies` (re)construit. Un corps n'existe donc qu'à partir du pas qui suit son `set`.
6. **Le pas est dans la phase `Physics`** du pipeline de simulation, entre le gameplay (`Simulation`) et ce qui
   lit son résultat (`PostPhysics`) : `SyncBeforePhysics`, `BuildBodies`, `PushKinematicBodies`, puis
   `StepPhysics`, dans cet ordre. Le premier est un point de synchronisation : sans lui, un `set` fait par le
   gameplay serait appliqué après le pas. `world.component<levain::scene::Physics>().disable()` met la
   physique en pause, et avec elle `PostPhysics`, qui en dépend ; désactiver `Simulation` arrête toute la
   simulation. C'est la règle des phases du pipeline intégré de flecs.
7. **L'état global de Jolt** (allocateur, fabrique, journal) appartient au processus : il s'installe avec le
   premier `PhysicsWorld`, une seule fois, et n'est jamais désinstallé. Au démarrage, `VerifyJoltVersionID`
   vérifie que Jolt est compilé avec les options du moteur, et arrête le programme sinon.
8. **Déterministe au bit près** pour un même binaire, quel que soit le nombre de threads (test). Le natif et le
   navigateur, deux binaires, ne donnent pas les mêmes résultats.

## Mesures

Critère de M6.1, 1 000 caisses en chute libre sans un pas au-dessus de 4 ms (Release, machine de référence,
`./build/linux-release/tests/levain_physics_bench`, 600 pas, trois lancements) : **0,52 ms par pas en
moyenne, 1,1 à 2,4 ms au pire**, avec 15 threads de travail ; 1,7 ms en moyenne et 3,3 à 3,6 ms au pire sur un
seul thread (`levain_physics_bench 0`), comme dans le navigateur. Le pas qui construit les 1 001 corps coûte
1,4 ms (2,5 sur un thread). La scène est celle de `levain_sandbox --view physics` (`sandbox/src/crates.hpp`) :
les deux finissent avec la plus haute caisse à 6,39 m.

## Pièges connus

- **Une caisse posée s'enfonce de 2 cm** : c'est la *penetration slop* de Jolt (`PhysicsSettings::
  mPenetrationSlop`), qu'il laisse pour que les piles ne tremblent pas. Un test qui attend la hauteur exacte
  échoue ; `restsOn` (dans `tests/physics_test.cpp`) en tient compte.
- **`Jolt/Jolt.h` avant tout autre en-tête de Jolt** : il définit les macros dont les autres ont besoin.
  `.clang-format` le garde en tête de son bloc.
- **Le binaire exige AVX2** : le port vcpkg compile Jolt pour x64 avec AVX2, FMA et F16C, et ses options
  s'appliquent aux sources de ce module.
- **Réinstaller Jolt plante en WebAssembly** : `UnregisterTypes` puis `RegisterTypes` finit en accès mémoire
  hors limites dans `Factory::Register`, sous Node ; en natif, ça passait. D'où l'installation unique
  (`installJoltOnce`), vue par le test de déterminisme, qui crée deux mondes l'un après l'autre.
- **Un corps endormi n'est pas recopié**, mais un corps dynamique endormi paie quand même son
  `PreviousTransform` (environ 20 ns par pas et par image, ADR-0016). Seul le décor statique ne coûte rien.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/physics/components.hpp`](include/levain/physics/components.hpp) | `Collider` (forme et couche), `RigidBody` (mouvement, masse, frottement, rebond), `BodyHandle`, `effectiveLayer` |
| [`include/levain/physics/layers.hpp`](include/levain/physics/layers.hpp) | `Layer` et la matrice `layersCollide` |
| [`include/levain/physics/physics_world.hpp`](include/levain/physics/physics_world.hpp) | `PhysicsWorld`, `createPhysicsWorld`, `createBody`, `teleportBody`, `moveKinematic`, `stepPhysics`, `collectMovedBodies` : la logique, sans flecs |
| [`include/levain/physics/body_rules.hpp`](include/levain/physics/body_rules.hpp) | `whyNotABody`, `whyNotThisLayer`, `poseOf` |
| [`include/levain/physics/physics.hpp`](include/levain/physics/physics.hpp) | `PhysicsModule` — `world.import<levain::physics::PhysicsModule>()` |
| [`src/physics_world.cpp`](src/physics_world.cpp) | Jolt : les couches, le job system, les corps, le pas |
| [`src/physics.cpp`](src/physics.cpp) | La glu flecs : observateurs et systèmes de la phase `Physics` |

## Équivalents ailleurs

| Moteur | Où | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `UPrimitiveComponent`, `FBodyInstance`, Chaos | La forme, la masse et les réglages sont réunis dans le `BodyInstance` du composant ; les collisions se règlent par *object channels* et réponses (**documenté**, ADR-0026). |
| **Unity** | `Collider`, `Rigidbody`, PhysX | Le même découpage qu'ici : un collider sans `Rigidbody` est statique, et la simulation suit `FixedUpdate` (**documenté**, ADR-0026). 32 couches nommées par le projet, là où Levain en fixe cinq. |
| **Godot** | `RigidBody3D`, `StaticBody3D`, `CollisionShape3D`, Jolt | Jolt par défaut depuis la 4.6 ; la forme est un nœud enfant du corps, et la documentation interdit l'échelle sur une forme (**documenté**, ADR-0026). |
