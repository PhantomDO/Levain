# ADR-0018 — Moteur, plugins et jeu : deux dépôts, trois niveaux

- **Statut** : accepté le 2026-09-23 (validé par Donnovan, classement conservé tel quel) ; amendé par
  l'ADR-0028 (2026-10-05) : la marche du personnage est un plugin moteur
- **Date** : 2026-09-23
- **Milestone** : M3.6

## Contexte

En choisissant *Rando* (M3.5, [JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md)), Donnovan a demandé que **le jeu vive dans un autre dépôt
que le moteur**, pour distinguer nettement Engine et Game, et que les fonctionnalités se rangent en plugins
« à la manière d'Unreal ». Il a fixé la règle :

- ce dont **tout moteur de jeu** a besoin va dans le **moteur** ;
- ce qui sert au **level design** va dans un **plugin moteur**, dans le dépôt du moteur ;
- ce qui sert au **gameplay** va dans un **plugin gameplay**, dans le dépôt du jeu.

Il a aussi tranché, sur sondages : le jeu récupère le moteur par `FetchContent`, en attendant de l'installer
comme un paquet ; un plugin se lie à la compilation en v1 et se chargera dynamiquement plus tard ; l'éditeur est
une bibliothèque du moteur, dont le jeu construit l'exécutable. Restent à fixer : ce qu'est un plugin dans le
build, comment le jeu obtient les dépendances du moteur, et **où va chaque fonctionnalité**.

Aujourd'hui, rien de tout ça ne fonctionne : le moteur ne se configure que comme projet principal (les tests et
le sandbox sont toujours construits), et un projet qui l'inclut n'hérite pas de son manifeste vcpkg.

## Options envisagées

**1. Déclarer et activer un plugin** (sondage du 23/09) :

| Option | Pour | Contre |
|---|---|---|
| **Fonction CMake `levain_add_plugin`, import explicite dans le code** | La cible est créée et le sens des dépendances vérifié ; le jeu écrit `world.import<TerrainModule>()` : tout se lit, rien n'est généré | Pas de descripteur de fichier : il faudra l'ajouter avec le chargement dynamique |
| Descripteur par plugin et liste du projet (`.uplugin`, `.uproject`) | Le plus proche d'Unreal | Les imports sont générés : du code qu'on ne lit pas, contraire à la règle d'explicitation de Donnovan |
| Une bibliothèque CMake sans convention | Rien à écrire | Rien ne vérifie la frontière |

**2. Les dépendances vcpkg du moteur, vues du jeu** (sondage du 23/09) : vcpkg installe le manifeste **du projet
principal**, au `project()` du jeu, avant que `FetchContent` ait téléchargé le moteur. Le manifeste du moteur
est donc ignoré.

| Option | Pour | Contre |
|---|---|---|
| **Le jeu recopie le manifeste ; le moteur le contrôle** | Simple ; l'écart se voit à la configuration, avec le correctif dans le message | Deux fichiers à tenir alignés, y compris le port maison `ports/tracy` |
| Le moteur en port vcpkg dès maintenant | Les dépendances suivent seules | C'est le « paquet installé » avant l'heure : chaque modification du moteur se réinstalle |
| Sous-module git | Le manifeste du jeu peut pointer vers les ports du moteur | Revient sur le choix de `FetchContent` |

**3. Un plugin peut-il dépendre d'un autre ?** (sondage du 23/09) **Oui, s'il le déclare** : l'herbe dépend du
terrain, la nage de l'eau. L'interdire forcerait à fusionner des plugins sans rapport.

## Décision

**Deux dépôts** : `Levain` (le moteur et ses plugins moteur) et `Rando` (le jeu et ses plugins gameplay),
tous deux publics, sous licence MIT.

**Un plugin est un module flecs dans sa propre cible CMake**, créée par `levain_add_plugin(<nom> SOURCES …
DEPENDS …)`. La fonction refuse une dépendance non déclarée, et **le moteur ne dépend jamais d'un plugin**
(contrôlé par un test, comme `deps.flecs-visibility`). Le jeu lie les plugins qu'il veut et les importe
explicitement dans son code.

**Le jeu recopie le manifeste vcpkg du moteur** (dépendances, baseline, ports), et le moteur compare les deux à
la configuration : au moindre écart, la configuration échoue et dit quoi corriger (règle n°7).

### Où va chaque fonctionnalité

Classement proposé par Claude selon la règle de Donnovan, validé par lui sans correction.

