# `engine/scene`

## Rôle

Le modèle objet du moteur : le monde flecs, ses composants et ses systèmes (ADR-0004). Tout ce qui vit dans une
partie (objets, caméra, lumières) sera une entité de ce monde.

**État en M3.3** : les composants `Transform`, `Velocity`, `WorldTransform` et `PreviousTransform`, décrits
pour la réflexion de flecs. **Deux pipelines** : la simulation (`ApplyVelocity`, `SavePreviousTransform`)
tourne à pas fixe, 60 Hz, autant de fois par image qu'il le faut ; le rendu (`ComputeWorldTransforms`) tourne
une fois par image et affiche l'entre-deux. Le sandbox en fait 10 000 cubes, enfants d'une entité `grid` :
lever la grille dans l'explorer lève les 10 000 cubes.

## Invariants

1. **flecs est visible ici et au-dessus, jamais en dessous** (SPECS §7) : ni dans `core`, `platform`, `gpu`, ni
   dans `render`, qui est sur une autre branche du graphe. Vérifié par le test `deps.flecs-visibility`. C'est
   l'application qui relie la scène et le rendu.
2. **La logique en fonctions libres, la glu flecs en une instruction** (ADR-0011) : `applyVelocity`
   (`motion.hpp`) ne sait rien de flecs et se teste seule ; `scene.cpp` ne fait que la brancher sur les entités.
3. **Les composants sont des données** : pas de méthode, pas de pointeur vers d'autres entités. Le lien
   parent-enfant est un composant de flecs, `flecs::Parent`.
4. **La hiérarchie passe par `flecs::Parent`, jamais par `child_of`** ([ADR-0015](../../docs/adr/0015-stockage-de-la-hierarchie.md)) :
   une entité ne peut pas avoir les deux, et `ComputeWorldTransforms` ne voit que le premier — un enfant rangé
   par `ChildOf` serait traité comme une racine. Un enfant se crée par
   `world.entity(flecs::Parent{parent}, "nom")`.
5. **Un composant porte son sens dans son type** : `PreviousTransform` enveloppe un `Transform` plutôt que
   d'être une paire flecs `(Transform, Previous)`, pour qu'une signature de système dise laquelle des deux
   valeurs elle reçoit. `Transform` et `WorldTransform` ne sont pas deux formats de la même chose : le premier
   est la source (position, quaternion, échelle), le second le produit, qui peut porter un cisaillement qu'un
   `Transform` ne saurait pas représenter. Les deux réponses détaillées sont dans
   [`docs/QA.md`](../../docs/QA.md).
6. **`Transform` s'écrit, `WorldTransform` se lit** : le système réécrit `WorldTransform` à chaque tour, dans
   la phase `PostUpdate`, donc après la simulation et avant que le rendu ne relève les positions.
7. **Un système de gameplay va dans le pipeline de simulation** ([ADR-0016](../../docs/adr/0016-boucle-a-pas-fixe.md)) :
   `.kind<levain::scene::Simulation>()`. Son `delta_time` vaut alors toujours un pas — c'est ce qui rend son
   résultat reproductible. Un système déclaré dans une phase du pipeline par défaut tourne, lui, une fois par
   **image**, à cadence libre : c'est la place du rendu, pas celle du jeu.
   Le pipeline de simulation a **quatre phases, dans cet ordre** ([ADR-0026](../../docs/adr/0026-integration-de-jolt.md)) :
   `Simulation` (le gameplay), `Physics` (le pas de physique), `PostPhysics` (ce qui lit son résultat), et
   `EndOfStep`, la fin du pas, où le module `app` oublie les appuis que le pas vient de voir (ADR-0029). Dans
   une phase, l'ordre est celui des déclarations. Elles portent `SimulationPhase`, jamais `flecs::Phase` : le
   pipeline par défaut prend toute entité qui le porte, et rejouerait la simulation une fois par image.
8. **L'application avance le monde par `advanceWorld`**, jamais par `world.progress` : `progress` seul ne
   simule rien. Simulation arrêtée (`simulationPaused`, [ADR-0036](../../docs/adr/0036-mode-edition-vue-et-camera-de-l-editeur.md)) :
   aucun pas, `RenderAlpha` à 1, l'accumulateur ne reçoit pas le temps de l'image (`planFrame`), et la passe de rendu
   tourne encore. Un `Transform` écrit après elle se voit par `composeWorldTransforms`, qui relance le seul système
   des matrices monde, sans pas ni autre système.
9. **`scene` ignore l'existence de l'input** (SPECS §7) : la caméra libre lit un singleton `FpsInput`, que
   l'application remplit depuis `engine/input`. C'est ce qui permet de la piloter au clavier, à la manette ou
   par un test, sans que son code change d'une ligne.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/scene/components.hpp`](include/levain/scene/components.hpp) | `Transform` (position, rotation, échelle, **dans le repère du parent**), `Velocity`, `WorldTransform` (la matrice monde, calculée) |
