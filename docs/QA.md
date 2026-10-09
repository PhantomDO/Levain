# Questions et réponses

Les questions de Donnovan dont la réponse mérite d'être retrouvée. La plus récente en haut. Chaque réponse
renvoie au code, à un ADR ou à une source.

## Format

```
### Question (AAAA-MM-JJ, Mx.y)
Réponse courte, puis détails. Références : fichier:ligne, ADR, source externe.
```

---

### Que fait un moteur quand le jeu plante sous Windows, en Release ? (2026-10-09, M1.4)

**Aucun ne laisse un plantage muet là où un développeur regarde, et trois sur quatre laissent Windows Error
Reporting (WER) voir le plantage du build joueur.** WER voit les plantages d'Unity, que son gestionnaire a rapportés ;
Godot et O3DE n'écrivent rien en release et lui passent la main, O3DE l'appelle même exprès ; seul Unreal semble le
contourner : il termine lui-même le processus (code 3) et a de quoi le remplacer, minidump et CrashReportClient.
Microsoft le demande aux jeux : « If a fault such as an Access Violation appears in a game, it must allow Windows Error
Reporting to report the crash » [M5]. Couper les fenêtres est partout un mode d'automatisation (`-unattended`,
`-silent-crashes`, `sys_no_crash_dialog`) ou l'affaire du lanceur de tests (doctest chez Godot, AzTestRunner chez
O3DE), pas le comportement par défaut du joueur.

Sans mention, une case est documentée ; « (sup.) » marque ce qui est supposé. Build joueur : Shipping, player
non-development, `template_release`, Release.

| | Unreal 5 | Unity | Godot 4 | O3DE |
|---|---|---|---|---|
| Plantage | gardes SEH et filtre de processus ; journal, `Saved/Crashes` ; sortie forcée, code 3, vue dans des builds packagés de développement [U1, U4] ; en Shipping, même chemin (sup.) | gestionnaire hors processus : minidump et `error.log` [Y1, Y2], dans `%TMP%\…\Crashes` [Y3] | rien d'installé, aucune ligne [G1, G3] | `ReportFault` vers WER, `sys_WER=1` par défaut ; rien d'écrit [O1, O2] |
| abort, appel virtuel pur, paramètre invalide | `abort` [U8] et appel virtuel pur [U11] interceptés en Shipping ; paramètre invalide intercepté au cook [U9], en Shipping (sup.) | `ForceCrash` a `Abort` et `PureVirtualFunction` [Y7] ; leur capture (sup.) | défauts de la CRT, qui finissent chez WER [M2, C1, C2, C5] | appel virtuel pur et paramètre invalide → `CryFatalError`, puis WER [O4] ; `abort` : défaut de la CRT |
| Asserts | `check` retiré ; `Fatal` reste [U7] | `Assert` compilé en Development Build seulement [Y8] | `DEV_ASSERT` retiré ; `CRASH_COND` reste [G4] | `AZ_Assert` retiré ; `CryFatalError` reste [O4] |
| Fenêtre | la sienne, « The X Game has crashed and will close » [U12], sauf `-unattended` [U5] | la sienne, sauf `-silent-crashes` [Y4] | celle de WER, selon la machine | la sienne pour `CryFatalError` (boîte modale « Open 3D Engine Error »), sauf `sys_no_crash_dialog` [O6] ; celle de WER pour un plantage, selon la machine |
| WER | contourné (sup., de U1 et U6) | voit le plantage [Y5] (après Unity : sup.) ; la doc d'Unity passe par ses LocalDumps [Y6] | voit tout [G2] | appelé exprès [O2] |
| Désactiver | `SetCrashHandlingType(Disabled)`, pour un autre gestionnaire [U6] | rien, hors supprimer l'exe [Y2] | `--disable-crash-handler` [G6] | `sys_WER` ; `sys_no_crash_dialog`, qui coupe aussi WER [O1, O3] |
| En développement | même chemin | même chemin (sup.) | la pile sur stderr, **puis WER** [G2] | `error.log` et dump, **puis `ReportFault`** [O3] |

**Décidé pour Levain** (Donnovan : « Comment font les autres moteurs du marchés ? Base toi sur eux pour prendre ta
décision. ») : en Release, la ligne sur stderr, puis la main à WER, pour un plantage, un `abort()` et un paramètre
invalide (`engine/core/src/crt_report.cpp`). Le Debug garde sa fin : ni fenêtre ni WER, code 3 ou, pour un plantage,
celui de l'exception (0xC0000005 pour une écriture en 0), comme les lanceurs de tests de Godot [G5] et d'O3DE [O5] ;
seul `raise(SIGABRT)` y gagne une ligne. Les raisons :

- **Levain n'a pas de quoi remplacer WER.** Unreal le contourne parce qu'il a son minidump et son CrashReportClient.
  Microsoft : « application should not handle fatal exceptions » [M1] ; un gestionnaire de jeu « must pass any error
  on to the ReportFault or WerReportSubmit functions » [M5, M6]. Les rapports de WER arrivent aussi au développeur,
  pour un exe signé (Windows Desktop Application Program [M7]).
- **La CI et Donnovan lancent des programmes Release.** Sans ligne, un plantage n'y laisse que son code, que ctest
  tronque à 8 bits depuis la distro (0xC0000005 y devient 5).
- **La fenêtre de WER ne s'est ouverte nulle part où l'on lance Levain.** Aucune sur le runner (`DontShowUI=1`,
  débogueur JIT retiré [R1]). Aucune sur le portable : ni pour un programme sans fenêtre, ni pour un programme fenêtré
  au premier plan qui plante (mesuré le 2026-10-09, `build/GOTCHA.md`), alors que WER en montre une à un processus
  interactif [M1] et que le portable déclare le débogueur JIT de Visual Studio sans `Auto` [M8]. Un mode « sans
  surveillance » n'aurait rien à couper.
- **`abort()` et le paramètre invalide finissent en `__fastfail`** en Release, qu'aucun filtre ne voit [M3, C1, C2] :
  un gestionnaire écrit la ligne, puis fait le même `__fastfail` que la CRT, donc WER reçoit le même rapport.
  `std::terminate` et l'appel virtuel pur passent par `abort()` [C4, C5].
