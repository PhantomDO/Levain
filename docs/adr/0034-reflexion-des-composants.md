# ADR-0034 — La réflexion des composants : une déclaration, les champs lus dans la struct, rangés dans flecs

- **Statut** : accepté le 2026-10-07, sur les réponses de Donnovan au sondage du jour (1B, le lecteur de
  champs ; 5B, les refus de la physique ; 6B, contre la recommandation : les panneaux dans la bibliothèque
  éditeur) ; écrit par un workflow (études, trois conceptions, un juge, relectures adverses), puis condensé
- **Modifie** : [ADR-0026](0026-integration-de-jolt.md), § 4 de sa décision (« une assertion en Debug » sur un
  corps refusé)
- **Date** : 2026-10-07
- **Milestone** : M7.2 (#252)

## Contexte

**La réflexion**, c'est ce que le moteur sait, à l'exécution, des champs d'un composant : nom, type, place dans la
struct. Avec elle, un outil générique traite tous les composants sans en connaître aucun : l'inspecteur dessine un
widget par champ, la sauvegarde écrit chaque champ sous son nom, l'annulation garde un avant et un après. Le C++ ne
la donne pas avant C++26 : Unreal la génère (`UPROPERTY`), Unity la tient de C#, Godot la remplit à la main
(`ClassDB`). **flecs la range dans l'ECS** (l'addon meta) : un composant y est une entité, qui porte sa description
(`EcsStruct` : nom, type, décalage et bornes de chaque membre), que lisent l'explorer, le JSON et le curseur.

**Ce qui existe.** `engine/scene/src/scene.cpp` décrit à la main, champ par champ, sept types (`glm::vec3`,
`glm::quat`, `Transform`, `Velocity`, `WorldTransform`, `PreviousTransform`, `RenderAlpha`). Le moteur, ses
plugins, le sandbox et *Rando* comptent 42 données (23 composants d'entités, 10 singletons, 4 étiquettes ou
relations, 5 phases), dont 5 décrites ; les 28 autres qui portent des champs sortent `null` en JSON, et l'explorer
n'en a que le nom.

**Ce qu'il faut.** M7.2 : « un nouveau composant devient éditable en une seule déclaration », avec une hiérarchie
et un inspecteur ; M7.3 (scène en JSON, annulation), M7.4 (gizmos) et M7.5 (Play/Stop) reprennent la description.
L'issue #252 demande comment un composant devient visible de l'éditeur, ce qu'on génère, ce que voit un plugin.

**Ce que les études ont trouvé** (flecs 4.1.6 ; clang 23.1.3 et GCC 15.2 sous ASan et UBSan ; em++ 6.0.11). Sans
script versionné, leurs chiffres sont **indicatifs** (règle n°6) ; la PR du moteur versionne ceux qu'elle garde,
l'inventaire des composants compris (un test qui liste ce qui est décrit).

1. **Le nom d'un composant dépend de qui l'enregistre en premier** (`levain.scene.SceneModule.Transform` par son
   module, son nom C++ sinon) : `WalkInput` change de chemin entre le sandbox et *Rando*, or c'est la clé du JSON
   de flecs. Le **symbole** de flecs, lui, reste le nom C++ quel que soit l'ordre (vérifié).
2. **flecs se tait sur une description incomplète** : un champ oublié en fin de struct rend le type « partiel »,
   un champ sans réflexion est écarté d'une ligne de journal, une seconde description réinterprète les bits.
3. **Une écriture par référence ne déclenche rien**, or toutes les réactions du moteur sont des observateurs
   `OnSet` : un corps endormi déplacé ainsi diverge en silence, et `ensure` sur `MeshRef` (hook `on_replace`),
   refusé en Debug, fausse en silence le compte de références des assets en Release.
4. **Des types de champs n'ont pas de réflexion** (`glm::vec2`, `std::optional`, le `std::variant` et les
   `std::shared_ptr` de `Collider`), et un `flecs::entity_t` s'écrit comme un nombre, perdu au rechargement.
