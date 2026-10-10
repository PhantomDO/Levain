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
| [`include/levain/core/crt_report.hpp`](include/levain/core/crt_report.hpp) | Windows seulement : `routeCrtReportsToStderr` (plus de fenêtre de la CRT ni d'une exception : la ligne sur stderr, puis l'arrêt en Debug ou Windows Error Reporting en Release, voir plus bas) et ses fonctions pures, `failureEndingFor`, `crtReportActionFor`, `describeCrtReport`, `describeException` |
| [`include/levain/core/i18n.hpp`](include/levain/core/i18n.hpp) | `Catalog`, `catalogKey`, `placeholdersOf`, `activeCatalog`, `translate`, `translateFormat` : le catalogue des textes, le français pour clé (gettext) ; une traduction absente, vide ou aux `{}` fautifs laisse le français. `ui::tr` en est la façade (ADR-0036) |
| [`include/levain/core/profile.hpp`](include/levain/core/profile.hpp) | `LEVAIN_PROFILE_SCOPE`, `LEVAIN_PROFILE_SCOPE_TEXT` (un nom connu à l'exécution), `LEVAIN_PROFILE_PLOT` (une courbe), `LEVAIN_PROFILE_FRAME` — compilées hors du binaire par défaut (`ctest -R build.no-tracy`) |
| [`include/levain/core/version.hpp`](include/levain/core/version.hpp) | `version()` et `toolchain()` — la bannière de démarrage |

**La règle de lecture qui découle de l'ADR-0008** : si une fonction rend un `Result`, elle peut échouer sans que
ce soit notre faute. Si elle n'en rend pas, tout échec est un bug et s'arrête sur une assertion, sauf un refus de
donnée que personne ne peut recevoir (un corps physique créé par un système) : il va au journal d'erreurs, sans
assertion ([ADR-0034](../../docs/adr/0034-reflexion-des-composants.md)).

Les en-têtes publics vivent sous `include/levain/core/`, l'implémentation sous `src/`. On inclut donc
`"levain/core/version.hpp"`, jamais un chemin relatif.

## Les échecs sous Windows : une ligne sur stderr, puis l'arrêt ou WER

Un échec ouvrait une fenêtre modale (« Debug Assertion Failed! » pour `_ASSERT` et la STL, « abort() has been called »,
« Le programme a cessé de fonctionner » pour un plantage) : une CI ou un agent attendait un délai, et le message
n'arrivait pas au journal. **Plus de fenêtre de la CRT ni d'une exception Windows** : le message va sur stderr, et il
est aussi envoyé au débogueur (`OutputDebugStringA`, DebugView), car un programme sans stderr (sous-système GUI lancé
hors d'un outil) perd la ligne. Ce qui suit la ligne dépend de la configuration, et `failureEndingFor` le décide
(ADR-0035, [comparaison avec les autres moteurs][qa-release]) :

- **Debug** : le programme s'arrête lui-même, avec le code 3 ou celui de l'exception, sans WER. Sous un débogueur
  (`IsDebuggerPresent`), il s'y arrête, comme le « Retry » de l'ancienne fenêtre.
- **Release** : Windows Error Reporting (WER) reçoit l'échec, comme pour un programme sans Levain : son rapport et ses
  dumps restent, et le code de sortie est celui de Windows. Une fenêtre de WER ne s'est ouverte nulle part où l'on lance
  Levain (le runner pose `DontShowUI=1` ; sur le portable, mesuré, `build/GOTCHA.md`), donc rien n'est à couper.

| Ce qui échoue | Mécanisme (`src/crt_report.cpp`) | Debug | Release |
|---|---|---|---|
| `_ASSERT`, la STL | crochet `_CrtSetReportHook2` | « rapport de la CRT (…) », code **3** | n'existe pas : la CRT de Release n'a pas ces rapports |
| `abort()` | Debug : le même crochet ; Release : le gestionnaire de `SIGABRT` | « rapport de la CRT (erreur) », code **3** | « abort() ou SIGABRT », puis `__fastfail` : **0xC0000409**, WER |
| `raise(SIGABRT)` direct | le gestionnaire de `SIGABRT` | « abort() ou SIGABRT », code **3** | idem `abort()` |
| `std::terminate`, un appel virtuel pur | passent par `abort()` | idem `abort()` | idem `abort()` |
| un paramètre invalide de la CRT | Debug : le crochet (l'assertion précède) ; Release : `_set_invalid_parameter_handler` | « rapport de la CRT (assertion) », code **3** | « paramètre invalide… », puis `__fastfail` : **0xC0000409**, WER |
| une exception C++ que personne n'attrape | le filtre de la vcruntime, gardé par le nôtre : `std::terminate` | le gestionnaire de `std::set_terminate`, sinon `abort()` : code 3 | idem, puis la ligne de `abort()` : 0xC0000409 |
| un plantage : écriture en 0, `ud2` de la STL sous clang-cl, `int3` de `LEVAIN_ASSERT` | `SetUnhandledExceptionFilter` | « exception non gérée 0x… », `TerminateProcess` : code de l'exception | la même ligne, puis `EXCEPTION_CONTINUE_SEARCH` : WER, code de l'exception |
| `assert()` du C dans un programme sans console | `_set_error_mode(_OUT_TO_STDERR)` | « Assertion failed: … », puis `abort()` : code 3 | retiré par `NDEBUG` |
| un `__fastfail` direct (dépassement de `/GS`) | rien : **par conception**, aucun filtre ne le voit | muet ; 0xC0000409 | muet ; 0xC0000409, WER |
| le mode d'erreur | `SetErrorMode` | `SEM_FAILCRITICALERRORS` et `SEM_NOGPFAULTERRORBOX` | `SEM_FAILCRITICALERRORS` seulement, comme Microsoft le recommande à toute application |

**Un crochet, pas le seul mode FILE vers stderr** (Debug) : en mode FILE, `_CrtDbgReport` imprime puis rend la main, et
`_ASSERT` continue. Le crochet passe avant le mode, il est le seul à pouvoir arrêter. Il reçoit le message avec le
« fichier(ligne) » de la macro (l'en-tête de la STL pour `vector`, pas la ligne qui a mal indexé : c'est le
débogueur qui la donne) ; un `_ASSERT(x)` sans message rapporte `(null)`. La STL fait suivre son rapport d'un `ud2`
sous clang-cl (`__msvc_doom_core.hpp`), un `__fastfail` sous cl.exe que ce dépôt n'emploie pas.

**Pourquoi un gestionnaire de `SIGABRT` en Release** : `abort()` y finit par `__fastfail`, que nul filtre ne voit, donc
sans lui, aucune ligne. Il écrit la ligne, puis fait ce que fait la CRT après son gestionnaire
(`ucrt/startup/abort.cpp`) : un `__fastfail` si `_CALL_REPORTFAULT` est posé, sinon `_exit(3)` (doctest retire le
drapeau pendant ses cas : le gestionnaire le lit au lieu de le supposer). Il ne rend jamais la main, sinon un
`raise(SIGABRT)` direct continuerait. **Il ne sert qu'une fois** : la CRT le remet à `SIG_DFL` avant de l'appeler
(`ucrt/misc/signal.cpp`), donc un second `abort()` d'un autre thread finit sans ligne. Un `raise(SIGABRT)` direct finit
ainsi comme `abort()` (0xC0000409), là où la CRT sans gestionnaire ferait `_exit(3)`, muet : SIGABRT veut dire
`abort()` partout dans ce moteur. Le gestionnaire du paramètre invalide fait de même avec
`__fastfail(FAST_FAIL_INVALID_ARG)`, ce que fait `_invoke_watson` ; une CRT de Release lui passe des arguments nuls, la
ligne ne dit donc pas quelle fonction a refusé.

**Pourquoi aucun exécutable ne peut l'oublier.** Le routage tient dans une variable globale,
`LevainCrtReportRouted`, dont l'initialisation dynamique l'installe avant `main`. Une bibliothèque statique écarte le
fichier objet que personne ne référence : `/INCLUDE:LevainCrtReportRouted` force la référence, en `INTERFACE` sur
`levain_core` (tout exécutable qui le lie, celui d'un jeu compris) et sur chaque exécutable du moteur (un exécutable
qui ne lierait pas core échoue à l'édition de liens, `undefined symbol: LevainCrtReportRouted`). Un appel au début de
chaque `main` s'oublierait dans le prochain, et doctest génère le sien. L'ancre reste en Release, où elle installe la
ligne et la main rendue à WER. Rien n'est compilé hors de Windows. Le nom `routeCrtReportsToStderr` et l'ancre n'ont pas
changé : la fonction route toujours les rapports vers stderr, la Release y ajoute sa fin.

**Les limites, dites.**

- Un `__fastfail` direct ne laisse aucune ligne, ni en Debug ni en Release.
- **Le dernier gestionnaire posé gagne.** Une bibliothèque qui, après `main`, pose `SetUnhandledExceptionFilter`,
  `signal(SIGABRT)` ou un gestionnaire de paramètres invalides retire la ligne sans le dire ; rien ne le détecte
  aujourd'hui. doctest repose les nôtres à la fin de chaque cas.
- **La CRT de chaque DLL** : le triplet lie la CRT en DLL, donc les gestionnaires valent pour tout le processus. Une DLL
  liée en `/MT` aurait son propre `abort()`, non couvert.
- **Le joueur ne voit pas la ligne** : un exécutable GUI n'a pas de stderr. Le dump vient de WER
  (`%LOCALAPPDATA%\CrashDumps` si les `LocalDumps` de la machine sont posés). Le rapport de plantage propre à Levain
  (minidump et journal, comme `Saved/Crashes` chez Unreal) attend un jeu distribué, avec une issue et un ADR.
- **Ce que le probe ne garde pas** : un programme fenêtré qui plante (ses enfants n'ont pas de fenêtre ; une mesure
  ponctuelle, `build/GOTCHA.md`). Une machine où WER est désactivé, elle, fait rougir le probe (voir plus bas).
- **Chaque plantage de Release dépose un rapport chez WER** : un minidump et un dossier de rapport sur le portable,
  envoyé selon le consentement de la machine. Un passage du probe en Release en dépose 8 (`build/GOTCHA.md`).

**Sous doctest**, le `FatalConditionHandler` du cas pose ses réglages et rend les nôtres à la fin : il rapporte
lui-même un plantage ou un `abort()` (« test case CRASHED », code 1) ; un rapport de la CRT (`_ASSERT`, STL), lui, est
arrêté par notre crochet, qui passe avant son mode de rapport (stderr, code 3).

**Les contre-tests** : `ctest -R crt.report` (`tests/crt_report_probe.cpp`), un scénario par façon d'échouer, avec le
code de sortie exact et le texte de stderr ; `debugger.*` s'attache à l'enfant et exige un point d'arrêt (Debug). En
Release, deux choses de plus :

- **Le test de `failureEndingFor`** (`tests/crt_report_test.cpp`) garde la décision, mais pas qu'un gestionnaire la
  suive : un filtre qui terminerait lui-même le processus avec le même code laisse le code et stderr inchangés.
- **L'événement de WER** garde ce point : pour chaque plantage, le probe cherche dans le journal Application l'événement
  1000 (« Application Error ») qui nomme l'enfant (`EvtQuery`, un XPath sur les données nommées `ProcessId` et
  `ProcessCreationTime`, pas sur le texte localisé), et échoue bruyamment s'il n'y est pas 10 s après la fin de
  l'enfant, ou si le journal est illisible. Une machine où WER est désactivé rougit donc, au lieu de passer sans
  contrôle. **WER perd des rapports quand les plantages se chevauchent** (5 ou 6 événements vus sur 8 à `-j6`, cause non
  trouvée) : les tests `crt.report.*` de Release s'excluent par un `RESOURCE_LOCK`, un plantage à la fois. WER garde
  l'enfant en vie le temps de son minidump (3 s au repos, 34 s au plus vu) : à 10 s, un enfant sans événement est une
  fenêtre ou un blocage, rouge ; avec l'événement, le probe l'attend jusqu'à 120 s.

Mesures et pièges : `build/GOTCHA.md`.

## Équivalents ailleurs

| Moteur | Module | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `Runtime/Core` | `FString`, `FMemory`, `check`/`ensure`/`verify`, `FPlatformTime`, `FMemStack`. Notre `LEVAIN_VERIFY` est directement leur `verify`, et notre `LinearAllocator` joue le rôle de leur `FMemStack` (**documenté** : sources publiques). |
| **Godot** | `core/` | `Variant`, `Error`, `Memory`, `OS`, `String`. Godot y met aussi son système d'objets (`Object`, `ClassDB`), ce que nous **ne faisons pas** : chez nous le modèle objet est flecs et vit dans `scene` (**documenté** : dépôt public). |
| **Unity** | — | Le cœur C++ d'Unity n'est pas public. Ne pas supposer de correspondance. |

La différence qui compte : chez Unreal et Godot, `Core` porte aussi la réflexion et le système d'objets. Chez
nous, flecs s'en charge, donc `core` reste plus petit — logs, mémoire, temps, fichiers, environnement, et rien
d'autre.

Les fenêtres d'échec, comparées en détail dans [`docs/QA.md`][qa-release] : Unreal les supprime par `-unattended`
(**documenté** : la ligne de commande d'Unreal) et remplace WER par son `CrashReportClient` et son minidump (le
contournement lui-même est **supposé**). Unity a `-silent-crashes` pour son player (**documenté** : le manuel ;
`-batchmode` ne coupe que les fenêtres de l'éditeur) et son `UnityCrashHandler64.exe`, hors du processus. Godot
(**documenté** : `platform/windows/crash_handler_windows_seh.cpp`) imprime la pile sur stderr dans ses builds de
développement puis « passe l'exception à l'OS », donc à WER, et n'installe rien dans `template_release` : c'est le
modèle de la Release de Levain, avec une ligne en plus. O3DE appelle `ReportFault` exprès.

[qa-release]: ../../docs/QA.md#que-fait-un-moteur-quand-le-jeu-plante-sous-windows-en-release--2026-10-09-m14