- **Pas d'interrupteur pour le joueur** : le gestionnaire ne fait qu'ajouter une ligne, et un futur gestionnaire de
  plantage posé dans `main` prend sa place (le dernier posé gagne).
- **Ce que ça coûte.** Debug et Release finissent différemment : code 3 sans WER, contre le code de Windows avec WER.
  Sur le portable, chaque plantage en Release laisse un minidump dans `%LOCALAPPDATA%\CrashDumps` et un dossier dans
  `ReportArchive`, et WER envoie son rapport selon le consentement de la machine. Un joueur ne voit pas la ligne : un
  exe GUI n'a pas de stderr.

Écarté : (a) rien ne change, le modèle de Godot et d'O3DE ; (c) le Debug aussi en Release, le modèle d'Unreal sans ce
qu'il met à la place de WER. Reporté au premier jeu distribué : le rapport de plantage propre à Levain (minidump et
journal, comme `Saved/Crashes` ou le dossier `Crashes` d'Unity), et la signature des exécutables qu'exige le rapport
de WER côté développeur [M6, M7].

Sources. Unreal (code non lu : il faut se connecter au GitHub d'Epic) :
[U1] https://forums.unrealengine.com/t/procedural-vegetation-nanite-foliage-meshes-crash-packaged-project/2730886
(build packagé de développement) ;
[U2] https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/ECrashExitCodes ;
[U3] https://dev.epicgames.com/documentation/unreal-engine/crash-reporting-in-unreal-engine ;
[U4] https://forums.unrealengine.com/t/navigation-doesnt-work-in-shipping-game/2670725 (son journal lance le
CrashReportClient de Debug) ;
[U5] https://forums.unrealengine.com/t/crashreportclient-not-saving-logs-when-run-from-development-editor/2668198/6
(staff Epic) ;
[U6] https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FGenericPlatformMisc/SetCrashHandlingType ;
[U7] https://dev.epicgames.com/documentation/unreal-engine/asserts-in-unreal-engine ;
[U8] https://forums.unrealengine.com/t/unreal-process-has-crashed-ue-marvel-this-effects-multiple-games-using-unreal/2179975 ;
[U9] https://forums.unrealengine.com/t/packaging-fatal-error-when-cooking/540160 ;
[U11] https://forums.unrealengine.com/t/pure-virtual-function-being-called-while-application-was-running-gisrunning-1/2078120 ;
[U12] https://forums.unrealengine.com/t/ue5-0-2-crash-while-changing-level-only-on-shipping/591753.
Unity (moteur natif fermé) :
[Y1] https://unity.com/releases/editor/whats-new/2018.1.0f1 ;
[Y2] https://discussions.unity.com/t/what-is-the-unity-crash-handler-do-i-need-it/799609 ;
[Y3] https://docs.unity3d.com/ScriptReference/Windows.CrashReporting-crashReportFolder.html ;
[Y4] https://docs.unity3d.com/Manual/EditorCommandLineArguments.html ;
[Y5] https://discussions.unity.com/t/unity-build-crashes-when-running-on-windows/929617 ;
[Y6] https://docs.unity3d.com/Manual/WindowsDebugging-forensic.html ;
[Y7] https://docs.unity3d.com/ScriptReference/Diagnostics.ForcedCrashCategory.html ;
[Y8] https://docs.unity3d.com/ScriptReference/Assertions.Assert.html.
Godot, `godotengine/godot@65e8d16951d6963cb3984c090e45f40d1ba5f704` :
[G1] https://github.com/godotengine/godot/blob/65e8d16951d6963cb3984c090e45f40d1ba5f704/platform/windows/crash_handler_windows.h#L35-L43 ;
[G2] https://github.com/godotengine/godot/blob/65e8d16951d6963cb3984c090e45f40d1ba5f704/platform/windows/crash_handler_windows_seh.cpp#L126-L128 et #L258-L259 ;
[G3] https://github.com/godotengine/godot/blob/65e8d16951d6963cb3984c090e45f40d1ba5f704/platform/windows/godot_windows.cpp#L144-L152 ;
[G4] https://github.com/godotengine/godot/blob/65e8d16951d6963cb3984c090e45f40d1ba5f704/core/error/error_macros.h#L101-L111 et #L578-L598 et #L843-L852 ;
[G5] https://github.com/godotengine/godot/blob/65e8d16951d6963cb3984c090e45f40d1ba5f704/thirdparty/doctest/doctest.h#L4745-L4762 ;
[G6] https://github.com/godotengine/godot/blob/65e8d16951d6963cb3984c090e45f40d1ba5f704/main/main.cpp#L630 et #L1957-L1958.
O3DE, `o3de/o3de@5e4c5e1cc47405a6840f048837731a820e49a93f` :
[O1] https://github.com/o3de/o3de/blob/5e4c5e1cc47405a6840f048837731a820e49a93f/Code/Legacy/CrySystem/SystemInit.cpp#L1002-L1007 et #L1317-L1324 ;
[O2] https://github.com/o3de/o3de/blob/5e4c5e1cc47405a6840f048837731a820e49a93f/Code/Legacy/CrySystem/WindowsErrorReporting.cpp#L100-L136 ;
[O3] https://github.com/o3de/o3de/blob/5e4c5e1cc47405a6840f048837731a820e49a93f/Code/Legacy/CrySystem/DebugCallStack.cpp#L231-L243 et #L281-L305 et #L652-L657 ;
[O4] https://github.com/o3de/o3de/blob/5e4c5e1cc47405a6840f048837731a820e49a93f/Code/Legacy/CryCommon/platform_impl.cpp#L40-L68 et
https://github.com/o3de/o3de/blob/5e4c5e1cc47405a6840f048837731a820e49a93f/Code/Legacy/CrySystem/IDebugCallStack.cpp#L210-L232 ;
[O5] https://github.com/o3de/o3de/blob/5e4c5e1cc47405a6840f048837731a820e49a93f/Code/Framework/AzTest/AzTest/Platform/Windows/Platform_Windows.cpp#L128-L133 ;
[O6] https://github.com/o3de/o3de/blob/5e4c5e1cc47405a6840f048837731a820e49a93f/Code/Legacy/CrySystem/SystemWin32.cpp#L285-L289
(le `MessageBoxW` de `CSystem::FatalError`, où mène `CryFatalError` :
https://github.com/o3de/o3de/blob/5e4c5e1cc47405a6840f048837731a820e49a93f/Code/Legacy/CryCommon/ISystem.h#L1048-L1066).
Microsoft :
[M1] https://learn.microsoft.com/windows/win32/wer/using-wer ;
[M2] https://learn.microsoft.com/cpp/c-runtime-library/reference/abort ;
[M3] https://learn.microsoft.com/cpp/intrinsics/fastfail ;
[M4] https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode ;
[M5] https://learn.microsoft.com/windows/win32/dxtecharts/games-for-windows-technical-requirements-1-1-0006 (§4.3,
programme hérité de Windows XP à 8) ;
[M6] https://learn.microsoft.com/windows/win32/dxtecharts/crash-dump-analysis ;
[M7] https://learn.microsoft.com/windows/win32/appxpkg/windows-desktop-application-program ;
[M8] https://learn.microsoft.com/windows-hardware/drivers/debugger/enabling-postmortem-debugging (`Auto`).
CRT (sources livrées avec le Windows SDK 10.0.26100.0 et MSVC 14.51.36231) :
[C1] `ucrt/startup/abort.cpp:15-21, 61-89` ; [C2] `ucrt/misc/invalid_parameter.cpp:90-113, 237` ;
[C3] `ucrt/misc/signal.cpp:481-488, 506-519` ; [C4] `ucrt/misc/terminate.cpp:38-58` ;
[C5] `vcruntime/purevirt.cpp:18-30`, `vcruntime/utility_desktop.cpp:86-102`.
Runner :
[R1] https://github.com/actions/runner-images/blob/e7c7cb8f4227797c6404a4e98c2ad463c2f70f91/images/windows/scripts/build/Configure-Diagnostics.ps1#L8-L21
(inclus par https://github.com/actions/runner-images/blob/e7c7cb8f4227797c6404a4e98c2ad463c2f70f91/images/windows/templates/build.windows-2025-vs2026.pkr.hcl#L187).

---

### Peut-on afficher dans le navigateur les images/s, la machine et le pourcentage d'utilisation ? Et Tracy ? (2026-10-06, M6.3)

**Les images/s et la machine, oui ; le pourcentage d'utilisation, pas tel quel.** Le moteur mesure déjà son temps
d'image : il l'écrit chaque seconde dans le titre de la page (`describeFrameTimes`, aujourd'hui dans `engine/app/src/app.cpp`,
« images/s »), que
l'Artifact ne montre pas. La machine se lit en JavaScript : le GPU tel que WebGPU le décrit (`GPUAdapterInfo` :
vendeur et architecture ; vides sous le Firefox de nos tests de fumée, où le moteur journalise
« WebGPU :  () »), le navigateur,
les cœurs logiques (`navigator.hardwareConcurrency`), la mémoire (`navigator.deviceMemory`, Chromium seulement,
arrondie et plafonnée à 8 Go), la résolution et le zoom de l'écran (`devicePixelRatio`).