| Fonctionnalité | Niveau | Pour | Contre |
|---|---|---|---|
| Animation squelettique (M4.5) | **Moteur** | Tout jeu avec des personnages en a besoin ; elle touche l'import glTF et le rendu (skinning) | Un jeu sans personnage la compile pour rien |
| Character controller (M6.3) | **Moteur** | C'est une brique de `physics` (le `CharacterVirtual` de Jolt) ; le *CharacterMovementComponent* d'Unreal est dans son cœur | Chaque jeu le règle différemment : les réglages restent des données |
| La marche : gravité, saut, accélération (M6.3, ajoutée par l'[ADR-0028](0028-personnage.md)) | **Plugin moteur** | Réutilisée par le sandbox et par *Rando* ; un jeu qui marche autrement s'en passe | Un plugin moteur qui n'est pas du level design : la catégorie s'élargit aux briques de gameplay réutilisables |
| Volumes déclencheurs (M6.2) | **Moteur** | Toute logique de niveau en a besoin : portes, pièges, points de contrôle | — |
| ImGui (M7.1), sons 3D (M8.1) | **Moteur** | Communs à tous les jeux et à l'éditeur | — |
| Terrain : rendu, LOD, collision (M5.6, M6.2) | **Plugin moteur** | Du level design ; un jeu en intérieur s'en passe | Il faut que `render` accepte des passes venues d'un plugin ; le Landscape d'Unreal est dans son **cœur** |
| Outils de terrain (M7.6) | **Plugin moteur**, partie éditeur | Ne se charge que dans l'éditeur | Un plugin a désormais deux cibles, runtime et éditeur |
| Eau (M5.7) | **Plugin moteur** | Du level design ; Water est un **plugin** chez Unreal | Même besoin de passes de rendu extensibles |
| Herbe et végétation (M5.7, M7.6) | **Plugin moteur**, dépend du terrain | Du level design, réparti sur le terrain | Foliage est dans le **cœur** d'Unreal |
| Caméra à la troisième personne (M6.4) | **Plugin gameplay** | Chaque jeu a sa caméra ; ses réglages sont du game feel | Le *SpringArm* d'Unreal est dans son cœur ; un second jeu en 3e personne la réclamera, et elle remontera |
| Nage, planeur, endurance (M6.5) | **Plugin gameplay** | Les règles propres à *Rando* | — |
| Cœurs, pièges, pommes, points de contrôle, énigmes (M8.2) | **Plugin gameplay** | Idem | — |
| La vallée, le sanctuaire, les réglages | **Jeu** (données) | Propres à *Rando* | — |

## Conséquences

**Pour le moteur (#115)** :

- Les tests, le sandbox et les benchmarks ne se construisent que si `PROJECT_IS_TOP_LEVEL` : un jeu qui inclut le
  moteur ne paie que ses bibliothèques. Les options de compilation (`-Werror`, `-pedantic-errors`) s'appliquent
  déjà aux seules cibles du moteur, parce que CMake les limite au dossier qui les déclare.
- Un script CMake compare le `vcpkg.json` du projet principal à celui du moteur, et les ports maison des deux
  côtés. En cas d'écart, la configuration échoue avec la liste des différences.
- `plugins/<nom>/` à la racine du moteur, comme `Engine/Plugins` chez Unreal ; chaque plugin a son `README.md`,
  comme chaque module. Aucun plugin moteur n'existe avant M5.6.

**Ce qui rend le chargement dynamique possible plus tard** (sans rien coder maintenant) :

- un plugin n'a **qu'un point d'entrée**, son module flecs, et aucune inscription par initialisation statique ;
- un plugin n'inclut que les en-têtes publics du moteur.

Le jour venu, une fonction `extern "C"` qui importe le module suffira comme point d'entrée d'une bibliothèque
partagée. Il faudra un nouvel ADR, qui remplacera celui-ci, comme pour le paquet installé.

**Ce qui coûtera plus tard** :

- **`render` doit accepter des passes de rendu venues d'un plugin** avant le terrain (M5.6) : un point
  d'extension qu'il faudra concevoir, sans doute par un ADR. C'est le prix principal du classement « plugin
  moteur » ; le mettre dans le moteur l'éviterait.
- Une mise à jour du moteur dans le jeu est **une PR du dépôt du jeu**, qui change le commit figé ; sa CI
  vérifie que tout compile encore. Pendant le développement, `FETCHCONTENT_SOURCE_DIR_LEVAIN` pointe vers le
  clone local : on modifie les deux dépôts sans rien publier.
- La règle n°1 (une PR ouverte à la fois) vaut **pour les deux dépôts ensemble** : il n'y a qu'un Donnovan.

SPECS §7 est mise à jour : `games/` disparaît, `plugins/` apparaît, et le graphe de dépendances gagne un
étage.

## Ce que font les autres moteurs

- **Unreal** (documenté [1]) : des plugins **moteur** (`Engine/Plugins`) et des plugins **de projet**
  (`<Projet>/Plugins`). Chacun a un descripteur `.uplugin` (modules, type de module : runtime ou éditeur,
  dépendances, phase de chargement), et le projet active les siens dans son `.uproject`. D'après l'arborescence
  du code source d'Unreal, le terrain (Landscape) et la végétation (Foliage) sont dans le cœur du moteur, et
  l'eau (Water) dans un plugin. C'est notre modèle, sans les descripteurs, pour l'instant.
- **Unity** (documenté [2]) : des *packages*, avec un manifeste `package.json` et des dossiers `Runtime/` et
  `Editor/` séparés par des *assembly definitions*. Même séparation runtime et éditeur que la nôtre.
- **Godot** (documenté [3]) : des *addons* dans `addons/<nom>/`, décrits par un `plugin.cfg` et activés dans les
  réglages du projet. Ce sont surtout des extensions de l'éditeur, en script. Le code natif passe par des
  modules, qui obligent à recompiler le moteur, ou par GDExtension.

## Sources

1. Epic Games, *Plugins in Unreal Engine* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine
2. Unity, *Package layout* — https://docs.unity3d.com/6000.2/Documentation/Manual/cus-layout.html
3. Godot, *Making plugins* — https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html