5. **Une valeur refusée, tapée dans l'inspecteur, arrête le Debug** : les refus de la physique (masse nulle,
   pente de 90°, échelle sur un corps) finissent sur `LEVAIN_ASSERT(false)`, dont le `__builtin_debugtrap` tue le
   processus sans débogueur (SIGTRAP, vérifié ; assert.hpp dit l'inverse). Ils ne se testent qu'en Release.

## Options envisagées

| 1. Comment les champs d'un composant arrivent dans flecs | Pour | Contre |
|---|---|---|
| A. À la main, champ par champ : `.member("speed", &T::speed)…` | L'API de flecs telle quelle, rien de caché ; tout type | Chaque nom écrit deux fois ; un champ ajouté demande une ligne, un oubli passe en silence (un contrôle par la taille rate un petit champ logé dans un trou d'alignement) ; la dernière description gagne |
| **B. Lus dans la struct, par un en-tête maison (≈ 220 lignes, plus une échelle écrite par un script)** | Une ligne par composant, rien sur la struct ; un champ ajouté apparaît seul, un oubli est impossible ; aucune dépendance | Une astuce de compilateur à tenir (`__PRETTY_FUNCTION__`), MSVC non essayé ; des agrégats seulement |
| C. Lus dans la struct, par Boost.PFR 1.91 (BSL-1.0) | La déclaration de B, la lecture maintenue par Boost | Une dépendance publique, donc aussi dans le `vcpkg.json` de *Rando* ; en `-isystem`, elle se sert en silence des paquets de liaisons de C++26 en mode C++23, ce que `-pedantic-errors` est là pour attraper : il faut `BOOST_PFR_USE_CPP26=0` ; 0,4 à 0,9 s d'include par fichier |
| D. Une macro qui liste les champs (Boost.Describe, visit_struct, X-macro) | Marche sur les non-agrégats | Les noms écrits deux fois, l'oubli silencieux ; l'X-macro rend la struct illisible |
| E. `ECS_STRUCT`, la macro de flecs | Une déclaration, déjà installée | Échoue **à l'exécution** sur une valeur par défaut et sur `glm::vec3` : tous nos composants en ont |
| F. Génération de code, comme l'Unreal Header Tool | Une annotation sur le champ, les membres privés, les métadonnées à côté | Un générateur (l'AST de clang : 0,39 s par en-tête) branché dans CMake, dans *Rando* et dans le job web ; du code généré qu'on ne lit pas, ce que l'ADR-0018 a déjà écarté pour les imports des plugins |
| G. La réflexion de C++26 (P2996) | La solution du langage, sans astuce | **Indisponible** (ci-dessous) |

**C++26, essayé.** clang 23.1.3 ignore `-freflection` ; forcé, `^^Velocity` donne « unknown or unimplemented
reflectable entity », de même sous le clang 24 d'emsdk, et la page de statut de clang marque P2996 « No » [11].
GCC 15.2 refuse l'option ; GCC 16.1 l'a, mais Levain ne compile qu'en clang. **B plutôt que A** : le critère de
M7.2 compte les déclarations, et A en demande une par champ, sans pouvoir détecter l'oubli. **B plutôt que C** :
la technique de PFR, en ≈ 220 lignes à nous, plus l'échelle, sans dépendance ni define.

| 2. Ce qui sépare la donnée d'auteur du reste | Pour | Contre |
|---|---|---|
| **A. Une étiquette `Authored` sur l'entité du composant, posée par sa déclaration** | Un oubli rend le composant en lecture seule et non sauvegardé : ça se voit ; une seule réponse pour l'inspecteur, la sauvegarde et Play/Stop | Pas de lecture seule par champ : une struct qui mêle les deux se coupe |
| B. Une étiquette `Derived` sur ce que le moteur réécrit | Moins d'étiquettes à poser | Un oubli sauvegarde `PreviousTransform` en silence |
| C. Des drapeaux par champ (masqué, lecture seule) | Le plus fin, comme Unreal | Une décision par champ à tenir à jour, là où couper une struct règle la question une fois (`ThirdPersonCamera` : une dizaine de lignes de *Rando*) ; flecs le permet type par type, au prix d'une entité par membre |