Le navigateur ne donne en revanche **aucun pourcentage d'occupation du CPU ni du GPU**, pour la vie privée :
Chromium n'expose qu'un état grossier de la charge du CPU (Compute Pressure API, quatre niveaux), et rien pour le
GPU. On mesure à la place la part de l'intervalle entre deux images passée
dans notre code. Le temps GPU par passe demanderait la fonctionnalité WebGPU `timestamp-query`, que notre backend
WebGPU ne prend pas encore en charge (`engine/gpu/src/webgpu/commandlist.hpp:39`), et que Chrome arrondit.

**Tracy** n'a servi qu'en M1.1 et M1.3. Depuis, les chiffres du journal viennent de benchs versionnés
(`levain_bench`, `levain_physics_bench`), reproductibles sans profileur (règle n°6), et le moteur n'a que six
zones et une marque d'image, toutes dans le sandbox. Tracy reste le bon outil pour « où part l'image », mais en natif seulement : il lui
faut un socket TCP, et vcpkg ne l'installe pas pour Emscripten (`engine/core/CMakeLists.txt:34`).

**Décidé par sondage** : un calque des images/s, du CPU et de la machine sur la page web (#294), et Tracy remis en
service, le moteur instrumenté et une capture de la vallée analysée (#295), tous deux avant M6.4. Le temps GPU par
passe dans le navigateur n'est pas retenu pour l'instant.

---

### Est-ce qu'on est obligés de faire la CI à chaque PR ? (2026-10-05, M6.2)

**Pas obligés, mais chaque PR fusionnée devient un commit de `main`, et c'est la CI qui dit qu'il marche.** Ne la
lancer qu'à la dernière PR d'une feature laisserait des commits jamais vérifiés seuls : dans la pile de M6.2, la
PR des grandes formes, découpée après coup, n'avait jamais été compilée sans celle du terrain avant sa propre CI ;
et c'est la CI des PR empilées qui a trouvé deux échecs du sommet (un test multi-thread en WebAssembly, un contrôle
de la vue terrain), avant toute fusion. Un `main` cassé entre
deux rend aussi `git bisect` inutilisable.

**Ce qui coûtait, c'était l'attente en série**, pas la CI : les quatre jobs tournent en parallèle, 8 à 10 minutes
en tout (7 min 55 à 8 min 46 sur `main`), sur des runners gratuits (dépôt public). Mais une PR à la fois, c'était
« ouvrir, attendre, fusionner, ouvrir la suivante » : environ 1 h 30 estimées pour les neuf PR de M6.2.

**Décidé par sondage** : les PR d'un milestone, déjà relues, s'ouvrent ensemble, empilées ; leurs CI tournent en
même temps ; elles fusionnent ensuite dans l'ordre en merge commit. Un squash ne casserait pas la vérification de
la PR fusionnée, dont l'arbre est le même ; il casserait celle de la suivante, qui devrait se rebaser, changer de
SHA et repasser la CI. Et la cuisson des assets de test, 2 min 30 à 4 min du job Release, passe en cache.

**Les autres projets** : c'est le flux des « stacked diffs » (Phabricator chez Meta, Graphite, `git town`), qui
fusionnent d'ordinaire par squash ou rebase puis réempilent ; le merge commit est notre choix. Godot fait tourner
le même workflow complet sur chaque PR et dans sa file de fusion GitHub (`merge_group`, **documenté** :
`.github/workflows/runner.yml`). Unreal a Horde et ses *preflights*, un build lancé avant la soumission d'un
changement (**documenté** pour l'outil ; **supposé** pour l'usage chez Epic).

**Mis à jour le 2026-10-09 : Donnovan a tranché dans l'autre sens.** Les piles ont coûté trois jours d'attente (la
CI de dix PR empilées, dont `windows-build` de 57 à 115 minutes au lieu de 7 à 16), et la CI ne tourne plus qu'une fois
par fonctionnalité, sur la PR vers `main`, ou à la main sur la branche. Le prix est celui décrit plus haut : les
commits intermédiaires de la branche de la fonctionnalité ne sont pas vérifiés seuls par la CI, mais par
`tools/verify.sh` avant chaque fusion. La décision, avec les mots de Donnovan, est dans `AGENTS.md`, règle n°1.

Références : `AGENTS.md`, règle n°1 (sa version d'avant le 2026-10-09 expliquait les piles) ;
`.github/workflows/ci.yml`, « Cache des assets cuits ».

### Ce n'est pas plutôt le volume qui doit savoir qui est en lui ? (2026-10-05, M6.2)

**Si, et c'est lui qui pose la question ; la réponse est seulement rangée ailleurs.** Dans un ECS à archetypes
comme flecs, les composants d'une entité déterminent sa table. Si le lac portait la liste de ses occupants
(`(Overlaps, caisse_12)`, `(Overlaps, joueur)`…), chaque ensemble d'occupants différent serait une table
nouvelle : un essai de la relecture de l'ADR-0027 (non versionné, un ordre de grandeur) en a compté 20 000 créées
et jamais libérées pour 20 000 entrées et sorties, à 141 µs l'une. Rangée sur le corps, `(InsideOf, lac)`,
l'information ne change la table du corps qu'à l'entrée et à la sortie, avec un nombre de combinaisons borné :
0,18 µs dans le même essai. Et flecs **indexe une paire par sa cible** : il trouve directement les tables qui
portent `(InsideOf, lac)`, sans parcourir les autres.

L'API cache ce rangement et parle du point de vue du volume : `onEnter(lac, …)`, `onExit(lac, …)`,
`occupantsOf(lac)`. Le corps peut aussi demander `has<InsideOf>(lac)` quand c'est lui qui a besoin de savoir,
comme le joueur qui nage.

**Et le parallélisme ?** La détection des contacts tourne déjà sur les threads de Jolt. Côté flecs, ce qui se
parallélise, ce sont les requêtes sur des données (`.multi_threaded()` sur un système qui vise les lacs par une
variable, `with<InsideOf>("$volume").with<Lake>().src("$volume")`, et non ce lac-ci, qu'il empêcherait de
supprimer) ;
les observateurs, eux, tournent l'un après l'autre à la fusion des commandes, et poser ou retirer une paire est
un changement de structure, appliqué sur un seul thread, quelle que soit l'option.

**Les autres moteurs** : chez Unity, `OnTriggerEnter` est envoyé aux deux objets ; chez Unreal, les deux
reçoivent leur événement de chevauchement quand ils ont « Generate Overlap Events » ; chez Godot, c'est
l'`Area3D` qui signale `body_entered` et qui liste ses occupants (**documenté**, sources de l'ADR-0027).

Références : [ADR-0027](adr/0027-volumes-requetes-formes.md), option A' et décision 1.

### Pourquoi fastgltf plutôt que tinygltf, maintenant que gltf2ozz existe ? (2026-09-25, M4.5)

**gltf2ozz ne nous aurait presque rien épargné, parce que ce n'est pas une bibliothèque.** C'est un
exécutable en ligne de commande (`main()` dans `src/animation/offline/gltf/gltf2ozz.cc`), qui embarque sa
**propre copie** de tinygltf (`extern/tiny_gltf.h`) et jsoncpp pour sa configuration, et qui écrit des fichiers
`.ozz`. Avoir choisi tinygltf pour le moteur n'aurait donc rien partagé avec lui. Pour s'en servir, il aurait
fallu :

- le lancer en sous-processus depuis le cuiseur (comme les shaders, ADR-0014), et perdre le repli sur la
  source en cours d'exécution que l'ADR-0020 garantit pour tous les assets ;
- continuer à lire nous-mêmes le mesh, les poids des sommets et les matrices de liaison : gltf2ozz ne produit que
  le squelette et les clips.

La passerelle, elle, coûte environ 300 lignes et fait les deux dans le processus. Elle reprend de gltf2ozz la
seule astuce qui n'est pas évidente (les clés `STEP`), avec sa licence.

**Pourquoi fastgltf au départ** : c'est un choix de SPECS §6 (phase 0), fait pour M4.1 (meshes, matériaux,
textures), bien avant ozz : « rapide, C++ moderne ». Il analyse le JSON avec simdjson et utilise le SIMD
(documenté, README de fastgltf). Il ne décode pas les images, ce qui nous va : stb le fait déjà. tinygltf est un
seul en-tête, plus simple à intégrer, mais recopie tout dans ses propres structures. **Rien ne dit qu'il aurait
été trop lent pour nous** : l'écart n'a pas été mesuré sur nos fichiers (le glTF de Sponza se lit en 7,2 ms depuis la source, journal de M4.3).
Le choix se défend par la qualité de l'API plus que par un chiffre.