| [`include/levain/scene/motion.hpp`](include/levain/scene/motion.hpp) | `applyVelocity` — la logique, sans flecs |
| [`include/levain/scene/transform.hpp`](include/levain/scene/transform.hpp) | `localMatrix`, `worldMatrix` (l'ordre du produit), `interpolate`, `nlerpShortestPath`, `worldPosition`, `worldRotation` (l'échelle retirée d'abord) — sans flecs non plus |
| [`include/levain/scene/fixed_step.hpp`](include/levain/scene/fixed_step.hpp) | `FixedStep`, `planSteps`, `pausedPlan` et `planFrame` — l'accumulateur et son plafond, et l'image à l'arrêt, testables sans monde |
| [`include/levain/scene/camera_control.hpp`](include/levain/scene/camera_control.hpp) | La caméra libre : `FpsController`, `FpsInput`, `applyFpsInput`, et les pièges nommés `normalizeOrZero`, `clampPitch` (des bornes inversées sont remises dans l'ordre), `horizontalBasisFrom` |
| [`include/levain/scene/scene.hpp`](include/levain/scene/scene.hpp) | `SceneModule` — `world.import<levain::scene::SceneModule>()` ; `advanceWorld` (avec l'arrêt) et `composeWorldTransforms` |
| [`include/levain/scene/reflection.hpp`](include/levain/scene/reflection.hpp) | La réflexion : `describe`, `describeAuthored`, `.range`, `Authored`, `rangeOf`, `stableKeyOf`, `componentOfKey`, puis `setComponentValue` et `sameValue` |

## Trois notions de flecs

- **Archetype** : flecs range ensemble les entités qui ont exactement les mêmes composants, dans des tableaux
  contigus par composant. Un système parcourt ces tableaux sans sauter d'un objet à l'autre en mémoire : 100 000
  entités mises à jour en **0,09 ms** (`levain_scene_bench`, Release).
- **Requête** : un système déclare ce qu'il lit (`const Velocity`) et ce qu'il écrit (`Transform`) ; flecs trouve
  les archetypes qui correspondent et les garde en cache.
- **Phase** : `world.progress()` exécute les systèmes phase par phase, dans l'ordre du pipeline par défaut
  (`OnLoad`, `PostLoad`, `PreUpdate`, `OnUpdate`, `OnValidate`, `PostUpdate`, `PreStore`, `OnStore`). La
  simulation va dans `OnUpdate`, les matrices monde dans `PostUpdate`.
- **Pipeline** : une liste de systèmes, exécutée par `progress` (celui par défaut) ou par `run_pipeline`
  (le nôtre, la simulation). Un système y entre par sa phase ; `cascade(DependsOn)` range les phases par
  profondeur, puis flecs exécute les systèmes d'une phase dans l'ordre de leur déclaration. Deux pipelines, c'est ce qui permet de rejouer la simulation N fois sans rejouer le rendu.
- **Hiérarchie** : le composant `flecs::Parent` contient l'entité parente, et flecs y ajoute la profondeur
  (`(ParentDepth, @n)`). `group_by(flecs::ParentDepth)` range les entités par niveau pour qu'un parent soit
  calculé avant ses enfants — c'est ce qui remplace une récursion. Coût mesuré : 100 000 entités sur 10
  niveaux en **1,43 ms** (`levain_scene_bench`, Release), sans dépendre de la forme de l'arbre.

Lectures : *Quickstart* de flecs (`docs/LECTURES.md`, D1), puis *Queries* (D2).

## L'explorer

En Debug, tout programme du moteur active l'addon REST de flecs sur `127.0.0.1:27750` (le module `app`,
ADR-0029) : deux programmes lancés ensemble en Debug se disputent ce port. Ouvrir
[flecs.dev/explorer](https://www.flecs.dev/explorer) dans un navigateur **sur la même machine** : il s'y connecte
seul, liste les entités (`grid` et ses enfants, dont `cube_50_50` au centre) et permet d'éditer leurs
composants. Donner une vitesse à un cube le fait partir : le rendu relit le monde à chaque frame. **Déplacer la
grille déplace les 10 000 cubes d'un bloc**, sans toucher à leur `Transform` : c'est la hiérarchie de M3.2.

L'explorer affiche des champs, pas des octets, grâce à la **réflexion** (addon meta) : chaque module décrit ses
composants (section suivante). Le JSON de l'explorer, et plus tard celui des scènes sauvegardées (phase 7), en
dépendent. Vérification : `tools/explorer-check.sh`.

## La réflexion des composants

Un composant devient visible de l'explorer, du JSON et de l'éditeur par **une ligne dans le module qui le
possède** ([ADR-0034](../../docs/adr/0034-reflexion-des-composants.md)) :

```cpp
scene::describeAuthored<RigidBody>(world).range(&RigidBody::mass, 0.001, 1.0e6); // donnée d'auteur
scene::describe<CharacterState>(world); // réécrit par le moteur : lecture seule, jamais sauvegardé
```

Les champs d'un composant sont lus dans sa struct, un agrégat de 32 champs au plus (ADR-0034). L'échelle de
liaisons structurées (`detail/field_ladder.inc`) est écrite par `tools/generate_field_ladder.py`, que le test
`scene.field-ladder` relance.

Un champ est un nombre, un booléen, une enum, un `flecs::entity`, une feuille glm décrite à la main dans
`SceneModule`, ou un agrégat imbriqué, décrit à son tour. Le reste est refusé à la compilation (`static_assert`), ou
à l'import : le journal nomme la struct et le champ, puis le programme s'arrête, en Release aussi. Les refus à
l'import ont leurs tests (`scene.reflection-refuses.*`), ceux à la compilation aussi
(`scene.reflection-refuses-compile.*`, `tests/reflection_compile_refusals.cpp`) : chaque `static_assert` de
`reflection.hpp`, et celui de flecs pour un pointeur, y a son cas, sauf `From != npos` de `fieldName`, qu'aucune
struct ne déclenche sous clang (il faudrait un `__PRETTY_FUNCTION__` sans « Pointer = », celui d'un autre
compilateur). Une classe de base, un tableau C et une faute de frappe de `range` n'ont pas de `static_assert` à
nous : l'erreur est celle de clang.

Une enum n'a que les constantes que flecs lit : de 0 à 126, et les puissances de deux
(`flecs/addons/cpp/utils/enum.hpp`). Une constante négative ou au-delà de 126 est écartée sans un mot, et l'import
ne refuse que l'enum qui n'en a aucune : une valeur qui n'a pas de nom vide le JSON de l'entité, et l'inspecteur
la montre « ? » sans pouvoir la choisir. Une enum de composant garde donc ses constantes entre 0 et 126.

**Ce que la scène décrit** : `Transform`, `Velocity` et `FpsController` en données d'auteur ; `WorldTransform`,
`PreviousTransform`, `RenderAlpha` et `FpsInput` en lecture seule ; à la main, les feuilles glm `vec2`, `vec3`,
`quat` et `mat4` (un tableau flecs de 16 flottants, que `sameValue` compare élément par élément). Pas
`SimulationPipeline` ni les phases : un identifiant de pipeline et des étiquettes. Le test
`tests/described_components_test.cpp` importe tous les modules sans fenêtre ni GPU et vérifie chaque composant
décrit du moteur ; sa ligne `MESSAGE` est l'inventaire, décrits et non décrits :
`./build/linux-debug/tests/levain_tests -tc='*inventaire*'`.

Un type imbriqué décrit à son tour n'est pas déclaré : la première description écrit les membres, `Authored` et
les bornes viennent de chaque déclaration, et deux déclarations contraires arrêtent l'import.

| Piège | Son nom |
|---|---|
| Le nom d'un champ, lu dans le `__PRETTY_FUNCTION__` d'une fonction instanciée sur son adresse ; un autre format (MSVC) | `fieldName`, refusé à la compilation par `isIdentifier` |
| L'objet `extern` jamais défini de Boost.PFR, que `-Wundefined-var-template` signale | `FakeObject`, une union jamais construite |
| Le décalage d'un champ calculé depuis un pointeur nul (la surcharge `member(nom, &T::champ)` de flecs) | `offsetInProbe`, une différence d'adresses dans un vrai `T` |
| Un champ sans réflexion (`std::optional`, `std::variant`, conteneur, feuille glm non décrite, enum sans constante comme `std::byte`), que flecs écarterait du JSON d'une ligne de journal | `hasReflection` et `refuseFieldWithoutReflection`, puis `exitOnRefusedDescription` |
| Un membre référence fait échouer toute accolade : zéro champ lu ; un `std::array` s'ouvre, mais son nom se lit `_M_elems[2]` | refusés à la compilation dans `describeFields` et par `IsStdArray` |
| Une borne absente se lit `[0, 0]` ; vide ou inversée, l'inspecteur cesserait de borner | `rangeOf` ; l'import refuse min ≥ max |
| Le remplissage diffère entre deux valeurs égales : `memcmp` les dirait différentes | `sameValue`, feuille par feuille |
| Le chemin d'un composant dépend du module qui l'enregistre en premier (`levain.scene.SceneModule.Transform`) | `stableKeyOf` et `componentOfKey` : le symbole, relu comme symbole |
| Une écriture par référence ne déclenche pas `OnSet` ; `ensure` sur un composant à hook `on_replace` est refusé | `setComponentValue`, un `set` d'une copie entière |

Ailleurs : Unreal lit les `UPROPERTY` par son Unreal Header Tool, un générateur de code ; Unity sérialise les champs
C# par la réflexion du langage, et `[Range]` borne l'inspecteur ; Godot les décrit à la main (`ADD_PROPERTY` dans
`_bind_methods`), comme nos feuilles glm (**documenté**, sources dans l'ADR-0034).

## Pièges connus (flecs 4.1.6)

| Piège | Parade |
|---|---|
| `b.set(a.get<T>())` : la référence rendue par `get` pointe dans la table de `a` ; si `b` rejoint la même table, l'ajout la réalloue, et `set` lit de la mémoire libérée. Invisible en Debug, trouvé par ASan (M4.2) | Copier la valeur dans une variable locale avant le `set` |
| Un composant avec un hook `on_replace` (le `MeshRef` d'`assets`) interdit `get_mut`, `ensure`, `emplace`… et `entity.clone()`, qui passe par `get_mut` : assertion de flecs | Poser et copier par `set` ; partager par un prefab (`IsA`) plutôt que cloner |
| `member<T>(nom, 1, décalage)` fait un **tableau** d'un élément, sérialisé `"x":[2.5]` | `ScalarMember` (0) : c'est 0 qui veut dire scalaire |
| La surcharge `member(nom, &Type::champ)` calcule son décalage en déréférençant un pointeur nul | `offsetof` pour une description à la main, `offsetInProbe` pour la réflexion |
| `EcsRest::ipaddr` : flecs en prend la propriété et le **libère** à la destruction du monde | `ecs_os_strdup` ; une chaîne statique finissait en « double free » |
| `group_by` **ne trie pas** les groupes : il les parcourt dans l'ordre inverse de leur création, donc un petit-enfant avant son parent | ajouter `query_flags(EcsQueryGroupByOrdered)` ; un test construit une hiérarchie dans le désordre |
| Une requête `(ChildOf, parent)` **combinée à un composant** ne se résout pas table par table quand la hiérarchie est dans `flecs::Parent` : 212 µs pour 10 000 cubes, contre 8 | filtrer autrement (un tag, comme `Cube` dans le sandbox), ou interroger `flecs::Parent` |
| `entity(...).get<T>()` dans la boucle d'un système, ou `it.world()`, coûtent 0,5 ms par 100 000 entités | capturer le monde et l'identifiant du composant une fois, puis `ecs_get_id` |
| Une requête dont le **singleton manque** ne correspond à rien, et son système se tait au lieu d'échouer | poser le singleton à l'import du module (`RenderAlpha`), et un test qui vérifie qu'un `progress` seul compose quand même les matrices |
| Les *tick sources* (`interval(1/60)`) **ne rattrapent pas** le retard : à 30 images/s, le système tourne 30 fois par seconde, pas 60 | l'accumulateur de `fixed_step.hpp` et un pipeline exécuté N fois ([ADR-0016](../../docs/adr/0016-boucle-a-pas-fixe.md)) |
| Sans `ipaddr`, le serveur REST écoute sur **toutes les interfaces**, et son API sait supprimer des entités et exécuter des scripts | toujours `127.0.0.1` ; `tools/explorer-check.sh` échoue sinon |

## Ce que coûte un tour (Release, 100 000 entités)

| Mesure | Valeur |
|---|---|
| Un pas de simulation (`SavePreviousTransform` + `ApplyVelocity`) | 0,16 ms |
| Une passe de rendu, toutes les entités interpolées | 2,09 ms |
| Les matrices monde seules, 10 niveaux de hiérarchie | 1,42 ms |

`levain_scene_bench`, machine de référence. L'interpolation ne se paie que sur ce que la simulation déplace :
sur les quelques centaines d'acteurs en mouvement d'un monde ouvert, elle coûte quelques microsecondes.

## Équivalents ailleurs

| Moteur | Où | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | Mass Entity ; *tick groups* | Un ECS à archetypes à côté des Actors ; les phases s'appellent groupes de tick (`TG_PrePhysics`, `TG_PostPhysics`…) (**documenté** : documentation d'Epic). |
| **Unity** | Entities (DOTS) ; *system groups* | Un ECS à archetypes rangés en *chunks* ; les systèmes dans des groupes (`Initialization`, `Simulation`, `Presentation`). La hiérarchie y est aussi un composant de l'enfant (`Parent`), et `LocalToWorldSystem` compose les matrices comme le nôtre (**documenté** : manuel du package Entities). |
| **Godot** | Arbre de scène, `Node` | Pas d'ECS : des nœuds hiérarchiques, mis à jour par `_process` (par image) et `_physics_process` (à pas fixe, 60 Hz) — nos deux pipelines. L'interpolation de la physique y est optionnelle et désactivée par défaut (**documenté** : docs officielles). |
