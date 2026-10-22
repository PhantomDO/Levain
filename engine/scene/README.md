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
| [`include/levain/scene/scene.hpp`](include/levain/scene/scene.hpp) | `SceneModule` — `world.import<levain::scene::SceneModule>()` ; `advanceWorld` (avec l'arrêt, et le pas unique d'« Image suivante » : `planFrame`, `singleStepPlan`) et `composeWorldTransforms` |
| [`include/levain/scene/reflection.hpp`](include/levain/scene/reflection.hpp) | La réflexion : `describe`, `describeAuthored`, `.range`, `Authored`, `rangeOf`, `stableKeyOf`, `componentOfKey`, puis `setComponentValue` et `sameValue` |
| [`include/levain/scene/tombstone.hpp`](include/levain/scene/tombstone.hpp) | La pierre tombale (ADR-0038) : `Tombstone`, `removeKeepingIndex`, `checkTombstonesHold`, `reviveFromTombstones`, `releaseTombstones` |
| [`include/levain/scene/entity_name.hpp`](include/levain/scene/entity_name.hpp) | Les noms d'entités : `refusalOfName` et `reasonOf`, `isValidEntityName` |
| [`include/levain/scene/snapshot.hpp`](include/levain/scene/snapshot.hpp) | L'instantané de la scène pour Play/Stop (ADR-0037) : `isSceneEntity`, `isEngineInternal`, `Transient`, `takeSnapshot`, `differencesFrom`, `compareWithSnapshot` et sa `Comparison` (le compte des vraies différences, sans plafond, et les enfants internes perdus à part), `entityIndexOf` et `byEntityIndex` (l’ordre par index, partagé avec la physique et la Hiérarchie), `createIdRanges`, `useEditIds`/`usePlayIds`, `bornInPlay`, `restoreSnapshot` et son `RestoreReport` |

## L'instantané de la scène

Play garde la scène, Stop la rend **en place**, avec les mêmes identifiants ([ADR-0037](../../docs/adr/0037-play-stop-restauration-en-place.md)).
`snapshot.hpp`, sans éditeur ni GPU :

- **Une entité de la scène** (`isSceneEntity`) est ce que la Hiérarchie pourrait montrer : vivante, ni flecs lui-même,
  ni module, système, observateur, requête, composant ou prefab (`isEngineInternal`), ni rangée sous un module.
  La scène n'emploie que `flecs::Parent` (ADR-0015) : une entité sous `ChildOf` n'en est pas une. On lit la **table**
  (`(ChildOf, *)`), car un enfant de `flecs::Parent` répond oui à `entity.has(ChildOf, *)`.
- **`takeSnapshot`** copie, pour chaque entité de la scène, son identifiant avec sa génération, son type complet, la
  valeur de chaque composant **par les crochets du type** (`ecs_value_copy`), son nom, son parent, l'ordre de ses
  enfants et ses enfants internes (l'observateur d'un `onEnter`). Play **ne part pas**, en nommant l'entité et la
  cause, pour : un composant qui ne se copie pas (`COPY_ILLEGAL`) ou sans constructeur par défaut (`CTOR_ILLEGAL`) ;
  une instance de prefab `(IsA, *)` ; deux frères du même nom (`lookup` n'en rend qu'un) ; une entité à `Transform`
  rangée par `ChildOf` sous autre chose qu'un module, que la Hiérarchie montre mais que l'instantané ne garde pas.
  Un type à copie supprimée mais à déplacement défini par défaut compte pour trivialement copiable pour flecs : il est
  copié par `memcpy`, sans risque puisque son destructeur est trivial ; ce qui pend, ce sont les handles, d'où `Transient`.
- **`Transient`**, une étiquette sur un composant comme `Authored`, posée à l'import de `SceneModule` : un module
  marque ce qui n'est pas la scène (les handles de Jolt). Jamais copié, jamais refusé, retiré des entités à Stop.
- **`differencesFrom`** : une ligne par différence (identifiants et générations, entités en trop ou manquantes, noms,
  `lookup(chemin) == id`, parents, types, valeurs, ordre des enfants, enfant interne manquant, entité née en jeu sous
  une entité de la scène par `ChildOf`), les vingt premières puis le compte des autres. Vide si la scène est
  identique : c'est le critère de Stop.
- **Les plages d'identifiants** (`createIdRanges`, appelée par `createApp` juste après le monde) : l'édition prend
  [1, 2^24 − 1], le jeu au-dessus (`useEditIds`, `usePlayIds`). Chaque plage a sa liste d'index à réutiliser : l'index
  d'une caisse détruite en jeu ne peut pas être repris par un observateur ou une requête créés ensuite, qui restent
  après Stop. *Né en jeu* (`bornInPlay`) : une entité de la scène d'index au moins 2^24.