Références : [ADR-0022](adr/0022-animation-squelettique.md), `engine/animation/src/gltf_bridge.cpp`,
https://github.com/spnda/fastgltf.

---

### ozz-animation date-t-il ? Y a-t-il une autre bibliothèque d'animation ? (2026-09-25, M4.5)

**ozz est maintenu, mais par une seule personne, et il n'a pas de concurrent de même portée.** Relevé sur GitHub
le 25/09/2026 (`gh api repos/guillaumeblanc/ozz-animation/...`) :

- **Activité** : la version 0.17.0 est sortie le 01/08/2026, et 37 commits ont été faits de mars à mai 2026. Le
  rythme est d'environ une version par an (0.14 en 2022, 0.15 en 2024, 0.16 en janvier 2025). 2 900 étoiles.
- **Le risque réel** : Guillaume Blanc a écrit 1 600 de ses quelque 1 650 commits. Si le projet s'arrête, la
  licence MIT permet de garder notre copie figée : l'ADR-0022 le fige déjà sur un commit.
- **Qui s'en sert** : The Forge (ConfettiFX, moteur de rendu utilisé dans des jeux commerciaux) l'embarque dans
  son système d'animation (`Common_3/Resources/AnimationSystem/ThirdParty/OpenSource/ozz-animation`).

