# ADR-0035 — Windows revient, compilé depuis Linux par clang-cl

- **Statut** : accepté le 2026-10-08, sur les réponses de Donnovan à deux sondages du jour (§ Décision)
- **Modifie** : [ADR-0011](0011-retour-au-cpp.md) § 1 (« Linux d'abord » : Windows revient, le compilateur est
  choisi) ; [ADR-0001](0001-langage-cpp23.md) (« compilé avec MSVC sous Windows ») ;
  [ADR-0007](0007-build-cmake-vcpkg.md), amendement des ports overlay (un cas de plus : une parade de
  compilation sous clang-cl) ; corrige [`docs/QA.md`](../QA.md) sur `__cplusplus` sous clang-cl
- **Date** : 2026-10-08
- **Milestone** : M1.4, rouvert

## Décision en bref

Windows revient : **compilé depuis Linux par clang-cl**, le même LLVM que la CI. Le moteur tourne d'abord sous
Windows **en Vulkan**, tel quel ; le **backend Direct3D 12** suit. En CI, un runner Linux compile le binaire
Windows, un runner Windows le lance. C'est fait **maintenant**, avant la fin de M7.2.

## Contexte

L'ADR-0011 différait Windows et Direct3D 12 « jusqu'à ce qu'une machine Windows soit disponible ». Depuis le
2026-10-08, Donnovan travaille aussi sur un portable Windows : Core i9-14900HX, RTX 4070 Laptop (8 Go) et iGPU
Intel UHD, 64 Go de RAM, avec la distro WSL `levain-dev` de [`tools/wsl/`](../../tools/wsl/README.md). M8.2
demande de toute façon des binaires Windows produits par la CI.

**Sous WSL, pas de Vulkan Linux sur ce GPU.** WSL partage la carte (CUDA, Direct3D 12), mais le pilote NVIDIA
pour WSL ne fournit pas de pilote Vulkan : `/usr/lib/wsl/lib` n'a pas de `nvidia_icd.json` (pilote 617.42). Le
seul chemin, Dozen, un pilote de Mesa qui traduit Vulkan en Direct3D 12, s'arrête à Vulkan 1.2 sur une carte
NVIDIA [1], quand `engine/gpu/src/device_vk.cpp` exige la 1.3 ; le Mesa d'Ubuntu 26.04 ne le contient pas.
**Faire tourner Levain sur ce GPU, c'est produire un `.exe` Windows.**

**Donnovan veut garder clang sur les deux plateformes**, en compilant pour Windows depuis Linux, « comme pour
Unreal » (qui fait le chemin inverse : il compile pour Linux depuis Windows).

**Les termes.** *clang-cl* est clang avec les options et l'ABI de MSVC : il produit des binaires Windows
ordinaires, liés par *lld-link*, l'éditeur de liens de LLVM. Il lui faut la bibliothèque standard (la STL) et le
SDK de Microsoft, rangés comme dans Visual Studio : un *winsysroot*. Hors de Windows, *xwin* [2] les télécharge
chez Microsoft. Le *PDB* est le fichier de symboles de Windows. *WARP* est le Direct3D 12 logiciel de Windows,
*lavapipe* le Vulkan logiciel de Mesa (celui de notre CI Linux). L'*interop* de WSL lance un `.exe` Windows
depuis le terminal de la distro.

### Ce que l'essai a mesuré

Branche jetable `spike/windows`, dont [`prototypes/windows/README.md`][essai] donne les commandes, les chiffres et
les pièges. Dans la distro WSL du portable ; la STL et le SDK de Microsoft lus dans l'installation de Visual
Studio Build Tools 2026 du PC (MSVC 14.51, SDK 10.0.26100), par un winsysroot de deux liens. xwin n'a pas
tourné : son chemin se prouvera dans la PR de la CI.

- **Exactement C++23.** clang-cl 23.1.3, le LLVM de la CI, donne :

  | Option | `__cplusplus` et `_MSVC_LANG` |
  |---|---|
  | `/std:c++20` | 202002 |
  | `/std:c++latest` | 202700, le brouillon d'après C++26 |
  | `/clang:-std=c++23` | **202302** |

  CMake 4.2 traduit `CMAKE_CXX_STANDARD 23` en `-clang:-std=c++23` pour clang-cl. MSVC, lui, n'a pas de
  `/std:c++23` : `/std:c++23preview`, en préversion, ou `/std:c++latest`, que CMake lui passe (ADR-0001).
  `docs/QA.md` affirmait que clang-cl reproduit le piège `__cplusplus` de MSVC (199711) : c'est faux pour
  clang-cl 23.