- **`restoreSnapshot`** rend la scène en place (ADR-0037, décision 3). Refusée en mode différé et si un index est pris
  par une autre génération, le monde intact, dans tous les builds. Puis : 1. détruire les entités nées en jeu ;
  2. revenir à la plage de l'édition, **avant** le réveil ; 3. réveiller les entités détruites avec leur génération
  (`ecs_make_alive`) ; 4. rendre les parents (par profondeur croissante, ce qui évite tout cycle), les composants
  (ceux de trop retirés, `Transient` compris), les valeurs (réécrites si elles ont changé, toujours si on ne sait pas
  les comparer) et les noms (tous reposés, même inchangés : l'index des noms range un nom sous son parent au moment
  où il est posé) ; 5. rendre l'ordre des enfants, la liste vérifiée en n log n ; 6. vérifier par `differencesFrom`.
  Un volume détruit en jeu revient sans l'observateur rangé dessous : `RestoreReport` le nomme. flecs ne vérifie qu'en
  Debug (`ecs_check`) un réveil sur un index vivant, une plage créée après une destruction et la taille de la liste
  d'enfants : Levain les vérifie lui-même.

| Piège | Son nom |
|---|---|
| Une entité qui perd son `flecs::Parent` (un enfant devenu racine) sort de l'index des noms sans être rangée à la racine : un parent et son enfant inversés puis remis en place perdent leur `lookup` | `restoreNames` (tous reposés après les parents) et le `lookup` de `differencesFrom` |
| Un frère prend le nom d'un autre, puis est détruit : il emporte l'entrée du nom, et l'autre ne se trouve plus par `lookup` | les mêmes |
| `ecs_get_ordered_children` s'arrête sur une assertion pour un parent sans enfant `flecs::Parent` (vérifié sur la 4.1.6) | `orderedChildrenOf` ne l'appelle qu'avec un enfant `Parent` |
| Le nom et `flecs::Parent` sont tenus par flecs : les recopier dans le type le casserait | `isKeptByFlecs`, `sceneTypeOf` |
| Une valeur qu'on ne sait pas comparer (ni décrite, ni crochet `cmp`) ne se voit pas dans `differencesFrom`, et Stop la réécrit toujours | `isComparable` |

Un instantané garde le monde pour libérer ses valeurs : il ne lui survit pas.

Ailleurs : Unity ne relit que « le contenu modifié de la scène » au retour de Play quand il ne la recharge pas
(**documenté**) ; Unreal joue dans une copie du monde (**supposé**) ; Godot dans un autre processus (**documenté**).

## La pierre tombale

Supprimer une entité pour de bon, et pouvoir la rendre avec **le même identifiant**, génération comprise
([ADR-0038](../../docs/adr/0038-annulation-et-scenes.md), décision 3). `tombstone.hpp`, sans éditeur ni GPU :

- **Le problème.** Dès qu'une entité est détruite, flecs redonne son index à la **prochaine** création, et pas seulement
  à celles de l'éditeur : une requête, un observateur suffisent. `ecs_make_alive` ne peut plus alors rendre l'ancien
  identifiant. L'ordre « dernier fait, premier défait » ne protège pas contre une entité de l'infrastructure.
- **La parade.** `removeKeepingIndex` détruit l'entité et son sous-arbre, puis réveille sur chaque index libéré une
  **pierre** (`Tombstone`, une étiquette) à la génération suivante : l'index est vivant, personne ne le reprend. Les
  `Tombstones` rendues appartiennent à la commande. `reviveFromTombstones` détruit les pierres et réveille les entités
  avec leur identifiant d'avant ; elles reviennent **vides** (types, valeurs, noms et parents sont l'affaire de
  l'enregistrement de la commande, morceau A2). `releaseTombstones` rend les index quand la commande sort de la pile.
- **Invariants, vérifiés dans tous les builds** (un refus nommé, le monde intact) :
  - avant chaque réveil, chaque index réservé est vivant, à la génération de sa pierre, et porte `Tombstone`
    (`checkTombstonesHold`). En Release `ecs_make_alive` écraserait sans rien dire l'entité qui a pris la place (elle
    meurt), en Debug flecs s'arrête ;
  - tout index est dans la plage d'identifiants **active** (`ecs_entity_range_get`) : supprimer en jeu une entité de
    l'édition rangerait l'index dans la liste de la plage du jeu, à la génération de la pierre, et Stop s'arrêterait
    (Debug) ou donnerait l'identifiant de la pierre à une autre entité (Release). Une suppression en jeu ne passe pas par
    la pile (ADR-0038, 9) ;
  - le mode différé est refusé, comme une entité morte, une pierre, ou ce qui n'est pas de la scène (`isSceneEntity` : un
    composant, un module) ;
  - si un observateur `OnRemove` crée une entité pendant la destruction et prend un index libéré (un `onExit` de
    gameplay), aucune pierre n'est posée et l'erreur le dit : la suppression est faite, l'annulation impossible.