**Les autres projets trouvés** (recherche GitHub par étoiles, C++) :

| Projet | Ce qu'il fait | État |
|---|---|---|
| **ACL** (nfrechette, MIT, 1 600 étoiles) | **Compression** et décompression des clips seulement : ni hiérarchie, ni fondu, ni IK | Codec d'animation **par défaut d'Unreal depuis la 5.3** [1] ; dernière version en décembre 2023, commits jusqu'en septembre 2025 |
| eely (MIT, 51 étoiles) | Bibliothèque d'animation squelettique | Dernier commit en août 2024 |
| Les autres | Projets d'étudiants ou abandonnés (moins de 25 étoiles, derniers commits de 2015 à 2022) | — |

**ACL complète ozz, il ne le remplace pas** : il répond à « comment stocker un clip en petit et le relire vite »,
pas à « comment mélanger deux clips sur un squelette ». Il pourrait remplacer la compression d'ozz si la taille
des clips devenait un problème : c'est ce qu'a fait Unreal.

Les systèmes d'animation plus complets (graphes d'animation, *motion matching*) vivent dans des moteurs
(Unreal, Godot), pas dans des bibliothèques séparées (déduit de la recherche).

Références : [ADR-0022](adr/0022-animation-squelettique.md). [1] N. Frechette, *The Animation Compression Library
in Unreal Engine 5.3* — https://nfrechette.github.io/2023/09/17/acl_in_ue/

### Pourquoi `PreviousTransform` enveloppe un `Transform`, et pourquoi `Transform` et `WorldTransform` coexistent (2026-09-22, M3.3)

Deux questions de conception posées à la relecture de #103.

#### 1. Une struct qui ne contient qu'une autre struct