| 3. La clé d'un composant dans une sauvegarde | Pour | Contre |
|---|---|---|
| **A. Le symbole de flecs (`levain.physics.RigidBody`)** | Le nom C++, quels que soient l'ordre d'enregistrement et le programme ; aucun enregistrement à déplacer | Le JSON d'entité de flecs garde le chemin : M7.3 écrit ses clés lui-même, ce qu'il doit faire de toute façon |
| B. Décrire à la racine, pour que le chemin soit le nom C++ | Le JSON de flecs tel quel | Un garde contre le module qui enregistre avant la description ; *Rando* déplace ses `world.component<T>()` |
| C. Le chemin tel quel | Rien à faire | Il diffère entre le sandbox et *Rando*, et selon l'ordre des imports |

| 4. Comment l'éditeur écrit une valeur | Pour | Contre |
|---|---|---|
| **A. Un `set` d'une copie entière (`ecs_set_id`)** | `OnSet` comme pour le code du jeu : téléportation, reconstruction, interpolation remise, compte de références de `MeshRef` | Une copie de quelques dizaines d'octets par écriture |
| B. Écrire par pointeur (`ensure`), puis `modified()` | Pas de copie | `ensure` sur `MeshRef` fausse le compte de références en Release ; un `modified()` oublié laisse un corps diverger en silence |

| 5. Les refus de la physique, quand l'inspecteur tape une valeur refusée | Pour | Contre |
|---|---|---|
| A. Garder l'assertion, et que l'inspecteur interdise plus (une borne par champ, un `whyNot…` avant chaque `set`) | L'ADR-0026 ne bouge pas | Chaque règle de la physique écrite deux fois, dans l'inspecteur et dans physics ; une règle oubliée tue le Debug |
| **B. Le seul journal d'erreurs, le corps ou le personnage restant refusé** | Une valeur tapée se lit au journal et se corrige ; les refus se testent en Debug | Modifie l'ADR-0026 ; un refus ne s'arrête plus dans le débogueur |
| C. Un `Result` rendu par la création | Ce que demande assert.hpp pour un échec qui n'est pas un bug | Les corps se créent dans les systèmes du module, qui n'ont personne à qui le rendre : le refus finirait au journal |

| 6. La place des panneaux de hiérarchie et d'inspecteur | Pour | Contre |
|---|---|---|
| A. Dans `app`, derrière F1, dans tous les builds, comme ceux de M7.1 | *Rando* les a en jouant, pour régler son game feel ; la page web aussi, où l'explorer n'existe pas ; aucune cible de plus | Le jeu livré les embarque, jusqu'au jour où il devra s'en passer (sans date) |
| **B. La bibliothèque éditeur de l'ADR-0018, dès maintenant** | La séparation runtime et éditeur de l'ADR-0018 dès le premier panneau : le jeu livré et la page web n'embarquent aucun code d'éditeur ; Play/Stop (M7.5) et les outils de terrain (M7.6) trouvent leur application | Une bibliothèque et deux exécutables de plus (un pour le sandbox, un pour *Rando*) avant l'éditeur complet (M7.5) ; régler le game feel passe par l'exécutable éditeur ; pas d'inspecteur dans le navigateur |
| C. En Debug seulement | Rien dans le jeu livré | Ni la page web (Release) ni `linux-release` ; le game feel se règle à la cadence du Debug |

## Décision

**1B, 2A, 3A, 4A, 5B, 6B.** Un composant devient visible de l'éditeur par **une ligne dans le module qui le
possède**, qui lit ses champs dans la struct et les écrit dans la réflexion de flecs. Cette ligne dit aussi s'il
est donnée d'auteur. L'éditeur n'écrit que par `set`. Un refus de la physique s'écrit au journal sans arrêter le
Debug. Les panneaux vivent dans la bibliothèque éditeur : le jeu livré garde la réflexion, pas l'éditeur.

### La déclaration

```cpp
struct TraversalStart { Mode mode = Mode::Walk; float stamina = 1.0f; }; // imaginé pour Rando, sans macro
scene::describeAuthored<TraversalStart>(world).range(&TraversalStart::stamina, 0.0, 1.0); // LA déclaration

void describePhysicsComponents(flecs::world& world) // dans physics.cpp : ses en-têtes ne voient pas flecs
{
    scene::describeAuthored<RigidBody>(world) // Motion, une enum : une liste déroulante sans rien déclarer
        .range(&RigidBody::mass, 0.001, 1.0e6)
        .range(&RigidBody::friction, 0.0, 1.0);
    scene::describeAuthored<CharacterController>(world);
    scene::describe<Capsule>(world).range(&Capsule::radius, 0.01, 10.0); // un type imbriqué porte ses bornes
    scene::describe<CharacterState>(world); // posé par le module à chaque pas : lecture seule
}
```