- **Ce qui ne revient pas** : les paires `(R, E)` d'une autre entité vers l'entité détruite, tout ce que portait l'entité
  (composants, nom, `flecs::Parent`), et les enfants rangés par `ChildOf` (l'observateur d'un `onEnter`). Ces derniers ne
  sont pas réservés (ils reviendraient en entités vides que `isSceneEntity` compterait) : `Tombstones::lost` les
  rapporte, nom lu avant la destruction, pour l'avertissement de la console. Un champ d'entité d'un composant d'auteur
  garde l'identifiant mort, et redevient valide au réveil.
- **Contrat pour la pile (morceau A4).** Supprimer, annuler, supprimer encore donne les **mêmes** pierres (même index, même
  génération). Libérer les pierres d'une commande ancienne après qu'une plus récente a supprimé la même entité détruit
  les pierres de la récente, dont l'annulation est alors refusée. La pile vide donc ses commandes refaisables, en les
  libérant, **avant** d'exécuter une nouvelle commande.
- **`Tombstone` est enregistré avant la destruction** : passés les 256 identifiants bas que flecs réserve aux composants,
  un composant enregistré après une destruction prend l'index libéré et devient sa propre pierre.
- **Pas encore faits** (morceau A2) : `isSceneEntity` n'écarte pas encore les pierres, et le sous-arbre n'est pas lu.

Les comportements de flecs dont tout cela dépend sont **épinglés** dans `tests/flecs_tombstone_pins_test.cpp`, en Debug
et en Release (a : l'index recyclé sans pierre ; b : rien ne le prend avec ; c : réveil de la génération d'avant ;
d : un sous-arbre ; e : une création annulée puis refaite ; f : « Cube 2 » se retrouve par `lookup` ; puis ce qui ne
revient pas, et en Release seulement l'écrasement silencieux). Si flecs change, c'est là que ça rougit.

Ailleurs (sources numérotées de l'[ADR-0038](../../docs/adr/0038-annulation-et-scenes.md)) : Unity annule
`Undo.DestroyObjectImmediate` en recréant l'objet (**documenté** [1]), depuis un état sérialisé (**supposé**) ; Unreal
range l'acteur supprimé dans la transaction (`FScopedTransaction`, **documenté** [8] pour la transaction, **supposé** pour
le sort de l'acteur) ; Godot garde les objets que l'action d'annulation référence (`UndoRedo`, **documenté** [4]). Ce
qu'aucun des trois n'a est notre problème d'index : c'est **supposé**, leurs identifiants n'étant pas recyclés comme ceux de
flecs, mais rien n'est lu là-dessus dans leurs sources.

## Les noms

`entity_name.hpp` : un nom est ce que `lookup` et le fichier de scène retrouvent (ADR-0038, décision 6). `refusalOfName`
dit pourquoi un nom est refusé, `reasonOf` en donne la phrase pour le champ de renommage :

| Refusé | Cause |
|---|---|
| vide | `lookup("")` ne trouve rien |
| contient « . » | le chemin « a.b » éclate au rechargement (#254) |
| contient « :: » | le séparateur de chemin de flecs |
| « # » suivi de chiffres | `lookup` rend l'entité n° 12 de flecs, pas la nôtre |
| commence par « # », **à la racine** | flecs lit le début du chemin comme un identifiant : « #abc » ne se retrouve pas (essayé sur la 4.1.6) |
| « _ », « * », « $ » seuls, **à la racine** | `EcsAny`, `Wildcard`, une variable ; sous un parent, `lookup` les retrouve |

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
| `ecs_get_ordered_children` s'arrête sur une assertion pour un parent sans enfant `flecs::Parent` (un volume dont les seuls enfants sont ses observateurs `ChildOf`) | `orderedChildrenOf` ne l'appelle qu'avec un enfant `Parent` ([GOTCHA](../../.agents/skills/build/GOTCHA.md)) |
| Une struct à copie supprimée et déplacement `= default` compte pour trivialement copiable : pas de `COPY_ILLEGAL`, `ecs_value_copy` la copie par `memcpy` | un type de test pour un refus porte un `std::unique_ptr` ; ce qui pend, ce sont les handles, d'où `Transient` |

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
