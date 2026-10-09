# Pièges — build, tests, sanitizers, CI

Un piège par entrée : symptôme, cause, parade. Le plus récent en haut. Les pièges propres à SDL sont détaillés
dans `engine/platform/README.md`, ceux de flecs dans `engine/scene/README.md`, section « Pièges connus ».

## Le sandbox sous Windows en CI : le bash de Git, Sponza hors de l'artefact (2026-10-09)

Les étapes du sandbox, de l'éditeur et du cuiseur dans `windows-debug` et `windows-release` (#346), par
`tools/ci-programs.sh`, le script des jobs Linux. Répétées sur le portable : le bash de Git lancé de la distro comme
sur le runner (`"/mnt/c/Program Files/Git/bin/bash.exe" --noprofile --norc -e -o pipefail -c …`), depuis la racine
du worktree, lavapipe par `VK_DRIVER_FILES` (WSLENV).

- **Le `bash` d'Actions sous Windows est celui de Git**, dont `bin/bash.exe` met `/usr/bin` en tête du PATH :
  `timeout` est celui de coreutils, pas `C:\Windows\System32\timeout.exe`. Il arrête un exe Windows : code 143 au bout
  de 20 s, sans processus restant (`tasklist.exe`). Il lance aussi `build/…/levain_sandbox` sans `.exe` ; le bash de
  la distro, non (code 127) : le script cherche l'exe avec son extension (`executableIn`).
- **spdlog finit ses lignes par « \r\n » sous Windows.** Le grep de Git les lit comme des fins de ligne, le grep GNU
  de la distro non : sans `tr -d '\r'` (`logTo`), le contrôle de l'éditeur, ancré par `$`, échoue lancé de la distro
  (« la sélection n'est pas celle de --select ») et passe dans le bash de Git.
- **Sponza ne va pas dans un artefact** : celui d'un dépôt public se télécharge depuis n'importe quel compte GitHub,
  et sa licence interdit la redistribution (`tools/assets.lock`). Le runner Windows reprend le cache d'assets des
  jobs Linux (`enableCrossOsArchive`), puis `tools/fetch-assets.sh` vérifie tout et télécharge ce qui manque ; il
  tourne dans le bash de Git (curl, sha256sum : téléchargement et seconde passe sans rien à faire, répétés).
- **`texture-hot-reload.sh` attendait 4 s fixes** avant d'écrire les octets invalides. Lancé de la distro, le scan
  des assets prend 3 s sur le partage `\\wsl.localhost` : le sandbox lisait les octets invalides à son premier
  chargement (« CesiumMilkTruck.jpg : unknown image type », critique, code 1), sur lavapipe comme sur la 4070. Le
  script attend maintenant la ligne « clic droit pour regarder » (le camion chargé, la boucle qui part), une minute au
  plus : rechargé 167 à 305 ms après l'écriture.
- **Lancé de la distro, le bash de Git** a pour dossier courant `//wsl.localhost/levain-dev/…` : `mkdir -p` d'un
  chemin absolu de ce partage échoue (« Read-only file system », il remonte jusqu'à `//wsl.localhost`), un chemin
  relatif passe ; `TMPDIR` dans le scratchpad, sans quoi `mktemp` et `<<<` écrivent dans le `%TEMP%` de Donnovan.
- **lavapipe pour Windows est 1,6 à 5 fois plus lent que celui de la distro**, sur la même machine (Debug, 32
  processeurs : le terrain 3 à 6 images en 3,3 s contre 16, le personnage 81,5 s contre 51,3 pour 200 pas). Limité à
  4 processeurs comme le runner (`cmd.exe /c start "" /b /wait /affinity F` devant le bash de Git, et
  `LP_NUM_THREADS=4`), la vue terrain en `--seconds 3` ne faisait que 3 images : le minuteur GPU, qui en demande
  quatre pour une mesure, n'avait rien mesuré, et « l'étape transparente n'a rien dessiné » (rouge, une seule fois
  limité à 4 processeurs, une fois sur trois sans limite). Le runner Linux fait 7 images en 3,3 s (run 37866995438,
  `linux-debug`, étape « Lancer le sandbox sur le terrain »). Parade : `--steps 8` au lieu de `--seconds 3`, des deux
  côtés, comme le personnage, la vallée et l'interface (« quelle que soit la vitesse du runner ») : huit images, cinq
  mesures du minuteur, à chaque lancement (Linux 0,8 s ; lavapipe pour Windows 5,0 à 6,3 s à 32 processeurs, 8,8 à
  10,2 s à 4 ; 4070 0,2 s) ; `--steps 3` rougit, sur Linux comme sous Windows. Aucun contrôle du terrain ne dépend du
  temps écoulé : « 66 corps » est un compte, et « lac : N caisses dans l'eau » n'a jamais dit autre chose que « 0 »
  en 3 s (12 journaux relevés, la 4070 à 295 images comprise), les caisses ne tombant dans l'eau que dans
  `terrain_test.cpp`, en 600 pas.
- **Le renard dans la vallée sur WebGPU, limité à 4 processeurs, a pris environ 249 s de processus** à la première
  répétition (234,5 s de boucle ; 247 s entre la première et la dernière ligne du journal, le démarrage en plus), et
  122,5 s à la seconde (112,4 s pour Vulkan, mesurés par la trace horodatée de `bash -x`), même commande : le double
  d'écart sur le portable. Un `timeout` de 300 s ne laissait que 51 s de marge dans le premier cas, et un dépassement
  tue le processus avec le seul code 143, sans `::error::`. Parade : 450 s pour la vallée, des deux côtés (`runHike`) ;
  `timeout` y est un filet contre un blocage, pas un contrôle (en-tête de `tools/ci-programs.sh`).
- **La fenêtre du sandbox est de 1920 × 1080 et redimensionnable ; Windows la réduit à la taille du bureau**, que
  GitHub ne documente pas pour ses runners. `--pick 960,540`, le
  centre d'une fenêtre de 1920 × 1080, ne viserait plus le centre : la sélection échouerait sous « aucune caisse », ou
  passerait en ne vérifiant plus ce qu'elle dit. Parade : `requireFullHdCapture` (`tools/ci-programs.sh`) exige
  « capture : physics-<gpu>.png (1920 × 1080) » avant les contrôles de la physique, et échoue en nommant la cause ;
  l'étape « Bureau du runner » affiche la résolution (`Win32_VideoController`), à lire au premier passage. Contre-test :
  un sandbox qui rejoue le journal réel avec une capture de 1280 × 720, ou sans ligne de capture, rougit, dans le bash
  de la distro et dans celui de Git.
- **L'artefact n'emporte que les exe de `sandbox/`** : le `.dll` et le `.json` des couches de validation à côté du
  sandbox (6,9 Mo compressés, par artefact) ne servent qu'au PC (`addLayerPathBesideExecutable`) ; le runner, élevé,
  passe par le registre, qui désigne ceux de `tests/`.
- **Le `start` des 4 processeurs ne rend pas le code de l'enfant** : lire les `::error::` du journal.

## La CI Windows : lavapipe pour Windows, ctest sur des chemins Linux (2026-10-08)

Les jobs `windows-*` de `ci.yml` (#346). Répété en local avant la CI : le ctest de Windows lancé de la distro par
`cmd.exe`, après `pushd \\wsl.localhost\levain-dev\…\build\windows-debug`, qui prête une lettre de lecteur au partage :
les chemins `/home/…` s'y lisent comme sur le runner (lavapipe par `VK_DRIVER_FILES`, `vulkan-1.dll` à côté des exe).

- **Le lavapipe de mesa-dist-win n'a pas `VK_EXT_headless_surface`** (vulkaninfo : 16 extensions d'instance, aucune
  « headless ») : sous `SDL_VIDEO_DRIVER=offscreen`, « Installed Vulkan doesn't implement the VK_EXT_headless_surface
  extension ». Parade : sous Windows, les tests prennent le pilote `windows`, une vraie fenêtre (`VK_KHR_win32_surface`).
- **Le runner Windows lance tout en administrateur, et le chargeur Vulkan ignore alors les chemins de
  l'environnement** (`VK_DRIVER_FILES`, `VK_ADD_LAYER_PATH` : « Loader is running with elevated permissions »,
  `loader_environment.c`) : vulkaninfo, « Found no drivers! », alors que la variable était posée ; en local, non
  élevé, elle marchait. Parade : lavapipe et les couches de l'artefact inscrits au registre de la machine jetable
  (`HKLM\SOFTWARE\Khronos\Vulkan\Drivers` et `ExplicitLayers`) ; vulkaninfo doit nommer les deux avant les tests.
  Conséquence : la CI ne passe plus par `addLayerPathBesideExecutable` (`VK_ADD_LAYER_PATH`, `device_vk.cpp`) ; seul
  le PC de Donnovan le vérifie, par `ctest -LE host` lancé de la distro.
