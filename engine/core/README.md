# `engine/core`

## Rôle

Le socle : types de base, logs, assertions, allocateurs, temps, fichiers, environnement et, sous Windows, les
rapports de la CRT. Tout le moteur en dépend, et lui ne dépend de rien — ni SDL3, ni NVRHI, ni flecs.

**État en M1.1** : logs par catégorie, assertions, politique d'erreurs de
l'[ADR-0008](../../docs/adr/0008-gestion-erreurs.md), allocateurs linéaire et pool, macros de profilage Tracy,
statistiques de frame time, lecture de fichiers (les shaders compilés, M1.3), surveillance d'un dossier par les dates
de modification (le hot-reload des shaders, M2.3).

## Invariants

1. **Aucune dépendance tierce dans les en-têtes publics.** `core` est la racine du graphe (SPECS §7). spdlog
   est lié en `PRIVATE` et n'apparaît que dans les `.cpp` : c'est pour ça que `log.hpp` expose un
   `logMessage(std::string_view)` non générique, tout le formatage étant fait par l'appelant avec
   `std::format`. Un module qui lie `levain::core` ne voit pas spdlog.
2. **Pas de flecs ici.** Le modèle objet commence à `engine/scene`. `core` ne sait pas ce qu'est une entité.
3. **Compilable seul.** `levain_core` doit se construire sans qu'aucun autre module du moteur existe.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/core/log.hpp`](include/levain/core/log.hpp) | `log(category, level, fmt, …)` — une catégorie par module, le niveau est testé **avant** le formatage |
| [`include/levain/core/assert.hpp`](include/levain/core/assert.hpp) | `LEVAIN_ASSERT` (Debug seulement) et `LEVAIN_VERIFY` (évalue toujours) |
| [`include/levain/core/error.hpp`](include/levain/core/error.hpp) | `Result<T>` = `std::expected<T, Error>`, pour les échecs qui ne sont pas des bugs |
| [`include/levain/core/linear_allocator.hpp`](include/levain/core/linear_allocator.hpp) | `LinearAllocator` — arène vidée d'un coup, **9,4× plus rapide que `malloc`** |
| [`include/levain/core/pool_allocator.hpp`](include/levain/core/pool_allocator.hpp) | `PoolAllocator` — blocs de taille fixe rendus dans n'importe quel ordre, **5,9×** |
| [`include/levain/core/frame_time.hpp`](include/levain/core/frame_time.hpp) | `recordFrame` — moyenne, minimum et **maximum** par période : c'est le maximum qui montre une saccade |
| [`include/levain/core/file.hpp`](include/levain/core/file.hpp) | `readFile` — un fichier entier en mémoire, ou un `Result` en échec ; `pathForC` — un chemin pour une bibliothèque C en `char*` (ktx, stb) ; `FileWatch`, `watchDirectory`, `takeChangedFiles` — les fichiers d'un dossier créés ou modifiés depuis la dernière fois |
| [`include/levain/core/environment.hpp`](include/levain/core/environment.hpp) | `environmentVariable` — une variable d'environnement, ou `std::nullopt`, sans l'API que la CRT de Microsoft déconseille |
| [`include/levain/core/crt_report.hpp`](include/levain/core/crt_report.hpp) | Windows seulement : `routeCrtReportsToStderr` (en Debug, plus de fenêtre de la CRT ni d'une exception, voir plus bas) et ses fonctions pures, `crtReportActionFor`, `describeCrtReport`, `describeException` |
| [`include/levain/core/profile.hpp`](include/levain/core/profile.hpp) | `LEVAIN_PROFILE_SCOPE`, `LEVAIN_PROFILE_SCOPE_TEXT` (un nom connu à l'exécution), `LEVAIN_PROFILE_PLOT` (une courbe), `LEVAIN_PROFILE_FRAME` — compilées hors du binaire par défaut (`ctest -R build.no-tracy`) |
| [`include/levain/core/version.hpp`](include/levain/core/version.hpp) | `version()` et `toolchain()` — la bannière de démarrage |

**La règle de lecture qui découle de l'ADR-0008** : si une fonction rend un `Result`, elle peut échouer sans que
ce soit notre faute. Si elle n'en rend pas, tout échec est un bug et s'arrête sur une assertion, sauf un refus de
donnée que personne ne peut recevoir (un corps physique créé par un système) : il va au journal d'erreurs, sans
assertion ([ADR-0034](../../docs/adr/0034-reflexion-des-composants.md)).

Les en-têtes publics vivent sous `include/levain/core/`, l'implémentation sous `src/`. On inclut donc
`"levain/core/version.hpp"`, jamais un chemin relatif.

## Les rapports de la CRT (Windows, Debug)

En Debug, un échec sous Windows ouvrait une fenêtre modale (« Debug Assertion Failed! » pour `_ASSERT` et la STL,
« abort() has been called », « Le programme a cessé de fonctionner » pour un plantage) : une CI ou un agent attendait
un délai, et le message n'arrivait pas au journal. **Plus de fenêtre de la CRT ni d'une exception Windows** : le
message va sur stderr, le programme s'arrête avec un code non nul. Sous un débogueur (`IsDebuggerPresent`), il s'y
arrête, comme le « Retry » de l'ancienne fenêtre. Une fenêtre reste, hors de ce routage : celle de `SDL_assert`,
que SDL ouvre elle-même, en Release aussi pour `SDL_assert_release`.

| Ce qui échoue | Mécanisme (`src/crt_report.cpp`) | Sortie |
|---|---|---|
| `_ASSERT`, la STL, `abort()`, un paramètre invalide | crochet `_CrtSetReportHook2` | « rapport de la CRT (…) », code **3** |
| une exception C++ que personne n'attrape | le filtre de la vcruntime, gardé par le nôtre : `std::terminate` | le gestionnaire de `std::set_terminate`, sinon `abort()` : code 3 |
| un plantage : écriture en 0, `ud2` de la STL sous clang-cl, `int3` de `LEVAIN_ASSERT` | `SetUnhandledExceptionFilter` | « exception non gérée 0x… », code de l'exception |
| `assert()` du C dans un programme sans console | `_set_error_mode(_OUT_TO_STDERR)` | « Assertion failed: … », puis `abort()` : code 3 |
| Windows Error Reporting | `SetErrorMode(SEM_FAILCRITICALERRORS \| SEM_NOGPFAULTERRORBOX)` | pour un `__fastfail`, que rien n'intercepte : **non vérifié**, ni la fenêtre de WER ni le débogueur JIT |

**Un crochet, pas le seul mode FILE vers stderr** : en mode FILE, `_CrtDbgReport` imprime puis rend la main, et
`_ASSERT` continue. Le crochet passe avant le mode, il est le seul à pouvoir arrêter. Il reçoit le message avec le
« fichier(ligne) » de la macro (l'en-tête de la STL pour `vector`, pas la ligne qui a mal indexé : c'est le
débogueur qui la donne) ; un `_ASSERT(x)` sans message rapporte `(null)`. La STL fait suivre son rapport d'un `ud2`
sous clang-cl (`__msvc_doom_core.hpp`), un `__fastfail` sous cl.exe que ce dépôt n'emploie pas. Pas de gestionnaire
de `SIGABRT` : `abort()` fait son rapport avant de lever le signal, et `raise(SIGABRT)` finit par `_exit(3)`.

**Pourquoi aucun exécutable ne peut l'oublier.** Le routage tient dans une variable globale,
`LevainCrtReportRouted`, dont l'initialisation dynamique l'installe avant `main`. Une bibliothèque statique écarte le
fichier objet que personne ne référence : `/INCLUDE:LevainCrtReportRouted` force la référence, en `INTERFACE` sur
`levain_core` (tout exécutable qui le lie, celui d'un jeu compris) et sur chaque exécutable du moteur (un exécutable
qui ne lierait pas core échoue à l'édition de liens, `undefined symbol: LevainCrtReportRouted`). Un appel au début de
chaque `main` s'oublierait dans le prochain, et doctest génère le sien. Rien n'est compilé hors de Windows.

**Release : rien ne change**, ni crochet, ni filtre, ni mode d'erreur. L'ancre y reste (l'oubli de core échoue
toujours à l'édition de liens) et n'installe rien. Ce que doit faire la Release, celle d'un jeu livré, est une
décision à prendre (sondage) : elle garde aujourd'hui Windows Error Reporting, ses rapports et ses dumps.

**Sous doctest**, le `FatalConditionHandler` du cas pose ses réglages et rend les nôtres à la fin : il rapporte
lui-même un plantage ou un `abort()` (« test case CRASHED », code 1) ; un rapport de la CRT (`_ASSERT`, STL), lui, est
arrêté par notre crochet, qui passe avant son mode de rapport (stderr, code 3).

## Équivalents ailleurs

| Moteur | Module | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `Runtime/Core` | `FString`, `FMemory`, `check`/`ensure`/`verify`, `FPlatformTime`, `FMemStack`. Notre `LEVAIN_VERIFY` est directement leur `verify`, et notre `LinearAllocator` joue le rôle de leur `FMemStack` (**documenté** : sources publiques). |
| **Godot** | `core/` | `Variant`, `Error`, `Memory`, `OS`, `String`. Godot y met aussi son système d'objets (`Object`, `ClassDB`), ce que nous **ne faisons pas** : chez nous le modèle objet est flecs et vit dans `scene` (**documenté** : dépôt public). |
| **Unity** | — | Le cœur C++ d'Unity n'est pas public. Ne pas supposer de correspondance. |

La différence qui compte : chez Unreal et Godot, `Core` porte aussi la réflexion et le système d'objets. Chez
nous, flecs s'en charge, donc `core` reste plus petit — logs, mémoire, temps, fichiers, environnement, et rien
d'autre.

Les fenêtres d'échec : Unreal les supprime par `-unattended` (**documenté** : la ligne de commande d'Unreal) et
remplace celle de Windows par son `CrashReportClient`. Unity a `-batchmode` (aucune fenêtre ; **documenté** : le
manuel) et `UnityCrashHandler64.exe`. Godot a un gestionnaire de plantage par plateforme dans `platform/` (**à
vérifier** : non relu pour cette PR).