- **`describeAuthored<T>`** : la donnée d'auteur, que l'inspecteur édite, que M7.3 sauvegarde, que M7.5 restaure ;
- **`describe<T>`** : ce que le moteur ou le jeu réécrit (`WorldTransform`, une entrée reposée à chaque image), en
  lecture seule, jamais sauvegardé. Un type imbriqué à bornes (`Capsule`) passe aussi par `describe` : seule
  l'étiquette du composant compte, et `Capsule` s'édite et se sauvegarde avec `CharacterController` ;
- **`.range(&T::champ, min, max)`** : facultatif, sur un nombre seulement ; une faute de frappe ne compile pas ;
- **une fonction libre par module**, appelée en une ligne par son constructeur (ADR-0011) et par tout module qui
  réutilise ses types (`TraversalModule`, sans `WalkModule` depuis l'ADR-0031, appelle `describeWalkComponents`) ;
- **un champ ajouté n'appelle aucune autre ligne**. Un non-agrégat se décrit à la main, comme aujourd'hui les
  feuilles de glm dans `SceneModule`, où `vec2` s'ajoute et `mat4` devient `array<float>(16)` (le même JSON).

### Ce que la réflexion lit, et ce qu'elle refuse

`levain/scene/reflection.hpp` (dans `levain_scene`) lit **le nombre de champs**, le plus grand N tel que
`T{x₁, …, x_N}` compile, puis **les champs** par une échelle de liaisons structurées (`auto& [a, b, c] = objet`)
de 1 à 32 champs, C++23 n'ayant pas les paquets de liaisons : 32 lignes hors clang-format (190 une fois
formatées), écrites par un script versionné et relues ; rien n'est généré à la compilation. **Les noms** viennent
du `__PRETTY_FUNCTION__` d'une fonction instanciée sur l'adresse du champ (la technique de Boost.PFR, et celle de
flecs pour les enums) ; **les décalages**, d'un vrai `T`, pas d'un pointeur nul comme la surcharge de flecs
(formellement indéfini, même si UBSan ne l'a vu ni sous clang 23 ni sous GCC 15 : le commentaire de scene.cpp, qui
prédit une alerte UBSan, se corrige). Un agrégat imbriqué se décrit à son tour ; nombres, booléens, enums,
feuilles glm décrites et `flecs::entity` passent. Le reste **échoue bruyamment** (règle n°7) :

| Cas | Quand | Ce qu'on lit |
|---|---|---|
| Un non-agrégat (constructeur, membre privé), une classe de base, un tableau C (`float w[4]` compte pour 4 champs) | Compilation | Un `static_assert` qui renvoie à la description à la main ; pour une base ou un tableau, l'erreur des liaisons (un type glm à la place du tableau ; `std::array` n'est pas essayé) |
| Plus de 32 champs (le plus gros composant en a 17), un `__PRETTY_FUNCTION__` inconnu (MSVC), un pointeur, une borne sur un non-nombre, une donnée d'auteur qui ne se copie pas octet par octet | Compilation | Un `static_assert` (celui de flecs pour un pointeur) |
| `std::optional`, `std::variant`, un conteneur, une feuille glm non décrite ; une borne vide ou inversée ; deux déclarations qui se contredisent | Import du module | Le journal nomme le type et le champ, puis le programme s'arrête (en Release aussi pour un champ sans réflexion) |
| Une feuille glm redécrite à la main, une référence d'entité en `flecs::entity_t`, une valeur d'enum hors de 0 à 126 | Relecture (trois règles du GOTCHA du skill build) | Rien : flecs garde la dernière description, écrit l'entier comme un nombre, ne lit pas le nom de la valeur (et une seule valeur invalide vide tout le `to_json`) |