- **Sur le runner Linux, la découverte des cas de doctest écrit « Syntax error »** (« levain_tests : la découverte des
  cas a échoué : 2 …/levain_tests.exe: 1: Syntax error: word unexpected », ou « Unterminated quoted string » pour
  l'exe de Release, run 37853947275) : le noyau refuse le binaire PE
  (ENOEXEC), et `execvp` le passe alors à `/bin/sh`, qui le lit comme un script. Sans conséquence : le test rouge
  `levain_tests_NOT_DISCOVERED` n'a pas le label `host`, et la découverte se fait sur le runner Windows.
- **Un faux `.exe` lancé de la distro ouvre sur le bureau de Windows un dialogue modal** (« Application 16 bits non
  prise en charge »), et le processus attend qu'on le ferme. Un contre-test efface l'exe, il ne le remplace pas.
- **Un job sauté parce que celui dont il dépend a échoué compte comme réussi** pour la protection de `main` :
  `windows-build`, `windows-debug` et `windows-release` sont requis tous les trois ensemble, à partir de la fusion de
  #359 (sondage de Donnovan du 2026-10-08) ; l'agent de la session les ajoute juste après la fusion.

## La découverte de doctest faite par ctest, et le winsysroot de xwin (2026-10-08)

Les cas de `levain_tests` découverts par ctest sur la machine qui lance l'exe (`tests/CMakeLists.txt`), et le
winsysroot de xwin, celui de la machine de référence et de la CI (`tools/winsysroot.sh`) (#346).

- **xwin 0.10.0 ne sait pas tirer MSVC 14.51 et le SDK 10.0.26100 d'un manifeste de Visual Studio 18** : « unable to
  find Universal CRT MSI » (le SDK y écrit `Installers\` ; le paquet à part de la CRT universelle, quand il existe, est
  pris à sa place, et ce n'est pas celle du PC) ; le SDK 10.0.28000 échoue au téléchargement (des espaces dans les
  adresses, xwin #188) ; le manifeste 17 s'arrête à MSVC 14.44. Parade (`tools/winsysroot.sh`) : le manifeste de 18.8.1
  figé par SHA-256, deux corrections vérifiées, posé dans le cache de xwin. Comparé fichier par fichier au
  `~/winsysroot` du portable (les Build Tools 18.8.1) : 5 597 fichiers identiques, aucun différent, 27 en-têtes
  `shared/netcx` absents du portable. Commande, depuis le winsysroot de xwin : `find . -type f ! -name
  .levain-winsysroot -print0 | while IFS= read -r -d '' f; do if [ ! -e ~/winsysroot/"$f" ]; then echo "absent
  ${f%/*}"; elif cmp -s "$f" ~/winsysroot/"$f"; then echo identique; else echo "différent $f"; fi; done | sort | uniq -c`.
- **xwin nomme les dossiers des versions courtes** (`MSVC/14.51`, `Include/10.0.26100`) : le script les renomme.
- **ctest ne définit pas `CMAKE_COMMAND` en lisant ses fichiers** : la découverte de doctest ne peut pas y relancer
  `cmake -P` sans nommer le cmake de la machine de build. Parade : le script de doctest est inclus. Mais son
  `FATAL_ERROR` arrête alors ctest entier (code 8, aucun test lancé, `-L host` compris) : la liste est demandée avant.
- **`execute_process` refuse sous Windows un `WORKING_DIRECTORY` sans lecteur** (`/home/…` : « no such file or
  directory »), quand ctest et les exe le prennent sur le lecteur courant. Parade : `file(REAL_PATH)`.
- **ctest lit ses fichiers sans politique** : sous Windows, `execute_process` décode alors la sortie dans la page de
  code de la console, et `--list-test-cases` rend « sup├⌐rieur » ; le cas ainsi nommé n'existe pas. Parade : CMP0176.

## Un shader juste sous lavapipe, faux sur un vrai GPU : la division arrondie à 2,5 ULP (2026-10-08)

- **Symptôme** : `gpu.environment.*` passe sous lavapipe, échoue sur la RTX 4070 (Vulkan comme WebGPU), sans
  erreur de validation : BRDF lisse à 0,394 au lieu de 1, reflet lisse à 0,016 au nadir au lieu de 0 (#347).
- **Cause** : `sqrt(1 − cos²)`, où cos² est le quotient de deux nombres égaux. Lavapipe divise et tire la racine
  comme le CPU, arrondi exact : cos vaut 1. Vulkan et WGSL admettent 2,5 ULP d'erreur sur une division, et une
  racine qui hérite de celle de `inversesqrt` : sur le pilote NVIDIA, cos sort parfois au-dessus de 1, et
  1 − cos² sous zéro : NaN. Des compteurs temporaires écrits dans un canal libre de la table (retirés depuis)
  l'ont montré : des échantillons NaN sur la 4070, d'autant plus que la surface est lisse, aucun sous lavapipe ;
  leur proportion change avec le code compilé autour. Sur NVIDIA, `saturate(NaN)` rend 0 (l'échantillon est
  perdu), et une cubemap lue dans une direction NaN rend une valeur quelconque.
- **Parade** : ne jamais tirer une racine d'une différence qui peut passer sous zéro d'un ULP ; calculer la
  grandeur par sa propre formule (`ggxHalfVectorOf` : sin² = α² y / d, pas 1 − cos²). Un test GPU vert sous
  lavapipe ne prouve rien de la précision : le relancer sur un vrai GPU (la 4070, ADR-0035) :
  `build/windows-debug/tests/levain_environment.exe vulkan`, puis `webgpu`, lancés de la distro, à chaque PR qui
  touche un shader de calcul de l'IBL ; aucune CI ne le fait (lavapipe arrondit exactement).

## Windows compilé depuis Linux par clang-cl : les pièges de l'essai et de la chaîne (2026-10-08)

Branche `spike/windows`, `prototypes/windows/README.md` ; ADR-0035.

- **`CMAKE_TOOLCHAIN_FILE` n'est pas défini dans un `try_compile`** : une toolchain chargée par vcpkg
  (`VCPKG_CHAINLOAD_TOOLCHAIN_FILE`) qui en déduit le dossier de vcpkg échoue dès la détection du compilateur.
  Parade : `CMAKE_PARENT_LIST_FILE`, le `vcpkg.cmake` qui l'inclut.
- **vcpkg n'ôte `/MP` que pour un compilateur nommé `clang-cl.exe`** : sous Linux, il s'appelle `clang-cl`. clang-cl
  ignore `/MP` en le signalant, et ktx, compilé en `-Werror`, s'arrête (« argument unused during compilation »).
  Parade : la toolchain retire `/MP` des options.
- **clang-cl lit `-Wall` comme `/Wall`, c'est-à-dire `-Weverything`** : un projet qui teste
  `CMAKE_CXX_COMPILER_ID` (« Clang » sous clang-cl) pour ajouter `-Wall -Werror` se compile avec tous les
  avertissements, en erreurs (ozz : « `_Ty` est réservé » ; spirv-reflect). Parade : nos options passent par
  `/clang:` ; un port qui casse reçoit un port overlay (pour ozz : sa branche MSVC, qui définit
  `_CRT_SECURE_NO_WARNINGS` pour ses seules sources ; pour spirv-reflect, dépendance des couches de validation,
  ses deux programmes, seuls compilés en `-Werror` et dont rien ne se sert, ne sont pas compilés). Les couches
  elles-mêmes se compilent sans parade : leur `-Werror` est facultatif (`BUILD_WERROR`, éteint), mais clang-cl les
  avertit en `-Weverything` : 1,1 million d'avertissements (`grep -c 'warning:'`), 5,2 millions de lignes et 715 Mo pour
  le seul journal Debug de vcpkg, sans effet ; `~/vcpkg/buildtrees/vulkan-validationlayers` pèse 3,8 Go, à effacer une
  fois le paquet dans le cache binaire.
- **ozz choisit sa CRT** sur sa branche MSVC, statique par défaut : lld-link refuse de le lier au reste
  (`/failifmismatch` sur `RuntimeLibrary`). Parade : `ozz_build_msvc_rt_dll` suit le triplet.
- **Modifier la toolchain change l'ABI de chaque port** : vcpkg recompile toutes les dépendances Windows, Dawn
  compris (8 min sur 24 tâches).
- **Dawn ne trouve pas `vulkan-1.dll`** (« Windows Error: 87 ») : il cherche à côté de lui et de l'exe, puis sans
  chemin avec `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`, qui exige un chemin complet. Il retombe en silence sur son backend
  Null, et les tests qui ne relisent pas d'image passent (six sur neuf, avant que `createWebGpuDevice` ne refuse ce
  backend en le nommant, #345 ; sans la ligne `System32`, 15 tests ou programmes échouent). Parade : lui donner
  `System32` (`DawnInstanceDescriptor::additionalRuntimeSearchPaths`, terminé par `\`). Son backend D3D12, lui, veut
  copier `d3dcompiler_47.dll` d'un SDK lu dans le registre, et compiler DXC.
- **Un programme Windows reçoit argv dans la page de code ANSI** : `ctest` ne trouvait pas les cas de test
  accentués (« test cases: 0 », que nos tests refusent). Parade : un manifeste qui met le processus en UTF-8
  (`activeCodePage`).
- **Lancé depuis WSL, un exe lit un chemin Linux absolu** (`/home/…`) sous la racine de la distro, son dossier
  courant étant `\\wsl.localhost\levain-dev\…` : les chemins compilés dans les tests marchent là, pas sur un
  runner Windows. Windows ne suit pas un lien symbolique de la distro : copier les assets, pas les lier.
- **Un `std::filesystem::path` passé à une bibliothèque C** (`path.c_str()`) ne compile pas sous Windows : c'est
  un `wchar_t*`. Sous Linux, rien ne le signale. Parade : `core::pathForC(path).c_str()`.
- **`std::getenv` est « unsafe » pour la CRT de Microsoft**, une erreur sous `-Werror`. Parade :
  `core::environmentVariable`, jamais `_CRT_SECURE_NO_WARNINGS` (règle n°4).
- **Le manifeste UTF-8 casse l'édition de liens en Debug** (« /manifestinput: requires /manifest:embed ») : CMake
  met `/INCREMENTAL` en Debug (le défaut de MSVC), lie alors en deux passes (`cmake -E vs_link_exe`) et ajoute son
  `/MANIFEST /MANIFESTFILE` après notre `/MANIFEST:EMBED`, qu'il annule. L'essai, en Release, ne l'avait pas vu.
  Parade (`CMakeLists.txt`) : `/INCREMENTAL:NO` ; lld-link ne lie jamais en incrémental.
- **Hors de Windows, clang-cl annonce `_MSC_VER` 1933** (il ne lit pas celui de cl.exe) et prend la STL et le SDK
  les plus récents du winsysroot. Parade : la toolchain fige les versions (`/vctoolsversion`, `/winsdkversion`) et
  annonce celle de la STL (`-fms-compatibility-version=19.51`).
- **Une toolchain modifiée ne change pas les options d'un dossier de build qui existe** : `windows.cmake` de vcpkg
  les pose en cache sans `FORCE`. Les versions figées entraient dans les ports (dossiers neufs) mais pas dans
  Levain, sans un mot ; vu en lisant `compile_commands.json`. Parade : la toolchain efface ces entrées du cache
  avant d'inclure `windows.cmake`, **une seule fois par configuration** (`_VCPKG_WINDOWS_TOOLCHAIN`) : à la
  deuxième inclusion, `windows.cmake` ne les repose plus, et le winsysroot disparaissait (« 'windows.h' file not
  found » dans les ports).
- **La découverte des cas de doctest lance l'exe pendant le build** : l'interop de WSL l'exécute, un runner Linux
  non. `doctest_discover_tests` n'a pas le `DISCOVERY_MODE PRE_TEST` de `gtest_discover_tests`. Parade
  (`tests/CMakeLists.txt`) : en compilation croisée hors Emscripten, un fichier que ctest inclut rejoue le script
  de doctest à chaque lecture de la liste ; si la découverte échoue, le test rouge `levain_tests_NOT_DISCOVERED`
  le dit, sans quoi ctest sortirait en 0 sur « No tests were found » (2026-10-08).
- **Le chargeur Vulkan de Windows ne cherche pas les couches de validation à côté de l'exe** : il lit d'abord
  `VK_ADD_LAYER_PATH`, puis le registre ; les couches du port passent ainsi devant celles d'un SDK inscrit au
  registre. Il ignore `VK_ADD_LAYER_PATH` quand `VK_LAYER_PATH` est posée, et les deux dans un processus lancé en
  administrateur. Sans elle, le refus « couches de validation Vulkan absentes » (vk-bootstrap,
  `requested_layers_not_present`), même avec les fichiers du port à côté de l'exe. Parade : le build copie
  `VkLayer_khronos_validation.dll` et son `.json` (ceux de la Release, 26 Mo, aussi pour un exe Debug : le chargeur les
  charge dans leur propre CRT, et ceux du Debug en pèsent 46) à côté de chaque exe qui lie `levain_gpu`, par un
  parcours de fin de configuration (`cmake/LevainVulkanLayers.cmake`) : une liste d'exes tenue à la main s'oublierait ;
  `device_vk.cpp` pose `VK_ADD_LAYER_PATH` sur `SDL_GetBasePath()`, sauf si elle l'est déjà. Preuve qu'elles sont
  chargées : `(Get-Process levain_sandbox).Modules` sous `powershell.exe` pendant un lancement (2026-10-08).
- **Un PDB nomme les sources par leur chemin dans la distro** (`/home/…`), introuvable pour Visual Studio. Parade :
  `-fdebug-prefix-map` (pas `-ffile-prefix-map`, qui changerait `__FILE__`), vers `//wsl.localhost/<distro>/…`, que
  Windows lit comme `\\wsl.localhost\<distro>\…`. Pas `\\wsl.localhost\…` : LLVM récrit un `\\` initial en `\` dans le
  CodeView (et un chemin tout en barres obliques, `/…`, reste tel quel), ce qui perd le préfixe UNC ; mesuré avec
  `llvm-pdbutil dump -files`. Les sources des ports et les STL gardent leur chemin d'origine (2026-10-08).
- **Le loader signale `SocialClubVulkanLayer.json` introuvable** sur le portable de Donnovan (une couche implicite
  de Rockstar, dans le registre) : un message du loader, pas une erreur de validation ; il ne touche pas
  `isValidationError` (device_vk.cpp) et n'arrête rien (2026-10-08).
- **Un exe Windows lancé depuis WSL ne reçoit une variable d'environnement que si `WSLENV` la nomme**
  (`VAR=valeur WSLENV=VAR programme.exe` ; pas `VAR/u`, qui ne vaut que de Windows vers WSL). La propriété
  `ENVIRONMENT` de ctest n'atteint donc pas l'exe : lancés de la distro, `gpu.*` et `smoke.*` ne reçoivent pas
  `SDL_VIDEO_DRIVER=offscreen` et tournent dans une vraie fenêtre. Avec la variable transmise, comme sur un runner
  Windows, les programmes Vulkan échouent sur la 4070 : le pilote NVIDIA n'a pas `VK_EXT_headless_surface`, que le
  pilote offscreen de SDL demande ; le chemin WebGPU passe. Réglé par la CI (#346) : sous Windows, les tests
  demandent le pilote `windows` (`testVideo`, `tests/CMakeLists.txt`) (2026-10-08).
- **`waitEvents` revient avant l'échéance, sous Windows lancé depuis la distro** : le test échouait en `ctest -j`
  (`elapsed >= 40ms` vu à 11 ms, sans autre sortie). Cause mesurée, un `fprintf` dans `waitEvents` : sans
  `SDL_VIDEO_DRIVER` (WSLENV, ci-dessus), la fenêtre s'ouvre sur le vrai bureau, qui lui envoie
  `SDL_EVENT_WINDOW_FOCUS_LOST` ou `SDL_EVENT_MOUSE_ADDED` ; `SDL_WaitEventTimeout` rend la main au premier événement,
  même un que le moteur ne traduit pas (`events.window` reste vide). 11 échecs sur 40 lancements à 8 en parallèle ;
  0 sur 80 en offscreen (depuis la distro, dans `build/windows-debug/tests` : 5 tours de 8
  `./levain_tests.exe -tc="waitEvents*" &` puis `wait`, en comptant les codes non nuls ; offscreen :
  `SDL_VIDEO_DRIVER=offscreen WSLENV=SDL_VIDEO_DRIVER` devant l'exe). Le moteur est juste (« jusqu'au premier
  événement », et la boucle de `app.cpp` recalcule son reste) ; c'est le test qui dépendait de l'environnement : il
  pose le pilote offscreen lui-même et le vérifie.
- **Les symboles d'un exe Windows se lisent dans le PDB** : `llvm-pdbutil dump -publics`, noms décorés à la MSVC, que
  `llvm-undname` démêle. Chercher `@editor@levain@@` dans la forme décorée en rate : un nom déjà écrit dans le symbole y
  devient un rang (108 symboles décorés contre 112 démêlés pour `levain::editor::`). `llvm-undname` sort en code 1
  dès qu'un nom n'est pas décoré (les symboles C) : ne pas lire son code (`tests/check_pdb_symbols.cmake`) (2026-10-08).
- **Un arbre de build déjà configuré ignore une nouvelle toolchain** : les `cmake.plugins.*` configurent un mini-moteur
  dans `build/<preset>/tests/plugin_boundaries/`, et la détection du compilateur y échouait (« unable to disambiguate:
  -nologo ») après un passage sans toolchain. `tests/CMakeLists.txt` efface ces dossiers à chaque configuration.
- **Le hot-reload des shaders d'un exe Windows passe par `wsl.exe`, qui a quatre pièges** (ADR-0035, décision 5 ; preuve
  sur la 4070 : `sandbox/` Debug lancé de la distro, un commentaire ajouté à `shaders/mesh.slang`, « recompilés en
  906 ms » puis « pipeline des meshes recréé en 11.0 ms », cinq fois de 898 à 944 ms ; le build seul dure 830 ms (823 à 853) dans
  la distro et `wsl.exe --exec true` 100 ms : `touch shaders/mesh.slang` puis `cmake --build build/windows-debug
  --target levain_shaders`, cinq tours, avec et sans `wsl.exe -d levain-dev --exec`) (2026-10-08).
  - **`wsl.exe -d <distro> -- cmd args` passe par le shell de la distro** : un « ; » y coupe la commande, un « $ » ou
    un « \\ » s'y interprètent (`-- /usr/bin/printf '[%s]' 'a b' 'c;d'` rend « [a b][c] », puis « d: command not
    found ») ; un espace, lui, passe. Parade : `--exec`, qui passe chaque argument tel quel (`shaderReloadCommand`).
  - **Ses propres messages sont en UTF-16** (distro inconnue : code de sortie 255, « Il n'existe aucune distribution
    avec le nom fourni. Code d'erreur : Wsl/Service/WSL_E_DISTRO_NOT_FOUND »), un octet nul entre les lettres dans le
    log. Parade : `WSL_UTF8=1` dans l'environnement de `wsl.exe` (`runProcess` sait l'ajouter) ; mesuré, ses messages
    arrivent alors en UTF-8, comme la sortie des processus de la distro.
  - **`--exec` ne lit pas `~/.bashrc`** : `LEVAIN_WINSYSROOT` y manque au `cmake` relancé, et un arbre `windows-*` à
    régénérer (un `CMakeLists.txt` modifié depuis le dernier build) s'arrêterait sur la toolchain. Parade : la commande
    passe par `/usr/bin/env LEVAIN_WINSYSROOT=<celui de la configuration>`.
  - **Un exe lancé de la distro ne reçoit pas `WSL_DISTRO_NAME`** (WSLENV, plus haut) : la distro est écrite dans le
    binaire à la configuration (`LEVAIN_WSL_DISTRO`, dans `levain_app` : une propriété de l'arbre de build, pas du
    programme). Un exe compilé hors d'une distro n'en a pas, et
    refuse. Le dossier des sources à surveiller (`/home/…/shaders`) se lit par le dossier courant `\\wsl.localhost\…` :
    la surveillance par date de modification marche à travers ce partage (10 à 63 ms entre l'écriture et la détection),
    mais lancé d'un dossier Windows, l'exe s'arrête avant, sur ses assets (« racine d'assets introuvable ») ; si les
    sources seules manquent, le programme le dit (« aucune source .slang lisible »).

## `enable_testing()` après un `add_subdirectory` : les `add_test` du dossier se perdent sans un mot (2026-10-08)

Corrigé par la PR de #354 (2026-10-08) : le piège reste, sa parade est en place.

- **Symptôme** : build/SKILL.md annonçait un test `dxil.*` par shader ; `ctest -N -R '^dxil[.]' | tail -1` rend
  « Total Tests: 0 », sur tous les presets, depuis le début (`ctest -N | grep -c dxil` compte aussi l'en-tête « Test
  project … » quand le chemin du build contient dxil, comme un worktree). Vu en comptant les tests par commande pour le label `host` (#345).
- **Cause** : le `CMakeLists.txt` racine appelait `enable_testing()` après `add_subdirectory(shaders)` et celui des
  plugins. Un dossier configuré avant n'écrit pas de `CTestTestfile.cmake` : ses `add_test` réussissent, ne mènent
  nulle part, et la propriété `TESTS` du dossier les liste quand même (mesuré : elle ne trahit pas la perte).
- **Parade** : `enable_testing()` en tête des `add_subdirectory` du `CMakeLists.txt` racine, et
  `levain_require_testing_enabled()` (`cmake/LevainShaders.cmake`) dans `levain_add_shader` : elle refuse la
  configuration si `CMAKE_TESTING_ENABLED`, que pose `enable_testing()`, manque au dossier. Contre-testé en replaçant
  `enable_testing()` après `add_subdirectory(shaders)` : la configuration s'arrête en nommant le dossier. Les 27 tests
  `dxil.*` s'enregistrent sur les presets natifs (pas sur le web : son garde-fou « au moins 80 tests » compte le code
  CPU), avec le label `host`. Cette garde surveille l'ordre, pas le nombre : l'étape « Tests » de la CI native
  refuse aussi moins de 27 tests `dxil.*` (`ctest -N -R '^dxil[.]'`), qu'un `add_test` perdu ferait tomber à 0.
- **Contre-test dans un arbre déjà configuré** : un `CTestTestfile.cmake` déjà écrit reste dans l'arbre, et ctest
  compte encore les 27 tests après une configuration refusée (ou après le retrait de `enable_testing()` et de la
  garde) : le contre-test se fait dans un dossier neuf, `cmake --preset linux-debug -B build/<nom>`, ou en
  effaçant ceux de `shaders/` et de `plugins/*/shaders/`.

## 16 Go de RAM : des builds en parallèle font planter la machine de référence (2026-10-08)

- **Symptôme** : le PC de Donnovan se fige et redémarre, plusieurs fois dans la nuit et la matinée, pendant que la
  session de M7.2 travaillait.
- **Cause** : jusqu'à trois builds à la fois (trois worktrees, des relecteurs de workflow qui compilaient en
  parallèle, des presets ASan), chacun à toutes les tâches de ninja (18 sur cette machine : nproc + 2). Une unité
  de traduction lourde (flecs.h, Jolt, ImGui, NVRHI) prend environ 1 Go ; la machine en a 14 utilisables, sans
  plafond de mémoire sur la distrobox : le bureau meurt avant le build.
- **Parade** : un build à la fois, jamais deux ; `tools/verify.sh` limite à 4 tâches de compilation, de ports vcpkg
  et de tests (`CMAKE_BUILD_PARALLEL_LEVEL`, `VCPKG_MAX_CONCURRENCY`, `LEVAIN_TEST_JOBS`) ; hors du script, `export
  CMAKE_BUILD_PARALLEL_LEVEL=4 VCPKG_MAX_CONCURRENCY=4` avant `cmake --preset` et `cmake --build` ; les relecteurs
  d'un workflow, l'un après l'autre. Mesuré sur deux heures de builds à 4 tâches, un à la fois (le workflow de
  l'inspecteur, un relevé de `free -m` toutes les 15 s) : 3,5 Go disponibles au plus bas, pendant une édition de
  liens, et jusqu'à 3,4 Go de swap. 8 tâches ne tiendraient sans doute pas sur cette machine. Un plafond de mémoire
  sur la distrobox (`podman update --memory 10g --memory-swap 12g dev-ubuntu`) ferait tuer le build plutôt que le
  PC : réglage du système, à la main de Donnovan.

## Une regex sur un message de CMake : son repli dépend du chemin (2026-10-08)

- **Symptôme** : `cmake.plugins.no-editor` vert en local, rouge sur les trois jobs de la CI de #335.
- **Cause** : CMake replie son message d'erreur selon sa longueur, donc selon le chemin du dépôt. Sur le runner
  (`/home/runner/work/…`), « : le » restait sur la ligne du chemin et « contrôle » passait à la suivante ; la regex
  avait été écrite sur le repli local.
- **Parade** : dans une regex sur un message de CMake, chaque espace peut être un retour à la ligne
  (`[\n ]+`) ; la vérifier contre la sortie de la CI et contre la sortie locale (un `string(REGEX MATCH)` dans un
  script `cmake -P` suffit).

## `PASS_REGULAR_EXPRESSION` : CTest ne lit plus le code de sortie (2026-10-07)

- **Symptôme** : les tests des refus de la réflexion (#327) restaient verts quand le refus écrivait son message
  puis laissait le programme continuer : la règle n°7 n'était vérifiée qu'à moitié.
- **Cause** : avec `PASS_REGULAR_EXPRESSION`, CTest juge le test sur sa seule sortie et ignore le code de sortie.
- **Parade** : le programme de test écrit une ligne témoin s'il arrive au bout (« aucun refus »), et le test la
  refuse par `FAIL_REGULAR_EXPRESSION`. Jamais `WILL_FAIL`, qui passe sur n'importe quel code de sortie non
  nul, quel que soit le message.

## Un arrêt par `std::abort` : un plantage pour CTest, quelle que soit la sortie (2026-10-07)

- **Symptôme** : « Subprocess aborted » sur un test de refus dont la sortie contenait pourtant le message attendu
  par `PASS_REGULAR_EXPRESSION`.
- **Cause** : CTest compte un processus tué par un signal (`SIGABRT`) comme un échec avant de lire la sortie.
- **Parade** : un refus attendu sort par `std::_Exit(EXIT_FAILURE)` après le journal (`exitOnRefusedDescription`,
  `engine/scene/src/reflection.cpp`) : spdlog a déjà vidé sa sortie, et le code de sortie reste un échec.

## Une feuille glm redécrite à la main : flecs réinterprète les bits (2026-10-07, règle de relecture)

- **Symptôme** : un champ `glm::vec3` lu et écrit à d'autres places que celles de la struct, sans erreur
  (études de l'ADR-0034, flecs 4.1.6).
- **Cause** : flecs garde la dernière description d'un type. Une feuille glm n'est pas un agrégat :
  `reflection.hpp` ne la lit pas, et ne peut pas voir une seconde description.
- **Parade** : les feuilles glm se décrivent une fois, dans `SceneModule` (`engine/scene/src/scene.cpp`) ; une
  nouvelle (`glm::vec4`) s'y ajoute. À vérifier en relecture.

## Une référence d'entité en `flecs::entity_t` : un nombre, perdu au rechargement (2026-10-07, règle de relecture)

- **Symptôme** : `"target":496` au lieu de `"target":"player"` dans le JSON ; une autre entité, ou aucune,
  après un rechargement.
- **Cause** : un `flecs::entity_t` est un entier, que rien ne distingue d'un compteur ; un `flecs::entity`
  s'écrit par son chemin.
- **Parade** : un champ qui désigne une entité est un `flecs::entity`. Exception connue : `CharacterGround::body`,
  en lecture seule (les en-têtes de physics ne voient pas flecs). À vérifier en relecture.

## Une valeur d'enum hors de ses constantes : flecs vide tout le JSON de l'entité (2026-10-07, règle de relecture)

- **Symptôme** : `to_json` rend une chaîne vide pour toute l'entité, avec « enumeration value '3' … is not a
  valid constant » au journal.
- **Cause** : flecs écrit une enum par le nom de sa valeur, et ne lit les constantes que de 0 à 126
  (`FLECS_ENUM_MAX`), plus les puissances de deux. Une combinaison de drapeaux (`A | B`), une constante négative
  ou au-delà de 126 (hors puissance de deux), ou une enum sans constante (`std::byte`) n'ont pas de nom. Une enum
  dont seules certaines constantes sont lues passe l'import (`hasReflection` veut une constante au moins) :
  l'inspecteur montre « ? » pour les autres et ne peut pas les choisir.
- **Parade** : une enum de composant ne prend que ses constantes déclarées, entre 0 et 126 ; ni drapeaux ni
  `std::byte`. Une enum sans constante est refusée à l'import (`hasReflection`) ; le reste se vérifie en relecture.

## basisu refuse de compiler sur `ubuntu-26.04` : le GCC de la variante amd64v3 vise x86-64-v3 (2026-10-07)

- **Symptôme** : vcpkg échoue en construisant ktx sur le runner `ubuntu-26.04`, sur `basisu_kernels_sse.cpp:27:
  #error Please check your compiler options`, alors que la ligne de compilation ne porte que `-msse4.1`. Suit
  un « CMake was unable to find a build program corresponding to "Ninja" » : CMake abandonne dans `project()`
  après l'échec de vcpkg, ninja est bien là.
- **Cause** : l'image du runner active la variante amd64v3 des paquets d'Ubuntu, dont le GCC vise
  `-march=x86-64-v3` par défaut : `__AVX__` est défini, et basisu refuse de compiler ses noyaux SSE sous AVX.
  La distrobox, sans la variante, vise `x86-64` : le même port compile en local.
- **Parade** : `triplets/x64-linux.cmake` pose `-march=x86-64` pour tous les ports (ADR-0033). Pour
  reproduire : `ubuntu:26.04`, `APT::Architecture-Variants "amd64v3";` dans `/etc/apt/apt.conf.d/`,
  `apt-get update && apt-get install g++`, puis `g++ -Q --help=target | grep march=`.

## `nm | grep -q` sous `pipefail` : un symbole trouvé, et pourtant un échec (2026-10-07)

- **Symptôme** : en préparant l'étape du build profilé (#298), `nm -C levain_sandbox | grep -q "tracy::"` échoue
  sous `set -euo pipefail`, alors que le binaire contient 488 symboles `tracy::`.
- **Cause** : `grep -q` sort dès la première ligne trouvée et ferme le tube ; `nm`, qui écrit encore, meurt de
  SIGPIPE (code 141). Avec `pipefail`, ce code est celui du tube entier : trouver le symbole fait échouer. Seule
  une sortie plus grande que le tampon du tube (64 Kio) y est exposée : `$t --version | grep -q` ne l'est pas, et
  le `nm | grep -q` de l'étape ASan ne tient que parce que cette étape tourne sans `pipefail`. Y ajouter
  `set -euo pipefail` la ferait échouer.
- **Parade** : lire la sortie d'abord (`symbols=$(nm -C …)`), puis `grep -q … <<< "$symbols"`. Ou `grep -c`,
  qui lit tout. Un contrôle qui échoue à tort finit désactivé : ce n'est pas mieux qu'un contrôle muet.

## Deux worktrees, deux caches d'assets : des captures qui diffèrent sans que le code change (2026-10-06)

- **Symptôme** : en sortant la boucle du sandbox (#300), les captures de l'ancien build et du nouveau, prises
  avec les mêmes options, diffèrent de 1 à 8 niveaux sur les bords des modèles glTF ; celles sans modèle sont
  identiques à l'octet.
- **Cause** : les deux builds venaient de deux worktrees, chacun avec son `assets-cache/`. L'un avait les
  textures cuites en BC7 (`.cooked/`, ADR-0020), l'autre non : les mêmes modèles n'avaient pas les mêmes texels.
- **Parade** : comparer deux builds sur les **mêmes** assets. Écarter le `.cooked/` de l'un le temps de la
  comparaison (le déplacer, puis le remettre), ou cuire les deux. Avec des assets identiques, les captures du
  sandbox sont identiques à l'octet d'un lancement à l'autre.

## tracy-csvexport range les zones par emplacement, pas par nom (2026-10-06)

- **Symptôme** : des zones renommées à l'exécution (`ZoneScoped` puis `ZoneName`) sortent toutes sous le nom de
  leur fonction (`runStage`, 13 appels par image), sans leur nom.
- **Cause** : `tracy-csvexport` groupe par emplacement dans le source ; `ZoneName` ne change que l'affichage du
  profileur.
- **Parade** : une zone « transitoire » (`ZoneTransientN`, notre `LEVAIN_PROFILE_SCOPE_TEXT`), dont l'emplacement
  porte le nom : Tracy les regroupe par nom, et csvexport aussi. Le nom doit être unique par fonction
  (« ombres/terrain », pas « terrain », qu'on retrouve dans deux étapes).

## Une correction de relecture poussée sans le contrôle de format : la CI passe au rouge (2026-10-06)

- **Symptôme** : #292 et #293, empilées, rouges sur l'étape « Format » de linux-debug, alors que la
  vérification complète de la pile était verte.
- **Cause** : la vérification avait tourné avant la correction de relecture. Celle-ci a reformaté les fichiers
  qu'elle croyait toucher, mais un commentaire modifié par script dans un autre fichier (`lake_shore.hpp`)
  dépassait la largeur.
- **Parade** : avant chaque push, même d'une petite correction, relancer le contrôle de la CI sur tout l'arbre :
  `find editor engine plugins sandbox tests tools -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run
  --Werror`.

## meshoptimizer, comme Jolt, n'a ses assertions qu'en Debug (2026-10-05)

- **Symptôme** : aucun, tant que les données sont saines ; la relecture de la collision du décor (M6.3) l'a
  relevé avant qu'un fichier abîmé ne le montre.
- **Cause** : meshoptimizer vérifie ses entrées par `assert`, que `NDEBUG` retire. Un indice au-delà des
  sommets, ou un nombre d'indices qui n'est pas un multiple de 3, devient en Release une lecture hors bornes.
- **Parade** : vérifier une fois au chargement, glTF ou cuit (`whyNotAValidModel`), et rendre une erreur ; puis
  seulement des `LEVAIN_ASSERT` là où la bibliothèque est appelée. Même règle que pour Jolt.

## Un « ; » dans le nom d'un TEST_CASE : deux tests verts qui ne testent rien (2026-10-05)

- **Symptôme** : `ctest -N` liste un cas coupé en deux entrées (« … le retire du monde » et « détruire
  « aucun corps » … ») ; les deux passent en 0,00 s, et doctest annonce `test cases: 0 | 0 passed`. Le test,
  écrit ainsi dès #263, n'avait jamais tourné en CI.
- **Cause** : `doctest_discover_tests` passe les noms des cas à CMake, pour qui « ; » sépare les éléments
  d'une liste. Chaque moitié devient un filtre `--test-case=` qui ne retrouve rien, et doctest sort sans
  erreur quand aucun cas ne correspond.
- **Parade** : ni « ; » ni crochet non refermé dans un nom de cas (entre crochets, CMake ne sépare plus : un
  « [ » ouvert fondrait les cas suivants en une seule entrée) ; et `FAIL_REGULAR_EXPRESSION
  "test cases: 0 [|]"` sur les tests découverts (`tests/CMakeLists.txt`) : une entrée qui n'exécute rien
  échoue (règle n°7). `[|]` et non `|`, qui est une alternance en regex CMake et reconnaîtrait toute sortie.
  Contre-test : le nom remis avec son « ; », les deux entrées rougissent.

## LeakSanitizer et lavapipe installé à côté de RADV : des fuites dans un « module inconnu » (2026-10-05)

- **Symptôme** : sous ASan, tous les tests GPU échouent d'un coup sur 128 à 256 octets perdus, dont la pile
  finit dans `<unknown module>` sous `libvulkan.so.1`, alors que `LD_PRELOAD=/usr/lib/libvulkan_radeon.so` (sous
  Arch) est posé (entrée « LeakSanitizer et RADV » plus bas).
- **Cause** : lavapipe (`vulkan-swrast`), installé dans la distrobox Arch pour reproduire le pilote de la CI.
  Le loader le charge avec les autres pilotes pour les énumérer, puis le décharge, puisqu'il n'est pas
  préchargé : sa globale paraît perdue, le même faux positif que celui de RADV. Les pilotes Intel, présents
  avant lui, ne le provoquaient pas. Sous Ubuntu, `mesa-vulkan-drivers` installe d'office les huit pilotes de
  Mesa, lavapipe compris : la parade y devient obligatoire pour chaque lancement ASan sur la machine de
  référence.
- **Parade** : un seul pilote, et préchargé : `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json` avec
  `LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libvulkan_radeon.so` (`/usr/lib/` sous Arch), ou `lvp_icd.json` avec
  `libvulkan_lvp.so` pour lancer les tests de fumée comme la CI (`VK_DRIVER_FILES` seul pour un build sans
  sanitizer). Vérifié dans `dev-ubuntu` le 2026-10-07 (`linux-asan`, `ctest`) : sans la parade, 25 tests sur
  285 échouent sur la même fuite de 128 octets ; avec l'un ou l'autre pilote préchargé, 285 sur 285 passent.
  La CI `ubuntu-26.04` y tombe aussi (15 tests, des fuites indirectes de 2 × 56 octets nées d'un thread du
  pilote) : `ci.yml` pose `VK_DRIVER_FILES` sur lavapipe pour tous les presets, et `LD_PRELOAD` sur les
  étapes Vulkan de `linux-asan`.

## Jolt n'a ses assertions qu'en Debug (2026-10-04, corrigé le 2026-10-05)

- **Symptôme** : en Release, une masse nulle, un rayon négatif, un quaternion non normalisé ou `destroyBody`
  sur le corps invalide passent sans un mot ; les NaN ou la corruption arrivent plus loin.
- **Cause** : `JPH_ENABLE_ASSERTS` n'est pas exporté par le port vcpkg, mais Jolt le déduit lui-même :
  sans `NDEBUG`, `Core.h` définit `JPH_DEBUG`, et `IssueReporting.h` active les assertions. Le Debug du
  moteur et la bibliothèque Debug de vcpkg les ont donc tous deux, le Release aucun des deux, et
  `JPH_VERSION_ID`, qui les compte, concorde des deux côtés. Une première version de cette entrée, écrite
  d'après une relecture qui ne lisait que `JoltConfig.cmake`, disait « aucune assertion, même en Debug » :
  c'était faux, vu en compilant un essai sans `NDEBUG` contre la bibliothèque Release (`AssertFailed`
  indéfini à l'édition de liens).
- **Parade** : le moteur valide lui-même ce qu'il donne à Jolt (`whyNotThisShape`, `whyNotThisLayer`, le corps
  invalide dans `destroyBody`, `toJoltRotation` qui renormalise), pour le Release ; en Debug,
  `JPH::AssertFailed` écrit dans notre journal avant de s'arrêter dans le débogueur.

## `tools/web-smoke.sh` : ni Firefox dans la distrobox, ni Node sur l'hôte (2026-10-04)

- **Symptôme** : « firefox introuvable », dans la distrobox comme sur l'hôte.
- **Cause** : Firefox n'existe qu'en Flatpak (`org.mozilla.firefox`), sur l'hôte ; Node n'est que dans la
  distrobox. Un Flatpak a son propre `/tmp` : un profil créé par `mktemp -d` lui est invisible.
- **Parade**, à la main, les deux côtés partageant le réseau : sur l'hôte, `python3 -m http.server 8765 --bind
  127.0.0.1 --directory build/web` et `flatpak run org.mozilla.firefox --headless --no-remote --profile
  ~/.var/app/org.mozilla.firefox/levain-smoke-profile --remote-debugging-port 9222` (le `user.js` du script
  dans ce profil) ; dans la distrobox, `node tools/web-smoke.mjs <url> <référence|-> <capture.png> 9222`. Pour
  arrêter Firefox, le PID de `flatpak run` ne suffit pas : tuer le processus `/app/lib/firefox/firefox` dont la
  ligne de commande porte ce profil, jamais `flatpak kill`, qui fermerait aussi le Firefox de Donnovan.

## La distrobox est passée à LLVM 23, la CI est restée à 22 (2026-10-04, aligné le 2026-10-05)

- **Symptôme** : clang-tidy signale en local des fichiers que la CI accepte (`bugprone-signed-bitwise`,
  `bugprone-unchecked-optional-access` dans `culling_test.cpp` et `main.cpp`, jamais modifiés).
- **Cause** : Arch a mis la distrobox à clang 23.1 ; `LLVM_VERSION` valait 22 dans `ci.yml`. clang-format et
  clang-tidy changent de règles d'une version majeure à l'autre (piège déjà vu en M0).
- **Parade** : vérifier avec les outils de la CI, sans toucher au système : `python3 -m venv <dossier>`, puis
  `pip install "clang-format>=N,<N+1" "clang-tidy>=N,<N+1"` pour la version N de la CI, et les lancer dans la distrobox. Aligner les
  deux versions : la règle écrite dans `ci.yml` veut que la CI suive la machine de référence. Fait le
  2026-10-05 : la CI est en LLVM 23. `bugprone-signed-bitwise`, nouveau en 23, refuse aussi un **littéral
  signé** comme décalage ou masque (`h >> 15`, `x & 0xff`) : écrire `15u`, `0xffu`. Au prochain écart de
  version, la même parade.

## « clang++ : commande introuvable » : le système hôte est immuable (2026-10-04)

- **Symptôme** : sur la machine de référence, `clang++`, `cmake --preset` ou `clang-format` échouent depuis le
  shell de l'agent, alors que `build/linux-debug/CMakeCache.txt` désigne bien `clang++`.
- **Cause** : l'hôte est une Fedora Atomic (`ogc`), sans chaîne de compilation. Tout s'outille dans la
  distrobox `dev-ubuntu` (Ubuntu 26.04, la même version que la CI, depuis le 2026-10-07 ; avant, la boîte
  Arch `dev`) : clang 23, CMake 4.2, vcpkg et emsdk y sont, avec le même `$HOME`.
- **Parade** : `distrobox enter dev-ubuntu -- bash -lc 'cd <dépôt> && <commande>'`. Une variable du shell de
  l'agent n'y passe pas : écrire les chemins en toutes lettres dans la commande. `~/.local/bin` exporte de la
  boîte `cmake`, `ninja`, `clangd`, `gdb`, `lldb`, `lldb-dap`, `gh` et `git-lfs` : `cmake --preset` marche
  depuis l'hôte, mais pas `ctest`, `clang++`, `clang-format` ni `clang-tidy`, qui passent par la commande
  ci-dessus. `git`, `curl` et les scripts de `tools/` qui ne compilent rien tournent aussi sur l'hôte. Les
  assets de test (`assets-cache/`) ne sont pas partagés entre worktrees : `tools/fetch-assets.sh` dans chacun.

## Une lecture de texture dans une branche : refusée en WGSL (2026-10-03)

- **Symptôme** : le shader du terrain compile en SPIR-V et tourne sur Vulkan, mais Dawn le refuse :
  « `textureSample` must only be called from uniform control flow ».
- **Cause** : `Sample` calcule ses dérivées à partir des pixels voisins. WGSL l'interdit dans une branche qui
  dépend des données (sauter une couche de poids nul), où les voisins peuvent ne pas l'exécuter.
- **Parade** : les dérivées avant la branche (`ddx`, `ddy`), puis `SampleGrad` dedans, ou `SampleLevel`.

## Un binding set partagé par deux pipelines : un seul layout en WebGPU (2026-10-03)

- **Symptôme** : « Bind group layout … does not match layout … of bind group », en WebGPU seulement.
- **Cause** : le backend déduit le type d'une texture (tableau, cube, profondeur) du WGSL de chaque pipeline.
  Un pipeline dont le shader n'utilise pas un binding, comme l'ombre du terrain qui ignore ses couches, le voit
  en texture 2D ordinaire, et ne correspond plus au binding set.
- **Parade** : à chaque pipeline, un layout réduit à ce que son shader lit, et son binding set.

## Hors écran, rien ne freine la boucle : la fermeture attend tout (2026-10-03)

- **Symptôme** : en CI Debug, `levain_sandbox --gpu webgpu --model …/Sponza.gltf` affiche « fenêtre fermée »,
  puis ne sort plus, jusqu'au SIGKILL de `timeout` (code 137), deux minutes plus tard. En local, rien.
- **Cause** : le WebGPU natif rend hors écran, sans swapchain. `presentFrame` rendait la main tout de suite, et
  le CPU empilait des images bien plus vite que lavapipe ne les rendait (une image de Sponza en Debug prend des
  secondes). À la fermeture, Dawn attend que tout ce qui a été soumis soit fini.
- **Parade** : `presentFrame` attend le GPU quand il n'y a pas de swapchain (`waitForIdle`) : une image en vol.
  Un temps de sortie qui croît avec la durée de la boucle trahit le même défaut.

## Un itérateur de libc++ n'est pas un pointeur (2026-10-03)

- **Symptôme** : `const auto* found = std::ranges::find(tableau, …)` compile avec libstdc++, pas dans le build
  web (Emscripten, libc++) : « incompatible initializer of type `__wrap_iter<…>` ».
- **Cause** : l'itérateur d'un `std::array` est un pointeur chez libstdc++, une classe chez libc++.
- **Parade** : `const auto found`, et lancer le build web avant de pousser une modification du sandbox.

## Une texture comparée doit être une `DepthTexture2D` pour le WGSL (2026-10-02)

- **Symptôme** : `Texture2D shadowAtlas` lu par `SampleCmpLevelZero` compile en SPIR-V, mais Slang écrit en WGSL
  `texture_2d<f32>` avec `textureSampleCompareLevel`, que WebGPU refuse : seule une `texture_depth_2d` se
  compare.
- **Cause** : en HLSL, rien ne distingue une texture de profondeur ; Slang ne le devine pas.
- **Parade** : `DepthTexture2D` (type de Slang) : `texture_depth_2d` en WGSL, et le même SPIR-V. Le backend WebGPU
  déduit ensuite le layout de cette déclaration (`BindingHint`).

## Un `float3` après un scalaire, dans un constant buffer : 16 octets de trop en WGSL (2026-10-02)

- **Symptôme** : sur WebGPU seulement, « bound with size 16 … requires a buffer binding which is at least 32
  bytes » au premier dessin. Vulkan ne dit rien.
- **Cause** : `struct { float exposure; float3 padding; }`. En SPIR-V (std140 de Slang), le `float3` suit le
  `float` dans les mêmes 16 octets ; en WGSL, un `vec3<f32>` s'aligne sur 16 : il commence à l'octet 16 et la
  structure fait 32.
- **Parade** : un `float3` toujours en tête d'un bloc de 16 octets, suivi d'un scalaire (`FrameConstants`), ou du
  bourrage en scalaires. La validation de WebGPU le signale au premier dessin : passer `--gpu webgpu`.

## Un ordre de liens faux, masqué dans le dépôt, visible chez le jeu (2026-10-02)

- **Symptôme** : *Rando*, qui ne relie que `levain::gpu`, échoue à l'édition de liens : « référence indéfinie vers
  nvrhi::CommandListResourceStateTracker… » depuis `libnvrhi_vk.a`. Levain, lui, compile et passe ses tests.
- **Cause** : `nvrhi` était déclaré avant `nvrhi_vk` dans `engine/gpu/CMakeLists.txt`. CMake ne garde que la
  première mention d'une bibliothèque, et l'éditeur de liens ne revient pas en arrière. Dans le dépôt, `render`
  relie `nvrhi` une seconde fois, après : l'erreur ne se voyait pas.
- **Parade** : `nvrhi` déclaré après `nvrhi_vk` ; `tests/gpu_alone.cpp`, un programme qui ne relie que `gpu`, casse
  le build si l'ordre redevient faux (vérifié : 20 références indéfinies sans le correctif).

## Aucun Chromium ne charge de page sur la machine de référence (2026-09-27)

- **Symptôme** : Helium 0.18 (Chromium 154), en headless comme chez Donnovan en fenêtre : l'onglet reste sur
  `about:blank`, `Page.navigate` ne répond jamais, même pour `example.com` ou google.com. Donnovan le constate
  pour tous les Chromium de la machine ; Firefox charge tout.
- **Cause** : non identifiée, du côté du système. En headless, l'uBlock intégré de Helium attend aussi des listes
  de filtres qu'il ne peut pas télécharger (`µBlock.readyToFilter` à `false`), mais lever sa suspension ne
  suffit pas.
- **Parade** : pas de Chromium pour les tests sur cette machine. Firefox pour `tools/web-smoke.sh` ; le
  téléphone ou la tablette de Donnovan pour le critère « Chromium » (navigateur DuckDuckGo, WebView système).

## SDL dans une page web (2026-09-27)

- **Symptôme 1** : « Document.querySelector: '##canvas' is not a valid selector ». **Cause** :
  `SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING` rend un sélecteur, dièse compris, malgré son nom. **Parade** : le
  prendre tel quel (`device_web.cpp`).
- **Symptôme 2** : les crédits de la page ont disparu. **Cause** : avec `SDL_WINDOW_FILL_DOCUMENT`, SDL range tout
  ce qui n'est pas le canvas dans un élément caché (`SDL3_fill_document_background_elements`). **Parade** : un
  calque unique, qu'un `MutationObserver` ressort (`sandbox/web/shell.html`).
- **Symptôme 3** : une modification du modèle de page n'apparaît pas. **Cause** : `--shell-file` est une option
  de lien, pas une dépendance ; ninja ne relie pas. **Parade** : `LINK_DEPENDS` sur la cible.

## Dawn natif tolère ce que le navigateur refuse (2026-09-27)

- **Symptôme** : le cube passe en natif sur Dawn (0 pixel d'écart), mais dans Firefox la page reste noire, avec
  en console « 'depthClearValue' member … is not a finite floating-point value ».
- **Cause** : dans `webgpu.h`, `depthClearValue` vaut NaN par défaut (« indéfini »). Dawn natif l'accepte quand
  la profondeur est chargée (`LoadOp::Load`) ; la couche JavaScript du navigateur valide le dictionnaire avant
  de regarder l'opération.
- **Parade** : donner une valeur finie à tout champ numérique d'un descripteur, même inutilisé. Le test en natif
  ne suffit pas : passer `tools/web-smoke.sh` après toute modification du backend WebGPU.

## clang-tidy sur un fichier que le build ne compile pas (2026-09-27)

- **Symptôme** : l'analyse statique de la CI échoue sur `canvas.cpp` (« use of undeclared identifier
  'WebGpuCanvasDeleter' »), puis compte ses 12 erreurs à chaque fichier suivant.
- **Cause** : la CI donnait à clang-tidy tous les `.cpp` du dépôt. Un fichier compilé seulement sous Emscripten
  n'est pas dans `compile_commands.json` du build natif : clang-tidy l'analyse sans `__EMSCRIPTEN__`, donc à tort.
- **Parade** : la liste vient de `compile_commands.json` (SKILL, section clang-tidy ; `ci.yml`).

## WebAssembly révèle les alignements supposés (2026-09-27)

- **Symptôme** : en WebAssembly, « le pool distribue des blocs distincts et alignés » échoue sur
  `isAligned(block, 16)` ; en natif, tout passait.
- **Cause** : `PoolAllocator` prenait `new std::byte[]` pour assez aligné. La norme ne garantit que
  `alignof(std::max_align_t)` : 16 octets sur un PC 64 bits, 8 en wasm32. En natif, un pool aligné sur 64 octets
  (une ligne de cache) échouait déjà, sans qu'aucun test le demande.
- **Parade** : réserver `alignement − 1` octets de plus et aligner l'adresse réelle du premier bloc, comme
  `LinearAllocator`. Un test demande 64 octets. Tout calcul d'alignement se fait sur l'adresse, jamais sur un
  décalage depuis le début d'un tampon.

## ktx ne compile pas sous Emscripten (2026-09-27)

- **Symptôme** : `cmake --preset web` échoue dans vcpkg sur ktx, « embind requires -std=c++17 or newer », dans
  `interface/js_binding/transcoder_wrapper.cpp`. Suivi d'un trompeur « unable to find a build program
  corresponding to Ninja » : c'est la suite de l'échec de vcpkg, pas un Ninja manquant.
- **Cause** : sous Emscripten, ktx 4.4.2 compile toujours ses liaisons JavaScript, en C++11, qu'Emscripten 6
  refuse. Aucune option ne les coupe.
- **Parade** : `triplets/wasm32-emscripten.cmake` passe `-DCMAKE_CXX_STANDARD=17` à ktx seulement. À retirer
  quand la baseline aura une version de ktx qui compile sans.

## Une erreur de chargement qui finit en assertion Vulkan (2026-09-25)

- **Symptôme** : un modèle dont une texture est illisible (`unknown image type`), ou un mauvais nom de
  `--locomotion`, arrête le sandbox Debug sur « vkDestroyDevice(): … has N leaked objects » puis sur
  l'assertion `!isValidationError(severity, types)` (code 133), au lieu d'un échec propre (ADR-0008).
- **Cause** : `createDemoScene` retournait l'erreur avec sa command list d'envoi encore ouverte. NVRHI ne libère
  jamais une command list ouverte puis détruite : `open()` l'inscrit dans les ressources de son propre command
  buffer (`vulkan-commandlist.cpp`), un cycle que seule la file rompt, quand elle retire le command buffer
  soumis. Tout ce qui a été enregistré (meshes, `UploadChunk`) fuit avec elle.
- **Parade** : un retour anticipé après `open()` ferme et soumet la command list (`submitAbandonedUpload`,
  `engine/app/src/models.cpp`), ou le travail qui peut échouer passe avant `open()`, comme les noms de
  `--locomotion`. Reproduire : copier un glTF dans `data/`, remplacer sa texture par du texte, lancer
  `SDL_VIDEO_DRIVER=offscreen levain_sandbox --seconds 1 --model <gltf>` en Debug : code 1 attendu, sans fuite.

## RenderDoc ne capture rien quand la session est verrouillée (2026-09-24)

- **Symptôme** : `tools/renderdoc-mips.py` échoue sur « aucune capture reçue en 30 s », et des sandbox restent en
  vie bien après leur `--seconds`, en attente dans `poll`.
- **Cause** : le script forçait une fenêtre X11 (RenderDoc 1.45 masque la surface Wayland). Session verrouillée
  (Donnovan à distance), la fenêtre n'est jamais présentée : le sandbox attend un événement, et RenderDoc, qui
  capture à la présentation, ne reçoit rien. `gdb` ne peut pas s'attacher (`ptrace_scope = 1`) : l'état des
  threads se lit dans `/proc/<pid>/task/*/wchan`.
- **Parade** : `tools/renderdoc_capture.py` lance le sandbox **hors écran** (surface « headless », indépendante
  de tout écran), et désactive pour ce seul processus les couches Vulkan implicites de la machine
  (`DISABLE_LSFGVK`, `DISABLE_MAKO`, prévues par leurs manifestes). Ne pas toucher aux réglages du système.
  Arrêter un sandbox bloqué par son PID (`kill -9 <pid>`), jamais par `pkill -f` : le motif se retrouve dans
  la ligne de commande du shell qui le lance, et le tue.

## Une limite que seule une grosse scène atteint, et que seul le Debug voit (2026-09-24)

- **Symptôme** : le sandbox avec Sponza s'arrête en Debug et sous ASan sur « Volatile constant buffer … has
  maxVersions = 16, which is insufficient ». En Release, il tourne et l'image paraît juste.
- **Cause** : chaque `drawMesh` écrit ses constantes dans une nouvelle version du buffer volatil ; 16
  suffisaient aux quelques dessins de la démo, pas aux 105 de Sponza. La validation de NVRHI, seule à le
  détecter, est éteinte en Release.
- **Parade** : `MaxMeshDrawsPerCommandList` (4 096), et la CI lance Sponza en Debug. Toute scène de test
  lourde doit passer au moins une fois en Debug avant qu'on se fie à une image Release.

## `cmake -P` : aucune politique par défaut (2026-09-23)

- **Symptôme** : `cmake.vcpkg-manifest` vert en local (CMake 4.4), rouge sur les trois jobs de la CI : « Policy
  CMP0057 is not set: Support new IN_LIST if() operator », puis un manifeste correct refusé.
- **Cause** : un script lancé par `cmake -P` n'hérite d'aucun `cmake_minimum_required` : ses politiques sont
  celles des vieilles versions, et `IN_LIST` n'est pas un opérateur. Le CMake 4 local les active par défaut, et
  masquait l'erreur.
- **Parade** : un script autonome commence par `cmake_minimum_required(VERSION 3.28)`. Un fichier aussi inclus
  par `include()` règle `cmake_policy(VERSION 3.28)` seulement si `CMAKE_SCRIPT_MODE_FILE`, **avant** ses
  fonctions, qui gardent les politiques de leur définition.

## Une signature qui coûte 5 ns par entité (2026-09-22)

- **Symptôme** : le système des matrices monde passait de 1,42 à 1,91 ms sur 100 000 entités, sans changement
  de logique. Même code écrit à la main dans un binaire de test : 1,42 ms.
- **Cause** : `worldMatrix(const glm::mat4&, const Transform&)` composait la matrice locale elle-même. Écrite
  ainsi, le compilateur matérialise et copie la matrice de retour ; en lui passant la matrice locale déjà
  composée, il écrit directement dans la destination.
- **Parade** : pour une fonction appelée par entité et par image, passer ce qui est déjà calculé plutôt que ce
  qu'il faut calculer. Le bisectage se fait en réécrivant le même corps à la main dans un binaire de test, et
  en supprimant une différence à la fois (termes de requête, phase, ordre de création, signature).

## Shaders

- **Lire l'erreur d'un shader dans la sortie de ninja** (2026-09-22) : chercher « error » ramène aussi la
  ligne de commande, qui contient `-warnings-as-errors`, et les nouveaux diagnostics de slangc
  (`error[E20001]`) s'étalent sur plusieurs lignes, avec la ligne fautive et un repère. Ninja encadre chaque
  échec : `FAILED: …`, la commande, puis la sortie de la commande jusqu'à la ligne d'état suivante `[n/m]`. Garder
  la sortie de la première commande en échec (`firstFailure`, `engine/app/src/shader_reload.cpp`) : les deux
  points d'entrée d'un fichier échouent sur les mêmes erreurs.
- **`SV_VertexID` exige `shaderDrawParameters`** (2026-09-21). En HLSL, il compte depuis 0 sans le sommet de base
  du draw ; en Vulkan, il l'inclut. Slang compense en lisant ce sommet de base (capacité SPIR-V
  `DrawParameters`) : sans la fonctionnalité côté device, la validation refuse le shader à sa création.
- **Aucun `-fvk-invert-y`** : NVRHI inverse déjà le viewport sous Vulkan. L'ajouter mettrait le triangle à
  l'envers.

## Fenêtre et SDL

- **Environ 20 images/s très régulières sous Wayland, et une fenêtre X11 qui ne s'ouvre plus** (2026-09-21,
  soirée) : `SDL_CreateWindow` bloque dans `X11_ShowWindow`. Le même binaire donnait 120 images/s quelques
  heures plus tôt, et les deux symptômes ont disparu ensemble quelques minutes après, sans changement de code :
  c'est l'environnement, pas le moteur. Cause supposée, non vérifiée : l'écran HDMI éteint côté TV. En cas de
  doute, mesurer en offscreen, où rien ne bride, et relancer plus tard.

- **Fenêtre invisible sous Wayland** (2026-09-21). Une surface Wayland n'apparaît qu'après sa première image ;
  tant que le moteur ne présente rien (avant M1.2), KWin ne la connaît pas. Parade : `SDL_VIDEO_DRIVER=x11`.
- **Deux SIGTERM font sauter une assertion du SDL de Debug** (`SDL_quit.c:171`), et SDL ouvre une boîte de
  dialogue zenity sur le bureau, qui attend une réponse. Cause : `timeout` signale l'enfant **puis** son groupe
  de processus. Parade : `timeout --foreground`, toujours.
- **SIGINT ignoré en arrière-plan.** Un processus lancé avec `&` depuis un shell non interactif hérite d'un
  SIGINT ignoré, et SDL respecte ce choix : `kill -INT` ne fait rien. Arrêter le sandbox par SIGTERM.
- **Titres non ASCII perdus sous X11** (`SDL_x11window.c:2300`) : SDL abandonne en silence et fuit. Une
  assertion ASCII dans `setWindowTitle` l'empêche.
- **Dépendances système de SDL en CI.** Les paquets suggérés par le port vcpkg ne suffisent pas : SDL exige
  huit extensions X11 (contrôles stricts de `cmake/sdlchecks.cmake`). La liste de `ci.yml` vient du
  `README-linux` de SDL ; en cas d'erreur `Couldn't find dependency package for X`, chercher le `-dev` de X.
- **Sous KWin, `Scripting.start()` exécute le script plus tard** : le décharger tout de suite l'annule sans
  message. `tools/kwin-window-smoke.sh` attend 0,5 s entre les deux.

## CI

- **Le démarrage peut manger tout le délai du sandbox** (2026-09-21). Sur le runner, il varie de 1 à plus de
  10 s selon la charge (lavapipe, couches de validation, sanitizers). Un délai de 3 s, puis de 8 s, est tombé
  avant la première frame ; l'étape restait verte la première fois, le contrôle « boucle ≥ 1 s » l'a fait
  rougir la seconde (PR #59). Parade : `--seconds N`, compté depuis le premier tour de boucle ; `timeout`
  n'est plus qu'un filet contre un blocage.
- **Un cache GitHub n'est visible que de sa branche et de `main`** (2026-09-21). Le cache rempli par une PR ne
  sert pas à la PR suivante : après un changement du manifeste, les PR n'en profitent qu'une fois qu'une CI a
  tourné sur `main`. En cas d'erreur réseau (504) sur un job, relancer les jobs en échec après que `main` a
  enregistré ses caches.
- **Un cache binaire sans `restore-keys` recompile tout à chaque changement du manifeste** (2026-10-06).
  Symptôme : après l'arrivée de Jolt dans *Rando* (#6), chaque job recompilait toutes les dépendances (25 à
  37 min), et les PR empilées au-dessus aussi, puisque le cache de #6 ne leur était pas visible ; remarqué par
  Donnovan. Cause : la clé contient l'empreinte de `vcpkg.json`, et sans `restore-keys` aucun ancien cache ne
  correspond. Parade : `restore-keys: vcpkg-<tag>-`. vcpkg ne reprend une archive que si l'ABI du paquet n'a
  pas changé, il ne compile donc que ce qui manque : 26 paquets restaurés, Jolt et meshoptimizer compilés,
  3 min (*Rando* #9).
- **Le bootstrap de vcpkg exige que `VCPKG_DOWNLOADS` soit un dossier existant** (2026-09-21) : « was set to
  …, but that was not a directory ». Restaurer ce cache et créer le dossier **avant** le bootstrap ; le cache
  binaire, lui, peut attendre après.
- **`timeout … | tee`** : sans `set -o pipefail`, le code de sortie est celui de `tee`, et une fuite signalée
  par LeakSanitizer passerait inaperçue.

## Sanitizers

- **UBSan dans stb_image_resize2 v2.10** (port vcpkg `stb` 2024-07-29, 2026-09-21) : « load of misaligned
  address … for type 'stbir_uint64' », ligne 3653. `STBIR_MOVE_2` copie deux `float` par une lecture 64 bits
  non alignée, un comportement indéfini. Parade : ne pas l'utiliser ; les mipmaps passent par un filtre boîte
  écrit à la main (`engine/assets/src/image.cpp`). stb_image, le décodeur, passe sous ASan et UBSan.
- **LeakSanitizer et RADV : 128 octets** (2026-09-21). Le loader Vulkan décharge le pilote à la destruction de
  l'instance ; la mémoire que RADV gardait dans une globale paraît alors perdue. Diagnostic : la fuite
  disparaît avec `LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libvulkan_radeon.so`, persiste sans couche de
  validation et sans `device_select`. Vérification sans faux positif : **Wayland + RADV préchargé**, code 0.
- **Ne pas précharger RADV et libX11 ensemble sous X11** : `SDL_CreateWindow` bloque dans `X11_ShowWindow`
  (`XIfEvent` attend un `MapNotify` qui ne vient pas). Configuration de diagnostic seulement, sans incidence sur
  un lancement normal.

- **Faux positifs LeakSanitizer sous X11** : ~50 Ko en ~900 allocations, la mémoire permanente de libX11 que SDL
  décharge par `dlclose`. Pour vérifier qu'il ne reste rien de vrai, précharger les bibliothèques X11
  (`LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libX11.so.6:…`) : les faux positifs disparaissent, les vraies fuites
  restent. La CI tourne en offscreen et n'est pas concernée.
- **Pile tronquée à une bibliothèque système** : `ASAN_OPTIONS=fast_unwind_on_malloc=0` pour une pile complète.
- **Un code de sortie lu à travers un pipe** est celui du dernier programme (`| tail` rend 0). Rediriger vers
  un fichier, puis lire `$?`.

## Contre-tests

- **`pkill -f <motif>` tue le shell qui le lance** (2026-09-21) : le motif figure dans sa propre ligne de
  commande. Chercher par nom exact : `pgrep -a -x levain_sandbox`, `pkill -x levain_sandbox`.
  Le 2026-09-27, trois fois de plus, sous d'autres formes : `pgrep -f`, puis `ps | grep "[h]ttp.server"`,
  alors que la même commande lançait ensuite `python3 -m http.server` (le texte du motif était dans le script
  du shell). **Garder le PID au lancement** (`programme & echo $! > fichier.pid`, puis `kill $(cat
  fichier.pid)`) pour tout processus qu'on arrêtera plus tard : serveur HTTP, Firefox de test.
- **Une faute injectée pour un contre-test se retire depuis une copie** (`cp fichier copie`, puis `cp copie
  fichier`), jamais par `git checkout -- fichier` : il efface aussi tout ce qui n'était pas encore commité. C'est
  arrivé le 2026-09-21 à l'intégration du device dans le sandbox, réécrite ensuite.
- Méthode qui marche pour la validation : un buffer Vulkan de taille 0 (`VUID-VkBufferCreateInfo-size-00912`) et
  une texture NVRHI de largeur 0 doivent chacun arrêter le programme (code 133, SIGTRAP).

## Avertissements et clang-tidy

- **Une variable qui ne sert qu'à une assertion** : `LEVAIN_ASSERT` compile son expression en Release sans
  l'évaluer (`sizeof`), donc pas d'avertissement. Une **fonction interne** dans le même cas reste signalée par
  clang (`-Wunneeded-internal-declaration`) : `[[maybe_unused]]`.
- **Constante globale d'un type non `constexpr`** (`const nvrhi::Color c{…}`) : clang-tidy la refuse
  (`bugprone-throwing-static-initialization`), une exception levée avant `main` ne se rattrape pas. La rendre
  locale à la fonction qui s'en sert.
- **Initialisation désignée incomplète** (`-Wmissing-designated-field-initializers`) : donner un initialiseur
  par défaut au champ (`PixelSize pixelSize{};`) plutôt que d'écrire `.champ = {}` partout.
- **clang-tidy 22 et `std::optional`** : `.value()` compte comme un accès non vérifié, et `REQUIRE` de doctest
  n'est pas reconnu comme une garde. Dans les tests : `value_or(T{})`, dont la valeur par défaut fait échouer
  les `CHECK`.
- **Macros de doctest** : `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` est posée par `set_source_files_properties` dans
  `tests/CMakeLists.txt`, pas par un `#define` que clang-tidy refuserait (préfixe `LEVAIN_`).
- **`main` et les exceptions** : `std::print` peut lever, et une exception qui sort de `main` est signalée par
  clang-tidy. Un `try`/`catch` au sommet de `main` (ADR-0008).

## Bureau de Donnovan

- **Ne pas faire de capture d'écran de son bureau** (2026-09-21). `spectacle -a` capture la fenêtre active, pas
  celle qu'on vise : KWin a refusé de donner le focus au profileur, et l'image montrait son navigateur sur une
  page de connexion. Supprimée aussitôt. Une capture d'écran se demande à Donnovan.
- **`pkill -f <motif>` tue aussi le shell qui l'exécute** si sa ligne de commande contient le motif. Cibler par
  nom de processus (`pgrep -x`, `ps -eo pid,comm`).

## Chaîne d'outils

- **`VCPKG_ROOT` non exportée** : CMake prenait les paquets du système (le `spdlog` d'Arch) sans rien dire. Le
  `CMakeLists.txt` racine refuse maintenant de se configurer sans la toolchain vcpkg.
- **LLVM en CI** : `update-alternatives` ne remplace pas `/usr/bin/clang++`, qui est un vrai fichier. La CI a
  tourné trois milestones sur clang 18 en croyant utiliser clang 22. Parade en place : liens dans
  `/usr/local/bin` et étape « Vérifier la chaîne », qui échoue si la version n'est pas la bonne.
- **clang-format change de sortie d'une version majeure à l'autre** : la version de la CI (`LLVM_VERSION`) doit
  rester celle de la machine de référence.
- **`$env{…}` dans une chaîne de `message()` CMake** est interprété et casse le parsing : l'écrire autrement.
- **Le bootstrap de vcpkg exige `zip`** (paquet système).
- **Le shell des commandes de l'agent est zsh** : une variable non quotée n'y est pas découpée en mots
  (`$args` valant `--seconds 2` arrive en un seul argument). Écrire les arguments en toutes lettres.
  Et `$var:r…` y applique un modificateur (`:r` retire l'« extension ») : `git push origin $C:refs/heads/x`
  pousse une référence tronquée (2026-10-03). Écrire `${C}:refs/heads/x`.
- **Le shell de Donnovan est fish** : `set -Ux` pour une variable d'environnement persistante. Les scripts du
  dépôt commencent par `#!/usr/bin/env bash`.

## RenderDoc

- **`qrenderdoc --python` n'affiche rien** (2026-09-21) : les `print` vont dans la console Python de
  qrenderdoc, pas sur la sortie standard. Écrire dans un fichier (`tools/renderdoc-mips.py`) et finir par
  `os._exit`, sinon qrenderdoc ouvre son interface après le script et ne rend jamais la main.
- **Le module Python `renderdoc` n'existe que dans qrenderdoc** : le paquet Arch ne l'installe pas pour le
  Python du système. D'où `QT_QPA_PLATFORM=offscreen qrenderdoc --python …`, sans fenêtre.
- **Au premier lancement, qrenderdoc bloque avant le script** (2026-09-21) : même un script d'une ligne ne
  s'exécute pas, le journal (`/tmp/RenderDoc/*.log`) s'arrête après `LoadLayout`. Cause : la question sur les
  statistiques d'usage (`AnalyticsPromptDialog`), invisible en offscreen. Vérifié : le script tourne dès que
  Donnovan y a répondu, en lançant `qrenderdoc` une fois. C'est son choix, pas celui de l'agent.
- **RenderDoc 1.45 masque `VK_KHR_wayland_surface`** : sous RenderDoc, `SDL_CreateWindow` échoue sous Wayland.
  Lancer le programme capturé avec `SDL_VIDEO_DRIVER=x11` (XWayland). `renderdoccmd capture -w …` montre la
  sortie du programme capturé, que `ExecuteAndInject` cache.
- **Capturer exige un bureau visible et déverrouillé** (2026-09-24) : `tools/renderdoc_capture.py` impose
  X11. Bureau verrouillé, trois sandbox `--seconds 6` ne se sont jamais arrêtés, ni sur SIGTERM (il a fallu
  `kill -9`). L'échéance ignorée fenêtre masquée est corrigée (`waitEvents` borné) ; le SIGTERM ignoré n'a pas
  été reproduit : minimisée, SIGTERM réveille bien l'attente (code 0). Cause supposée, non vérifiée : un blocage
  dans la présentation X11 plutôt que dans l'attente d'événements. En `SDL_VIDEO_DRIVER=offscreen`, le sandbox
  a aussi semblé bloquer sous RenderDoc ; piste non vérifiée, la couche Vulkan implicite de l'utilisateur
  `liblsfg-vk-layer.so` (« Failed to find vkGetInstanceProcAddr »). Ne pas toucher aux couches de l'utilisateur.
- **Mesurer une image de capture avec ImageMagick** (2026-09-21) : les PNG de RenderDoc ont un canal alpha,
  compté par `fx:standard_deviation` avec une variance nulle : le contraste mesuré est divisé par deux. Toujours
  `-alpha off`. Et `-gravity` persiste d'une option à l'autre : un `-splice` après `-gravity center` insère ses
  lignes au milieu de l'image (`+gravity` d'abord).
- **Objets sans nom dans une capture** : NVRHI ne transmet les `debugName` que s'il sait `VK_EXT_debug_utils`
  active, annoncée dans `DeviceDesc::instanceExtensions` (`engine/gpu/src/device_vk.cpp`).

## Tracy

- **`TRACY_ENABLE` est OFF par défaut depuis Tracy 0.14** (2026-09-21). Sans lui, les macros se compilent en
  rien : le build réussit, le binaire ne profile rien. Le port overlay le force, et `engine/core/CMakeLists.txt`
  refuse de configurer un build profilé sans lui. Preuve qu'un client est actif : il écoute sur le port 8086
  (`ss -ltnp | grep 8086`), et `TRACY_NO_EXIT=1` l'empêche de sortir.
- **`TRACY_NO_EXIT=1` n'est pas optionnel** pour un programme court : sans lui, le programme se termine avant
  qu'un profileur ait pu se connecter, et n'affiche aucun avertissement.
- **Client et profileur doivent avoir la même version de protocole** : 0.14.1 des deux côtés (port overlay pour
  le client, release officielle pour les outils, empreinte SHA-256 vérifiée contre celle publiée par GitHub).

## Windows revient (ADR-0035)

Compilé depuis Linux par clang-cl : les pièges de l'essai sont dans l'entrée du 2026-10-08, en haut. Des deux
bugs de M0.2, `__cplusplus` n'existe pas sous clang-cl 23 ; `<ostream>`, qui vient de la STL de Microsoft, s'est
retrouvé tel quel.