**La contrainte n'est pas le nommage, c'est l'identité.** flecs reconnaît un composant par son **type C++** :
`world.component<T>()` lui donne un identifiant. Deux valeurs du même type sur la même entité demandent donc
deux identités. `friend` ne crée pas d'identité (c'est de l'accès), et `union` dirait « l'un **ou** l'autre »
alors qu'il nous faut les deux en même temps — sans compter que lire un membre d'union écrit par un autre est
un comportement indéfini en C++ (c'est légal en C).

**Mais il existe mieux, et c'est propre à flecs : la paire.** Une paire `(Transform, Previous)` range un
`Transform` sous une identité différente, sans nouveau type :

```cpp
struct Previous {}; // une étiquette vide

entity.set<Transform, Previous>({.position = {7, 0, 0}});

world.query_builder<const Transform, const Transform>()
    .term_at(1).second<Previous>()
    .each([](const Transform& current, const Transform& previous) { /* … */ });
```

Essayé : la réflexion du `Transform` sert **aussi** à la paire, sans rien enregistrer de plus, et l'explorer
affiche `(levain.scene.Transform,Previous)` avec les mêmes champs. C'est l'idiome de l'exemple officiel des
hiérarchies de flecs, qui distingue `(Position, Local)` et `(Position, World)`.

| | Paire `(Transform, Previous)` | Struct enveloppe (le code actuel) |
|---|---|---|
| Nouveau type | aucun | un, qui n'existe que pour être nommé |
| Réflexion | partagée avec `Transform` | à réenregistrer, affichée sur un niveau de plus |
| Requête | `.term_at(1).second<Previous>()` | rien de spécial |
| Sécurité | deux `const Transform&` dans la lambda : **rien n'empêche de les intervertir** | le compilateur refuse de confondre `Transform` et `PreviousTransform` |

J'ai pris l'enveloppe pour cette dernière ligne : dans un système qui interpole, intervertir l'état courant et
l'état précédent donne un bug qui ne se voit qu'à l'œil, sur une image sur deux. La paire est plus idiomatique
et plus économe ; c'est un échange, pas une erreur.

**Décision de Donnovan (22/09/2026) : on garde l'enveloppe.** Sa règle, qui vaut au-delà de ce cas : « je
préfère que les choses soient explicites plutôt que quelqu'un qui relit le code ne le comprenne pas ». La paire
demande de tenir dans sa tête l'ordre des termes d'une requête pour savoir lequel des deux `Transform` est
l'état précédent ; l'enveloppe le dit dans la signature. Le raisonnement est recopié au-dessus de
`PreviousTransform` (`engine/scene/include/levain/scene/components.hpp`), pour qu'on n'ait pas à le retrouver
ici. La question reviendra à chaque « même donnée, autre sens » : la réponse par défaut est désormais le type
explicite, la paire restant possible si le coût de réflexion devient réel.

**Écarté aussi : l'héritage** (`struct PreviousTransform : Transform {}`). Il donne bien une identité sans
indirection, mais la conversion implicite vers la base fait compiler en silence un appel qui passe le mauvais
argument — et `offsetof` n'est garanti que sur un type *standard-layout*.

#### 2. `Transform` et `WorldTransform` : deux formats de la même chose ?

**Non : l'un est la source, l'autre est le produit.** Le passage `TRS → matrice` est exact et bon marché ; le
retour ne l'est pas.

- Une matrice 4×4 peut porter un **cisaillement** (le produit d'une rotation et d'une échelle non uniforme en
  porte un dès qu'on l'empile dans une hiérarchie). Un `Transform` (position, quaternion, échelle) **ne peut
  pas** le représenter : la décomposition perd de l'information, et le signe de l'échelle est ambigu (un
  miroir se lit comme une rotation).
- **La translation, elle, est bien « un pointeur sur une partie de la matrice »** : c'est la dernière colonne,
  contiguë, et c'est exactement ce que fait
  [`worldPosition`](../engine/scene/include/levain/scene/transform.hpp) — `glm::vec3(matrix[3])`. Mais la
  rotation ne l'est pas : les colonnes 0 à 2 mêlent rotation **et** échelle. Les séparer demande de normaliser
  les colonnes (faux dès qu'il y a cisaillement) puis de convertir une 3×3 en quaternion, une trentaine
  d'opérations par entité.
- **L'interpolation impose le quaternion** (M3.3) : interpoler deux matrices composante par composante rétrécit
  et gauchit l'objet en chemin. C'est pourquoi `ComputeWorldTransforms` interpole le `Transform` *puis* compose
  la matrice.
- **Un composant ne contient pas de pointeur** (invariant 3 du README de `scene`) : flecs déplace les
  composants en mémoire quand une entité change d'archétype. Un pointeur vers l'intérieur d'une matrice
  pendouillerait au premier `add` de composant.
- Taille : `Transform` 40 octets, `glm::mat4` 64. Garder les deux coûte 104 octets par entité ; tout en
  matrices en coûterait 128 (locale + monde) et perdrait tout ce qui précède.

Et les rôles diffèrent : `Transform` s'écrit (gameplay, éditeur, explorer), `WorldTransform` est **réécrit à
chaque image** par le système — le modifier à la main ne sert à rien (invariant 5).

**Ailleurs** : Unity Entities sépare `LocalTransform` (position, rotation, échelle **uniforme**) de
`LocalToWorld` (une `float4x4`), et sort l'échelle non uniforme dans `PostTransformMatrix` — précisément parce
qu'elle ne s'empile pas proprement dans une hiérarchie (**documenté** : manuel du package Entities,
« Transform concepts »). Unreal garde `FTransform` (Translation, Rotation en quaternion, Scale3D) pour le
gameplay et des `FMatrix` pour le rendu (**documenté** : référence d'API). Godot prend un intermédiaire,
`Transform3D` = une base 3×3 plus une origine, et paie l'ambiguïté de décomposition dont parle sa
documentation (**documenté**).

### Quiz sur l'étude E1 : les trois réponses à retenir (2026-09-21, M1.3)

Issue #17 faite par sondages : 5 bonnes réponses sur 8. Les trois erreurs portent sur la liaison des ressources,
le sujet de M2.1 — les voici corrigées.

**Pourquoi des décalages de binding (`-fvk-b-shift 256`…) pour Vulkan ?** HLSL sépare les registres par type :
`t0` (texture) et `b0` (constantes) ne se gênent pas. Vulkan n'a qu'une numérotation par descriptor set : le
binding 0 ne peut désigner qu'une ressource. NVRHI découpe donc l'espace en plages — textures à 0, samplers à
128, constantes à 256, UAV à 384 (`nvrhi::VulkanBindingOffsets`) —, et `slangc` doit appliquer les mêmes
(`shaders/CMakeLists.txt`). Sinon NVRHI lie les constantes au binding 256 et le shader lit le 0. Le DXIL, lui,
garde ses registres séparés et n'a besoin d'aucun décalage.

**Binding set ou descriptor table ?** Les deux existent sur les deux backends. Le *binding set* est immuable :
ses descripteurs sont écrits à la création, et NVRHI garde vivantes ses ressources et place leurs barrières. La
*descriptor table* (bindless) est un grand tableau modifiable que le shader indexe, par exemple par numéro de
matériau : NVRHI n'y suit **rien**. C'est la voie des gros moteurs et du ray tracing, et le choix de l'ADR de M2.1
(#40). Source : NVRHI Programming Guide (E1, §4).

**Qu'est-ce qu'un volatile constant buffer ?** Un buffer de constantes bien suivi par NVRHI, dont le **contenu**
est éphémère : il n'existe qu'entre le premier `writeBuffer` et la fermeture de la command list. NVRHI puise dans
un anneau de buffers d'upload à notre place ; sans lui, il faudrait un buffer par frame en vol, géré à la main.
C'est ce qui portera les matrices de la caméra en M2.1.

### Si on remet Windows, le moteur est-il prêt pour Direct3D 12, ou y aura-t-il beaucoup de travail ? (2026-09-21, M1.2)

Réponse courte : **c'est pensé pour, et le gros du travail n'est pas graphique.**

**Déjà interchangeable.**

- Tout ce qui s'écrit au-dessus de `nvrhi::IDevice` — command lists, pipelines, textures, passes de rendu —
  tourne sous Direct3D 12 sans changement : c'est la raison d'être de NVRHI ([ADR-0002](adr/0002-nvrhi.md)).
  `engine/render`, le plus gros du moteur, n'en saura rien.
- Hors de `engine/gpu`, un seul endroit connaît Vulkan : le drapeau `SDL_WINDOW_VULKAN` de
  `engine/platform/src/window.cpp` (vérifié par `grep` le 2026-09-21). SDL3 gère Windows.
- **Vulkan tourne aussi sous Windows.** Remettre Windows ne demande pas Direct3D 12 : le backend actuel y
  fonctionnerait tel quel. D3D12 serait un second backend, optionnel.

**À écrire pour Direct3D 12.**

1. `device_d3d12.cpp` à côté de `device_vk.cpp` : factory DXGI, adaptateur, device, queue, couche de debug, puis
   `nvrhi::d3d12::createDevice`. Donut y consacre **606 lignes, swapchain comprise, contre 1 413 pour Vulkan**
   (`DeviceManager_DX12.cpp`, `DeviceManager_VK.cpp`, mesuré avec `wc -l`) : D3D12 n'a pas la cérémonie
   d'instance, d'extensions et de fonctionnalités de Vulkan.
2. La swapchain DXGI, l'équivalent de `swapchain_vk.cpp`.
3. Les shaders compilés aussi en DXIL : prévu, Slang produit SPIR-V et DXIL ([ADR-0005](adr/0005-shaders-slang.md)),
   et nos shaders suivent déjà la convention de slots HLSL de NVRHI.
4. Le choix du backend au lancement (`--gpu vulkan|d3d12`).
5. **Le plus lourd, et rien de graphique** : presets et CI Windows, choix du compilateur (clang-cl d'abord,
   [ADR-0011](adr/0011-retour-au-cpp.md)), test de fumée sous WARP.

**Le seul couplage de notre API** : `engine/gpu/include/levain/gpu/device.hpp` nomme un type opaque
`VulkanContext` et un membre `vulkan`. Un second backend demandera un nom neutre, avec une définition par fichier
de backend. Un renommage de quelques lignes, pas fait tant que Windows est hors périmètre (il y revient avec
l'[ADR-0035](adr/0035-windows-compile-depuis-linux.md), en M1.4).

> **Mise à jour du 2026-10-08** (#18) : fait, mais pas tout à fait par un renommage. Les deux backends natifs
> vivent dans le même exe Windows, où un nom n'a qu'une définition : `NativeDevice` et `Swapchain` sont des
> classes de base, dont chaque backend dérive (`VulkanContext`, `VulkanSwapchain`), et la frame passe par un
> appel virtuel. C'est la forme du `FDynamicRHI` d'Unreal, réduite à la création du device et à la swapchain.

**Chez les autres** (documenté, sources publiques) : Unreal a une interface `FDynamicRHI`, avec les modules
`D3D12RHI` et `VulkanRHI` choisis au lancement (`-d3d12`, `-vulkan`) ; Godot a des `RenderingDeviceDriver`
Vulkan, D3D12 (depuis la 4.3) et Metal. Chez nous, NVRHI joue le rôle de leur RHI : il ne reste, par backend, que
la création du device et de la swapchain.

### Clang existe aussi sous Windows — pourquoi prendre MSVC ? (2026-09-20, M0.5)

Question posée à la clôture de M0.5. Réponse : **clang-cl ne règlerait aucun des deux bugs de M0.2**, ce qui est
contre-intuitif, mais il réglerait un problème plus profond.

> **Mise à jour du 2026-10-08** ([ADR-0035](adr/0035-windows-compile-depuis-linux.md)) : mesuré, clang-cl 23
> règle le premier bug, `__cplusplus` (point 1 ci-dessous) ; le second, `<ostream>`, vient bien de la STL de
> Microsoft et s'est retrouvé tel quel. Windows revient, compilé depuis Linux par clang-cl.

**Ce que clang-cl ne règle pas.** La [doc de compatibilité MSVC de Clang](https://clang.llvm.org/docs/MSVCCompatibility.html)
est explicite sur deux points :

1. **clang-cl reproduit volontairement le bug `__cplusplus`.** MSVC prétend être en C++98 ; clang-cl imite ce
   comportement par compatibilité, donc `/Zc:__cplusplus` reste nécessaire. **Faux pour clang-cl 23**, mesuré le
   2026-10-08 ([ADR-0035](adr/0035-windows-compile-depuis-linux.md)) : `__cplusplus` vaut 202002, 202700 et
   202302 sous `/std:c++20`, `/std:c++latest` et `/clang:-std=c++23`.
2. **clang-cl consomme la STL de Microsoft.** C'est son principe même : remplacer `cl.exe` en utilisant les
   en-têtes et bibliothèques MSVC. La divergence `<ostream>` venait de la STL, pas du compilateur — elle serait
   identique.

Nos deux bugs de M0.2 étaient des bugs de **bibliothèque et de driver**, pas de compilateur.

**Ce que clang-cl règle, et c'est le point important.** Avec MSVC on est coincés sur `/std:c++latest`, qui
déborde sur le brouillon C++26 : mesuré à `__cplusplus 202400` sous Windows contre `202302` sous Linux
(journal, M0.2). Avec clang-cl, `-std=c++23` donne **exactement C++23 sur les deux plateformes**. Le garde-fou
`-pedantic-errors` de l'ADR-0001 redeviendrait une précaution au lieu d'une nécessité. S'ajoutent les mêmes
diagnostics, warnings, clang-tidy et clang-format partout — fini l'épinglage de version d'un seul côté.

**La réserve** : la même doc précise que le support de l'ABI C++ de MSVC par Clang est « a work in progress ».
Non négligeable pour un moteur qui lie des dépendances compilées par vcpkg.

**La troisième voie** : clang + MinGW-w64 + libstdc++ donnerait *la même bibliothèque standard que sous Linux*,
donc plus de divergence de STL du tout. Mais ABI différente, support du SDK Windows et de Direct3D 12 plus
rugueux, et triplets mingw de vcpkg de qualité communautaire. Plus risqué.

**Décision** : sans objet tant que Windows est hors périmètre (ADR-0011). **Quand Windows redeviendra une cible,
clang-cl est la première option à évaluer** — et non MSVC par défaut, comme l'ADR-0001 l'avait posé sans le
justifier. C'est ce qu'a choisi l'ADR-0035, le 2026-10-08 : clang-cl, depuis Linux.

### Pourquoi « Levain » ? (2026-09-20, M0.1)

Parce que c'est le rythme du projet. Un levain se nourrit un peu chaque semaine, reste vivant entre deux
fournées, et sert de **base à partir de laquelle on cuit autre chose** — ce qu'est un moteur par rapport à un
jeu. Il se partage aussi, ce qui colle au « cuisiner à plusieurs » de Donnovan.

Le champ culinaire était déjà présent dans le projet avant le nom : M4.3 de la roadmap s'appelle
« Cuisson des assets » (*asset baking*).

Écartés et pourquoi : **Blitter** (la puce Amiga/Atari ST — Donnovan a commencé sur Game Boy Color et
PlayStation, la référence ne lui parlait pas) ; **Encore**, **Ludus**, **Noria**, **Marmite**, **Brigade**
(collisions GitHub sérieuses, dont `ludusavi`, un outil de sauvegardes de jeux — même domaine) ; **Braise**
(sémantiquement décroissante, elle pointe vers le passé) ; **Mijote** (verbe conjugué, mauvais namespace) ;
**Madeleine** et **Cartouche** (muets sur la technique).

Le namespace `levain` est une exception assumée à la règle « identifiants en anglais » (SPECS §8) : c'est un nom
propre, il ne se traduit pas, comme Godot.

### Le C++23 est-il stable en 2026 ? Peut-on y passer ? (2026-09-20, M0.1)

Oui côté Linux, « pas officiellement » côté Windows — et c'est gérable.

**Linux** : mesuré sur la machine de référence, Clang 22.1.8 et GCC 16.2.1 avec libstdc++ 16 donnent
`__cplusplus == 202302` et **toutes** les fonctionnalités C++23 sondées (dont `std::expected`, `std::print`,
`std::stacktrace`, `std::mdspan`, deducing this). Rien ne manque.

**Windows** : `/std:c++23` **n'existe pas**, ni en Visual Studio 2022 ni en Visual Studio 2026. Microsoft ne
propose que `/std:c++23preview` (« peut changer, peut ne pas être compatible en ABI d'une version à l'autre ») et
`/std:c++latest` (sur-ensemble qui déborde sur le brouillon C++26). CMake 4.4 mappe `CMAKE_CXX_STANDARD 23` vers
`-std:c++latest` chez MSVC (`/usr/share/cmake/Modules/Compiler/MSVC-CXX.cmake:46`). Les deux moitiés de la
matrice ne compilent donc pas le même langage.

**La parade** : c'est Linux qui fait autorité. La CI Linux compile en `-std=c++23 -pedantic-errors`, ce qui
refuse toute fonctionnalité post-C++23 (vérifié sur l'indexation de paquets `P2662`, refusée par Clang et par
GCC). Le code qui passe sous Linux est du C++23 ; MSVC, plus permissif, ne peut pas introduire de dérive
silencieuse.

Le déclencheur du changement est `std::expected` : la politique de gestion d'erreurs se décide en M0.3
(ADR-0008), et la trancher sans `std::expected` sous la main aurait appauvri le choix.

**Confirmé par la CI le 20/09/2026**, et le chiffre est parlant. Avec `/Zc:__cplusplus`, le même sandbox
compilé depuis les mêmes sources annonce :

| Plateforme | Compilateur | `__cplusplus` |
|---|---|---:|
| Linux | Clang 18.1.3, `-std=c++23` | **202302** — exactement C++23 |
| Windows | MSVC 19.51.36256, `/std:c++latest` | **202400** — au-delà de C++23 |

C'est la démonstration directe du raisonnement de l'ADR-0001 : les deux moitiés de la matrice ne compilent pas
le même langage, et Windows est plus permissif. `202400` n'est la valeur d'aucune norme publiée — c'est un mode
brouillon post-C++23. D'où le `-pedantic-errors` côté Linux : sans lui, rien n'empêcherait d'écrire du C++26 qui
passerait sous Windows.

Piège associé : **MSVC épingle `__cplusplus` à `199711L`** (la valeur de C++98) tant qu'on ne passe pas
`/Zc:__cplusplus`, quelle que soit la vraie version. Le premier run de CI l'a montré. Tout `#if __cplusplus >=
202302L` prendrait donc la mauvaise branche sous Windows, sans le moindre avertissement. Le flag est posé dans
le `CMakeLists.txt` racine, avec un commentaire pour qu'on ne le retire pas.

Détails, mesures et liste des trous MSVC restants : [ADR-0001](adr/0001-langage-cpp23.md).
Sources : [`/std` (msvc-180)](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version?view=msvc-180),
[conformance C/C++ Microsoft](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance).

### NVRHI est-il la norme de l'industrie ? Gère-t-il OpenGL et Metal ? (2026-09-20, M0.1)

Non aux deux. NVRHI est la couche de NVIDIA, utilisée par ses SDK RTX et ses exemples (Donut), et par quelques
projets tiers comme RBDOOM-3-BFG. Les grands moteurs ont chacun leur propre RHI (Unreal, Godot, Unity). Ses
backends sont Direct3D 11, Direct3D 12 et Vulkan : pas d'OpenGL ni de Metal. C'est sans conséquence pour nos
plateformes (Windows, Linux). On l'a choisi parce qu'il nous évite d'écrire une RHI, ce qui sert la priorité
« faire un jeu ». Détails : [ADR-0002](adr/0002-nvrhi.md), [étude E1](etudes/E1-rhi.md).

### NVRHI fonctionne-t-il sur une carte AMD ? (2026-09-20, M0.1)

Oui. NVRHI est une couche au-dessus de Vulkan et de Direct3D 12, deux API standard ; il tourne sur n'importe quel
GPU qui les prend en charge, dont la Radeon RX 9070 XT de la machine de référence. Seules les extensions propres à
NVIDIA (NVAPI, désactivée par défaut) sont réservées à ses cartes : on ne les utilise pas.

### Faut-il une VM Windows, ou Proton suffit-il ? (2026-09-20, M0.1)

Proton est plus utile : il lance le binaire Windows sur le vrai GPU, en traduisant Direct3D 12 en Vulkan
(vkd3d-proton). Une VM sans passthrough GPU n'a que du rendu logiciel, comme WARP en CI. Limite de Proton : ce
n'est pas un pilote D3D12 natif. Détails : SPECS §10.

### flecs ou EnTT ? (2026-09-20, M0.1)

flecs : documentation plus riche (quickstart, manuels par sujet, articles de l'auteur), explorer web, hiérarchies
et pipelines prêts à l'emploi, réflexion et JSON intégrés, utiles pour l'éditeur. EnTT est excellent et plus
minimal : il laisse davantage à écrire. Les deux sont bien maintenus. Détails : [ADR-0004](adr/0004-ecs-flecs.md).

### Faut-il utiliser un autre langage que le C++ pour un moteur en 2026 ? (2026-09-20, M0.1)

Non, pour ce projet. Rust, Zig et Odin sont de vraies alternatives, mais tous les moteurs étudiés sont en C++,
toutes les bibliothèques retenues aussi, et l'objectif est de comprendre les moteurs, pas d'apprendre un langage.
Détails : [ADR-0001](adr/0001-langage-cpp23.md).

### GitHub, GitLab ou Azure DevOps ? (2026-09-20, M0.1)

GitHub en dépôt public : CI Windows + Linux gratuite et illimitée, board avec champs personnalisés, CLI `gh`
pilotable par Claude Code. Le seul vrai atout de GitLab est le suivi du temps natif, compensé ici par des champs
du board et le journal. Détails : [ADR-0006](adr/0006-hebergement-github.md).