**Décrire deux fois.** La première description écrit les membres, que rien ne peut contredire ; `Authored` et les
bornes viennent de chaque déclaration, même sur un type déjà décrit par récursion (qui n'en est pas une). Deux
déclarations contraires arrêtent l'import ; l'ordre des imports ne change donc rien.

### Où vivent les métadonnées de l'éditeur

Dans **des données flecs sur l'entité du composant**, sans ImGui (ADR-0032) : **les bornes**, dans
`ecs_member_t.range` (flecs ne borne rien, l'inspecteur les impose), et **`Authored`**. **Rien d'autre en M7.2** :
pas d'unité, nos noms la portent (`maxSlopeDegrees`), et l'addon units coûte de l'ordre de 1 ms (Release) à 4 ms
(Debug) à l'import, et écarte d'une ligne de journal un membre dont l'unité n'est pas importée ; ni nom
d'affichage ni infobulle, l'addon doc voulant une entité par membre ; pas de lecture seule par champ. Les règles
hors bornes (une échelle autre que 1 sur un corps) restent aux `whyNot…`.

### Ce que voit un plugin

Un plugin, du moteur ou de *Rando*, voit `describe`, `describeAuthored`, `.range` et `Authored`, dans
`<levain/scene/reflection.hpp>` (il lie déjà `levain_scene`) :

- **il écrit** une ligne par composant dans la fonction de description de son module, que le constructeur, son
  seul point d'entrée (ADR-0018), appelle : ni inscription statique, ni code généré ;
- **il obtient**, sans une ligne de plus, sa section de l'inspecteur, sa place dans la scène sous son nom C++
  (`rando.traversal.TraversalRules`), l'annulation, les gizmos et Play/Stop, **sans que la réflexion demande à
  sa cible runtime de lier l'éditeur ou ImGui** : elle est dans `levain_scene` ;
- **il ne peut pas** contredire un type du moteur (l'import s'arrête), écrire autrement que par `set`, ni ajouter
  un dessinateur de champ : ceux de M7.2 (quaternion en angles, asset et entité par leur nom) sont dans l'éditeur,
  celui d'un plugin ira dans sa cible éditeur (ADR-0018) ; restent à sa charge les trois règles de relecture.

### Le chemin d'une modification

Une seule fonction écrit, pour l'inspecteur, l'annulation, le chargement, les gizmos et Stop :
`setComponentValue(world, entity, component, const void* value)`, un `set` qui déclenche `OnSet` comme le jeu,
dans `levain_scene` avec `reflection.hpp` (comme `sameValue`) : le chargeur de M7.3 tourne dans le jeu livré.

- **Copier, puis `set`** : les widgets éditent une copie, jamais la table, où un pointeur gardé pendrait ; `OnSet`
  part une fois par écriture, `on_replace` compris (vérifié), sans `modified()` ;
- **entre `defer_begin` et `defer_end`** : les observateurs passent après le parcours des panneaux ;
- **l'autorité de la physique reste au moteur** : un `set` du `Transform` d'un corps ou d'un personnage le
  téléporte (ADR-0026, ADR-0028) et remet à zéro la vitesse d'un personnage ; ce que le pas réécrit (rotation
  d'une caméra, rotation en Y d'un personnage, caméra libre) s'éditera à l'arrêt, en M7.5 ;
- **un geste, une commande** (M7.3) : l'avant au début, l'après à la fin, gardés en **octets** exacts (le JSON
  de flecs arrondit) ; une commande dont l'avant égale l'après, selon `sameValue`, est jetée.

### Les panneaux

Dans **la bibliothèque éditeur** (ADR-0018, SPECS §7) : `editor/`, la cible `levain_editor`, qui lie `app` et
jamais un plugin, et qu'aucun module ni cible runtime de plugin ne lie (contrôlé) ; un jeu qui récupère Levain la
reçoit (hors du bloc `PROJECT_IS_TOP_LEVEL`, sous `if(NOT EMSCRIPTEN)` comme le cuiseur). **La hiérarchie**
montre les entités à `Transform`, rangées par `flecs::Parent`, et un nœud *Singletons* ; en M7.2, elle
sélectionne. **L'inspecteur** dessine un widget par champ, grise un composant sans `Authored`, et nomme un
composant non décrit.

- **Le branchement** : `editor::withEditor(start)` enveloppe la fonction de démarrage du programme (ADR-0029)
  et son `FrameHooks::ui`, sans changer la boucle ; seule extension d'`app`, `UiLayer` garde les nœuds de sa
  disposition, où ancrer les deux fenêtres. F1 les ouvre avec ceux de M7.1 ; le `main` éditeur pose
  `settings.showUiPanels = true` avant de lire les options : `--ui off` les ferme encore.
- **L'exécutable, construit par le jeu** : son `main.cpp` compilé une seconde fois, avec une définition qui
  enveloppe son démarrage, et lié à `levain::editor`. Levain construit `levain_sandbox_editor` (dans `sandbox/`,
  en natif) pour ses démos et sa CI ; *Rando*, `rando_editor` à côté de `rando`, dont la cible ne change pas.
- **`app` garde** les panneaux de M7.1 partout ; **le navigateur**, sans éditeur, n'a ni inspecteur ni explorer.

### Ce que M7.2 décrit

| Module | Données d'auteur (`describeAuthored`) | Lecture seule (`describe`) | Pas décrit (son nom seul) |
|---|---|---|---|
| scene | `Transform`, `Velocity`, `FpsController` | `WorldTransform`, `PreviousTransform`, `RenderAlpha`, `FpsInput` | `SimulationPipeline`, les phases |
| physics (dans physics.cpp) | `RigidBody`, `CharacterController` | `CharacterState`, `CharacterVelocity` | `Collider` (un `std::variant` de formes), les handles, les étiquettes, les singletons |
| assets | — | `MeshRef` (l'asset par son nom) | `AssetUsage` |
| app (`describeAppComponents`, qu'appellent `createApp` et le test) | `CameraLens` | — | `PlayerInput` |
| plugins/character (`describeWalkComponents`) | `Walker` | `WalkInput` | — |
| *Rando*, à sa montée de version | `TraversalRules`, `ThirdPersonCamera` (une fois coupée) | `Stamina` | `Traversal`, `CameraOrbit` (des `std::optional`) |
| hors description | — | — | `CharacterMotion` (`engine/animation` ne voit pas flecs), `Lake` (copié par la passe de l'eau à sa création : l'éditer changerait la nage sans bouger l'eau dessinée) |

### Les pièges, et leur nom

- **`describeAuthored` ou `describe`** — la déclaration force le choix, un `Authored` oublié se voit (grisé) ;
- **`refuseFieldWithoutReflection`** — un champ sans réflexion arrête l'import en le nommant ;
- **`fieldName`** et **`isIdentifier`** — le nom lu dans `__PRETTY_FUNCTION__`, tout autre format refusé ;
- **`fakeObject`** — une union jamais construite, pas l'objet `extern` de Boost.PFR et son pragma (règle n°4) ;
- **`offsetInProbe`** — le décalage dans un vrai `T`, jamais depuis un pointeur nul ;
- **`stableKeyOf`** et **`componentOfKey`** — la clé est le symbole, relu comme symbole, jamais comme chemin ;
- **`setComponentValue`** — la seule écriture ;
- **`rangeOf`** — une borne absente se lit `[0, 0]`, une borne posée a toujours min < max ;
- **`sameValue`** — comparer feuille par feuille, jamais par `memcmp` : le remplissage diffère ;
- **`entityFieldOf`** — un `flecs::entity` se lit comme tel, sans appeler ses fonctions opaques (UBSan) ;
- **une référence d'entité est un `flecs::entity`** — écrite par son chemin (sauf `CharacterGround::body`, un
  entier dans `CharacterState`, en lecture seule : les en-têtes de physics ne voient pas flecs) ;
- **les pièges des panneaux** — `isEngineInternal`, `eulerHint`, `selectedIfAlive`, `nameTakenAmongSiblings`,
  `pushEntityId` : les PR des panneaux les écrivent dans `editor/README.md`, celle de `reflection.hpp` ceux de la
  réflexion dans le README de scene, et les règles de relecture vont au GOTCHA du skill build.

### Les contrôles

Un test importe tous les modules et plugins du moteur, sans fenêtre ni GPU, et vérifie pour chaque composant
décrit sa clé, son JSON, ses bornes, et un seul `OnSet` par `setComponentValue`, `MeshRef` compris ; il décrit
aussi un type imbriqué après le type qui le contient. *Rando* ajoute le même. Un arrêt à l'import se teste par un
exécutable dont la sortie doit nommer la struct et le champ (`WILL_FAIL` passerait sur tout plantage) ; un refus
à la compilation, par une cible `EXCLUDE_FROM_ALL` par cas, que CTest construit et dont la sortie doit contenir le
texte du `static_assert`.

La frontière de 6B se vérifie : au configure, `levain_check_plugin_boundaries` (ADR-0018, lancé aussi par
*Rando*) parcourt `editor/` comme `engine/` et refuse qu'une cible du moteur ou un plugin lie `levain_editor` ;
`build.no-editor` refuse un symbole `levain::editor` dans `levain_sandbox` (`nm`, comme `build.no-tracy`). En
CI, `levain_sandbox_editor --select <nom>` tourne sous linux-debug et linux-asan, et l'étape échoue si la ligne
« éditeur : N entités, M champs dessinés » manque ou compte zéro. La PR de la hiérarchie l'introduit (celle de
l'inspecteur y ajoute les champs) et ajoute `editor` aux contrôles qui listent leurs dossiers (format et
clang-tidy de la CI, `HeaderFilterRegex`, `check_asset_libraries_visibility.cmake`, commande du GOTCHA du skill
build), qui resteraient sinon verts sans le lire (règle n°7).

## Conséquences

- **Pas de nouvelle dépendance** : flecs 4.1.6 compile déjà meta et json partout, web compris
  (`FLECS_CUSTOM_BUILD` n'est pas posé) ; le `vcpkg.json` de *Rando* ne change pas pour la réflexion.
- **Un lecteur de champs propre au compilateur**, vérifié sous clang 23 (natif et emsdk) et GCC 15. MSVC
  (ADR-0011) échouera à la compilation, jamais en silence : Boost.PFR remplacera alors l'intérieur de l'en-tête,
  et P2996 quand clang l'aura, sans toucher un appel.
- **Un composant décrit reste un agrégat, une donnée d'auteur se copie octet par octet** : ni `std::string` ni
  `std::optional` avant un ADR (M7.6 et M8.2 en voudront peut-être). Une étiquette (le `Cube` du sandbox) entre
  par `describeAuthored` ; les paires restent hors de la scène.
- **Le coût** : une dizaine de microsecondes par composant à l'import (Release), rien par entité ; ≈ 0,1 s
  d'include, puis 0,3 s pour 20 structs de 4 champs. **Le web** : le même code, essayé sous em++ 6.0.11 (la CI
  fige 6.0.10, non essayé).
- **Les refus de la physique** (5B) passent de `LEVAIN_ASSERT(false)` au journal d'erreurs, le corps ou le
  personnage restant refusé (physics.cpp:39, physics_world.cpp:280 et 286, character_sync.cpp:52,
  character.cpp:157) ; les gardes `#if !LEVAIN_ASSERTIONS_ENABLED` de physics_test.cpp (l. 660 et 867) tombent, les
  commentaires de `createCharacter` et d'assert.hpp se corrigent ; les assertions restent aux vrais bugs. Cette PR
  précède celles de la réflexion, des modules, de la hiérarchie (avec la bibliothèque éditeur, son squelette de
  100 à 150 lignes, et `levain_sandbox_editor`) et de l'inspecteur, chacune sous 400 lignes, l'échelle générée
  hors compte (règle n°2) ; si la PR de `reflection.hpp` passe 400 lignes, ses tests de refus à la compilation
  partent dans une PR à eux.
- **Pour *Rando***, à sa montée de version : `ThirdPersonCamera` se coupe (ce que chaque image réécrit passe
  dans `CameraOrbit`, `armLength` devient deux réglages, marche et vol plané, `target` un `flecs::entity`) ;
  `TraversalModule` appelle `describeWalkComponents` ; l'imgui de l'ADR-0032 entre dans son `vcpkg.json` ; la
  cible `rando_editor` (son `main.cpp`, ses plugins et `levain::editor`) s'ajoute à côté de `rando`, dont la
  cible ne change pas, et sa CI le lance comme `rando` (`--steps N`).
- **Pour M7.3** (sa ligne de la ROADMAP et SPECS §6 changent) : le JSON de flecs ne sert plus qu'aux valeurs ;
  l'enveloppe et le chargeur sont à nous, car `world.from_json` s'arrête en Debug et plante en Release sur
  `flecs::Parent`, `from_json` s'arrête en Debug sur `MeshRef` (`on_replace`), et une clé inconnue y devient une
  étiquette sans erreur. Un type ou un champ renommé casse une scène, et `set_json` ignore un champ inconnu en
  silence : M7.3 rend ce cas bruyant ; son estimation (1,25 h) se revoit à son ouverture. L'ADR lui donne les clés
  stables, `setComponentValue` et `sameValue` ; le reste, indicatif (règle n°6), ira dans #254 et #255.
- **Pour M7.4** : les gizmos lisent `WorldTransform` et écrivent le `Transform` local par `setComponentValue`.
- **Pour M7.5** : Play/Stop vit dans l'exécutable éditeur (ADR-0029), pas dans le jeu ; il garde les octets
  `Authored` et compare par `sameValue` ; le détail ira dans #258. **Pour M7.6** : les outils de terrain vont dans
  la cible éditeur du plugin terrain (ADR-0018), au-dessus de `levain_editor`.

## Ce que font les autres moteurs

- **Unreal** : `UPROPERTY(EditAnywhere, meta=(ClampMin=…))` sur le champ (**documenté** [1]), lu par l'Unreal
  Header Tool, « a custom parsing and code generation tool » (**documenté** [2]) ; le panneau Details parcourt ses
  descripteurs (`FProperty`) sans connaître la classe (**supposé**, d'après le code source, non revérifié). C'est
  notre option 1F ; nos bornes sont son `ClampMin`, `describeAuthored` son `EditAnywhere`, `describe` son
  `VisibleAnywhere` avec `Transient` : chez lui, une `UPROPERTY` est sauvegardée, éditable ou non. Le panneau
  Details vit dans un module Editor (**supposé**), qui « will only be loaded when the editor is starting up »
  (**documenté** [12]) : notre 6B.
- **Unity** : un champ public ou `[SerializeField]` est sérialisé (**documenté** [3]), et l'inspecteur « accesses
  the serialized backing field directly » (**documenté** [4]) : une vue du sérialiseur, comme le nôtre. `[Range]`
  en fait un curseur (**documenté** [5]) ; ses *property bags* font notre arbitrage : la réflexion par défaut, la
  génération de code en option, contre « longer compilation times » (**documenté** [6]). Ses scripts d'éditeur
  « aren't available in Player builds at runtime » (**documenté** [13]) : l'inspecteur reste hors du jeu livré.
- **Godot** : en C++, accesseur, mutateur et `ADD_PROPERTY(PropertyInfo(…))` dans `_bind_methods`, avec un usage
  (éditeur, stockage) (**documenté** [7]), notre option 1A ; en GDScript, `@export` suffit (**documenté** [8]).
- **flecs** : « Types are stored as entities, with components that store the reflection data » (**documenté**
  [9]), et l'explorer ne montre que ce qui est décrit (**documenté** [10]). Notre `ClassDB`, c'est l'ECS.

## Sources

1. Epic Games, *Unreal Engine UProperties* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-uproperties
2. Epic Games, *Unreal Header Tool* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine
3. Unity, *Serialization rules* — https://docs.unity3d.com/Manual/script-serialization-rules.html
4. Unity, *How Unity uses serialization* — https://docs.unity3d.com/Manual/script-serialization-how-unity-uses.html
5. Unity, `RangeAttribute` — https://docs.unity3d.com/ScriptReference/RangeAttribute.html
6. Unity, *Property bags* — https://docs.unity3d.com/Manual/property-bags.html
7. Godot, *Object class* — https://docs.godotengine.org/en/stable/engine_details/architecture/object_class.html
8. Godot, *GDScript exported properties* —
   https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_exports.html
9. flecs 4.1.6, `include/flecs/addons/meta.h`, commentaire d'en-tête.
10. flecs 4.1.6, `docs/FAQ.md`.
11. Clang, *C++ Support in Clang* (P2996 : « No ») — https://clang.llvm.org/cxx_status.html
12. Epic Games, *Plugins* — https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine
13. Unity, *Special folder names* — https://docs.unity3d.com/Manual/SpecialFolders.html