- **Direct3D 12 sur la 4070.** Une sonde compile en 8,7 s, sans avertissement en `/W4 /WX`. Lancée depuis le
  terminal de WSL, elle tourne sous Windows et crée un device D3D12 sur la « RTX 4070 Laptop GPU » (shader model
  6.8). La couche de debug D3D12 est installée.
- **Les dépendances.** Les 25 ports vcpkg du moteur se compilent pour Windows depuis Linux : 16 min au plus la
  première fois, sur 24 tâches parallèles (la somme des temps que donne vcpkg), dont 8,3 pour Dawn. Deux ont
  demandé une parade, ktx et ozz. Le port des couches de validation Vulkan, écarté de l'essai, en demandera une
  pour spirv-reflect.
- **Le moteur.** Il n'a pas une ligne propre à Linux : `grep` de `_WIN32`, `__linux__`, `unistd.h`, `dlfcn.h`,
  `sys/`, `inotify` et `pthread` ne trouve rien. Six fichiers ne compilaient pas : quatre corrigés dans le code
  (des chemins en `wchar_t` sous Windows, un `<ostream>` manquant, le bug de M0.2), deux par des définitions
  (`NOMINMAX`, et `_CRT_SECURE_NO_WARNINGS`, que l'implémentation ne garde pas). Les shaders DXIL se compilaient
  déjà sous Linux.
- **Les tests.** `levain_tests.exe` passe 293 cas sur 295 sous Windows, en 8 s. `ctest` sur la Release, lancé
  depuis la distro, en passe 315 sur 337 ; les 22 échecs sont expliqués au § Conséquences. Un seul vient du
  moteur : `gpu.environment.webgpu` trouve sur la 4070 un reflet préfiltré et une BRDF faux (0,394 au lieu de 1
  vue de face), que lavapipe ne montre pas.
- **Le sandbox tourne sur la 4070**, en Release et en Vulkan 1.4.351 : device créé en 321 ms, 822 images en
  5,0 s, 0,696 ms de GPU par image, capture juste. Ces chiffres sont indicatifs : les mesures du projet restent
  sur la machine de référence (SPECS § 10).

## Options envisagées

### 1. La chaîne de compilation

| Option | Pour | Contre |
|---|---|---|
| **A. clang-cl depuis Linux** (LLVM 23, lld-link, un winsysroot) | Un compilateur, une version, un environnement : ceux de la CI et de la machine de référence ; exactement C++23 ; mêmes avertissements et clang-tidy ; le moteur compilé et lancé par l'essai | La STL et le SDK de Microsoft à fournir hors de Windows, sous la licence de Microsoft ; une toolchain et un triplet à nous ; les ports vcpkg ne sont pas testés avec clang-cl |
| B. clang-cl sous Windows | Même compilateur ; le chemin balisé de vcpkg et Visual Studio | Un second environnement à tenir (vcpkg, scripts, CMake sous Windows), que la machine de référence ne peut pas exécuter |
| C. MSVC sous Windows (l'ADR-0001) | Le chemin par défaut, le mieux outillé | Pas de `/std:c++23` stable ; d'autres avertissements ; les deux bugs de M0.2 ; un second environnement aussi |
| D. clang et MinGW-w64 depuis Linux (llvm-mingw) | Pas de SDK Microsoft ; MinGW, la voie des binaires officiels de Godot | Une autre ABI que celle de Windows ; des triplets mingw de vcpkg communautaires ; D3D12 et sa couche de debug moins balisés |

### 2. L'API graphique sous Windows

| Option | Pour | Contre |
|---|---|---|
| **G1. Vulkan d'abord, D3D12 ensuite** | Rien de graphique à écrire pour voir le moteur sur la 4070 ; D3D12 vient sur une base qui compile et se teste | Deux étapes |
| G2. D3D12 tout de suite | Le contenu prévu de M1.4 d'un bloc | Toolchain, CI et backend dans la même étape |
| G3. Vulkan seul | Le moins de code | Ni WARP en CI, ni la couche de debug D3D12, ni le second backend que NVRHI rend presque gratuit |

### 3. La CI Windows

| Option | Pour | Contre |
|---|---|---|
| **C1. Compiler sur un runner Linux, tester sur un runner Windows** | La CI compile comme le développeur ; le runner Windows ne fait que lancer | Deux jobs liés par un artefact ; la STL et le SDK téléchargés par xwin |
| C2. Tout sur un runner Windows (Visual Studio préinstallé) | Pas de xwin | La CI ne compilerait pas comme le développeur |
| C3. Linux seul, tests sous Wine | Un seul runner | Ni WARP ni couche de debug D3D12 sous Wine |

### 4. Le rechargement à chaud des shaders dans l'exe Windows

Il relance `cmake` et `slangc`, des binaires Linux qu'un exe Windows ne peut pas exécuter. Options : le
relancer par `wsl.exe` dans la distro d'où vient l'exe ; le refuser sous Windows ; un `slangc` Windows.

### 5. Les couches de validation Vulkan sous Windows

Le port vcpkg, compilé avec le reste ; ou le Vulkan SDK de LunarG, installé sur chaque machine.

## Décision

Les réponses de Donnovan, mot pour mot. Premier sondage : « clang-cl depuis Linux (Recommandé) », « Vulkan
d'abord, D3D12 ensuite (Recommandé) », « Compiler sous Linux, tester sous Windows (Recommandé) »,
« Maintenant, M7.2 ensuite (Recommandé) ». Second sondage, après la relecture : « Par wsl.exe, depuis la distro
(Recommandé) » ; pour les couches de validation, « Celui que tu pense le mieux, j'ai pas de préférence ici » :
le port vcpkg, pour la règle n°5.

1. **Windows se compile depuis Linux par clang-cl**, avec lld-link et les outils de `llvm-23` (`llvm-lib`,
   `llvm-rc`, `llvm-mt`) : une toolchain, `cmake/toolchains/windows-clang-cl.cmake`, que vcpkg charge pour
   chaque port comme pour Levain ; un triplet, `x64-windows-clang` (bibliothèques statiques, CRT en DLL) ; deux
   presets, `windows-debug` et `windows-release`.
2. **La STL et le SDK viennent d'un winsysroot**, désigné par `LEVAIN_WINSYSROOT`. Sur le portable, deux liens
   vers ses Build Tools ; sur la machine de référence et en CI, la sortie de xwin, aux versions du portable si
   son manifeste les propose (MSVC 14.51, SDK 10.0.26100), écrites dans le dépôt. Leur licence est celle de
   Microsoft, que Donnovan accepte par son choix (l'option du sondage le disait) : elles servent à compiler, et
   ne vont ni dans le dépôt ni dans les artefacts de la CI.
3. **Vulkan d'abord.** Le moteur tel quel tourne sous Windows ; le backend D3D12 (le contenu de M1.4 :
   `device_d3d12.cpp`, la swapchain DXGI, `--api vulkan|d3d12`, le test de fumée sous WARP) suit.
4. **La CI** : un job Linux compile `windows-debug` et `windows-release` et en fait un artefact ; un job Windows
   lance les programmes de la cible, Debug compris, puisque seul le Debug arrête une erreur de validation. Les
   tests Vulkan y tournent sur lavapipe pour Windows, comme sous Linux ; aucun test n'est écarté sans l'accord
   de Donnovan. Les contrôles qui lancent un outil de l'hôte (`cmake -P`, Python, la lecture des symboles)
   restent dans les jobs Linux, où le job croisé lit aussi le PDB du binaire Windows. Le job Windows ne bloque la
   fusion qu'une fois ajouté aux checks requis de `main`, ce qui se demande à Donnovan par sondage, dans la PR
   de la CI, après un premier passage vert.
5. **Le rechargement des shaders passe par `wsl.exe`** quand l'exe a été compilé dans une distro WSL : il y
   relance le build Linux. Ailleurs (l'exe de la CI), il refuse avec un message.
6. **Les couches de validation viennent du port vcpkg**, livrées à côté de l'exe : les mêmes sur le PC de
   Donnovan et en CI, rien à installer.
7. **Maintenant** : M1.4 passe avant la fin de M7.2 (la PR de ses tests de refus à la compilation, puis sa
   clôture).

## Conséquences

### Ce que l'essai a dû régler, et que l'implémentation garde

- **clang-cl lit `-Wall` comme `/Wall`, c'est-à-dire `-Weverything`.** Notre `CMakeLists.txt` passe ses options
  de clang par `/clang:`. Un projet qui teste `CMAKE_CXX_COMPILER_ID` (« Clang » sous clang-cl) pour ajouter
  `-Wall -Werror` se compile avec tous les avertissements, en erreurs : ozz (son port overlay lui fait prendre sa
  branche MSVC, et la CRT du triplet), spirv-reflect. Les ports de vcpkg ne sont pas testés avec clang-cl : un
  port qui casse reçoit sa parade dans un port overlay, qui dit sa raison et sa condition de retrait
  (ADR-0007), et le piège va dans `build/GOTCHA.md`.
- **vcpkg n'ôte `/MP` que pour un compilateur nommé `clang-cl.exe`** (ktx, compilé en `-Werror`, s'y arrêtait) :
  la toolchain le retire.
- **Dawn ne trouve pas `vulkan-1.dll`** (« Windows Error: 87 ») et retombe en silence sur son backend Null, qui
  ne dessine rien : il reçoit le dossier `System32`. Un test WebGPU refusera désormais le backend Null (règle
  n°7) : sous Windows, six des neuf tests WebGPU de `levain_tests` passaient sur ce backend.
- **Chaque programme tourne en UTF-8**, par un manifeste (`activeCodePage`) : sans lui, argv arrive dans la page
  de code ANSI, et `ctest` ne trouvait pas les cas de test accentués. Les chemins passés aux fonctions C
  (`path.string()`, pour ktx et stb) n'ont les accents justes que grâce à lui. Le manifeste fixe un plancher :
  Windows 10 1903.
- **Pas de `_CRT_SECURE_NO_WARNINGS`** : la CRT de Microsoft déclare « unsafe » `getenv`, et le définir couperait
  un avertissement (règle n°4). Les trois appels passent par une fonction nommée qui n'emploie pas l'API
  déconseillée (`SDL_getenv` là où SDL est visible, `_dupenv_s` sous Windows ailleurs). `NOMINMAX`, lui, retire
  des macros, pas un avertissement : il reste.

### Les 22 échecs de `ctest`, et ce qui les règle

- **8 programmes GPU et tests de fumée** refusent de démarrer sans les couches de validation, en Release aussi :
  ils les exigent dans toute configuration. Le port vcpkg les règle (décision 6).
- **7 `cmake.plugins.*`** configurent un mini-moteur sans la toolchain croisée : ils la recevront.
- **3 contrôles par `nm`** (`build.no-tracy`, `build.no-editor`, `build.editor-symbols`) : sous Windows, les
  symboles sont dans le PDB ; ils le liront par `llvm-pdbutil`, dans le job Linux.
- **2 `runProcess`** lancent le `cmake` de Linux depuis l'exe : le test lancera un programme de la cible.
- **`waitEvents`** passe quand `levain_tests.exe` tourne seul, et échoue sous `ctest -j 8`, sans sortie : la cause
  reste à trouver. Le test, écrit pour un vrai bug (une attente sans fin), ne sera pas relâché pour passer.
- **`gpu.environment.webgpu`** : le reflet préfiltré et la BRDF faux sur la 4070 (issue #347), peut-être un vrai
  bug que lavapipe cache. M1.4 ne se ferme pas tant qu'il échoue.

S'y ajoutent deux pièges d'outillage : **la découverte des cas de doctest** lance l'exe pendant le build, ce que
fait l'interop de WSL mais pas un runner Linux (elle passera au moment des tests) ; **les chemins Linux compilés
dans les tests** ne marchent sous Windows que lancés depuis WSL (`ctest` les donnera).

### Ce que Donnovan a à faire

Rien à installer sous Windows : les Build Tools 2026 sont déjà sur le portable, le pilote NVIDIA fournit Vulkan
et la couche de debug D3D12 est présente. Il lance le sandbox depuis la distro ; un exe livré (M8.2) demandera le
redistribuable Visual C++, que Microsoft permet de joindre au binaire. **Pour déboguer** sous Windows (Visual
Studio, RenderDoc, PIX), les PDB nommeront les sources par leur chemin dans la distro
(`\\wsl.localhost\levain-dev\…`) : à vérifier dans la PR de la chaîne.

### Coûts et risques

- **La CI** : non mesuré. Un runner GitHub a 4 cœurs ; la première compilation des ports Windows y prendra
  bien plus que les 16 min du portable, puis le cache de vcpkg les garde, comme pour Linux. Les runners sont
  gratuits pour un dépôt public.
- **xwin** n'a pas tourné pendant l'essai ; si son manifeste n'a pas les versions du portable, les plus proches,
  et le dire dans la PR de la CI.
- **Les ports vcpkg sous clang-cl** : d'autres casseront peut-être à une mise à jour de la baseline.

### Les PR et le reste

- **Les PR**, une à la fois ou empilées (règle n°1) : cet ADR ; la chaîne (#344 : toolchain, triplet, presets,
  manifeste, corrections du moteur, Dawn, couches de validation, `llvm-23` dans la distro, `windows-debug` dans
  `tools/verify.sh`) ; les tests et le rechargement par `wsl.exe` (#345) ; la CI (#346) ; le backend D3D12
  (#18, #19). L'essai ajoute 187 lignes hors prototypes : la PR de la chaîne tient sous 400. Le backend D3D12,
  que l'essai n'a pas écrit, aura la taille de son pendant Vulkan (`device_vk.cpp` et `swapchain_vk.cpp` :
  676 lignes) : il se découpera (règle n°2).
- **Roadmap v0.15** : M1.4 rouvert, 1,5 h Donnovan (0,75 pour Windows en Vulkan, 0,75 pour D3D12), 3 sessions ;
  il prend la place de la fin de M7.2, dont il reprend l'échéance (14/03/2027) ; les autres échéances ne
  bougent pas.
- **SPECS** : Windows 10 (1903 et plus) et 11, par clang-cl, compilé depuis Linux ; § 10, le portable de
  Donnovan devient la vraie machine Windows, sans en faire une machine de référence.

## Ce que font les autres moteurs

- **Unreal** compile pour Linux depuis Windows, avec une chaîne clang qu'Epic fournit par version du moteur ; seul
  Windows est pris en charge comme hôte de cette compilation croisée [3].
- **Unity** compile ses lecteurs Linux IL2CPP depuis n'importe quel système de bureau, par des paquets de sysroot
  et de toolchain [4] ; IL2CPP passe par le compilateur C++ natif de la plateforme [5].
- **Godot** construit tous ses binaires officiels dans des conteneurs, avec MinGW ; MinGW-LLVM est pris en
  charge, et D3D12 est compris par défaut (`d3d12=no` le retire) [6].
- **Firefox** a commencé en 2020 à compiler ses builds Windows sur Linux par clang-cl : plus rapide à matériel
  égal, sans licence Windows par machine ; Mozilla ne livre plus de build MSVC depuis Firefox 63 [7].
- **Chromium** peut compiler depuis Linux ou macOS la plupart de son code pour Windows ; le SDK s'empaquette
  depuis une machine Windows, et un bot compile en croisé et lance les tests [8].

## Sources

[essai]: https://github.com/PhantomDO/Levain/blob/ebadeb3461dad0a00f7b27c5c33afce034ff82bd/prototypes/windows/README.md

1. Dozen en Vulkan 1.2 sur une RTX 3090, Ubuntu 26.04 sous WSL2 :
   <https://github.com/taytay-industries/fork-godogen/pull/2> ; pas de `nvidia_icd.json` sous WSL :
   <https://github.com/microsoft/WSL/issues/12313>
2. xwin : <https://github.com/Jake-Shadle/xwin>
3. <https://dev.epicgames.com/documentation/unreal-engine/linux-development-requirements-for-unreal-engine>
4. <https://docs.unity3d.com/Packages/com.unity.sysroot@2.0/manual/index.html>
5. <https://docs.unity3d.com/Manual/scripting-backends-intro.html>
6. <https://docs.godotengine.org/en/4.6/engine_details/development/compiling/compiling_for_windows.html>
7. <https://glandium.org/blog/?p=4020>
8. <https://chromium.googlesource.com/chromium/src/+/main/docs/win_cross.md>
