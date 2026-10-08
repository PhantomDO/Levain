# ADR-0035 — Windows revient, compilé depuis Linux par clang-cl

- **Statut** : accepté le 2026-10-08, sur les réponses de Donnovan au sondage du jour, les quatre
  recommandées : clang-cl depuis Linux, Vulkan d'abord puis D3D12, compiler sous Linux et tester sous Windows,
  maintenant
- **Modifie** : [ADR-0011](0011-retour-au-cpp.md) § 1 (« Linux d'abord » : Windows revient, le compilateur est
  choisi) ; [ADR-0001](0001-langage-cpp23.md) (« compilé avec MSVC sous Windows ») ; corrige
  [`docs/QA.md`](../QA.md) sur `__cplusplus` sous clang-cl
- **Date** : 2026-10-08
- **Milestone** : M1.4, rouvert

## Contexte

L'ADR-0011 différait Windows et Direct3D 12 « jusqu'à ce qu'une machine Windows soit disponible ». Depuis le
2026-10-08, Donnovan travaille aussi sur un portable Windows : Core i9-14900HX, RTX 4070 Laptop (8 Go) et iGPU
Intel UHD, 64 Go de RAM, avec la distro WSL `levain-dev` de [`tools/wsl/`](../../tools/wsl/README.md). M8.2
demande de toute façon des binaires Windows produits par la CI.

**Sous WSL, pas de Vulkan Linux sur ce GPU.** WSL partage la carte (CUDA, Direct3D 12), mais le pilote NVIDIA
pour WSL ne fournit pas de pilote Vulkan : `/usr/lib/wsl/lib` n'a pas de `nvidia_icd.json` (pilote 617.42). Le
seul chemin, Dozen (un pilote de Mesa qui traduit Vulkan en Direct3D 12), plafonne à Vulkan 1.2 [1], quand
`engine/gpu/src/device_vk.cpp` exige la 1.3 ; le Mesa d'Ubuntu 26.04 ne le contient pas. **Faire tourner Levain
sur ce GPU, c'est produire un `.exe` Windows.**

**Donnovan veut garder clang sur les deux plateformes**, en compilant pour Windows depuis Linux, « comme pour
Unreal » (qui fait le chemin inverse : il compile pour Linux depuis Windows).

### Ce que l'essai a mesuré

Branche jetable `spike/windows`, dont [`prototypes/windows/README.md`][essai] donne les commandes, les chiffres et
les pièges. Dans la distro WSL du portable ; la STL et le SDK de Microsoft lus dans l'installation de Visual
Studio Build Tools 2026 du PC (MSVC 14.51, SDK 10.0.26100), par un dossier `winsysroot` de deux liens.

- **Exactement C++23.** clang-cl 23.1.3, le LLVM de la CI, donne :

  | Option | `__cplusplus` et `_MSVC_LANG` |
  |---|---|
  | `/std:c++20` | 202002 |
  | `/std:c++latest` | 202700, le brouillon de C++26 |
  | `/clang:-std=c++23` | **202302** |

  CMake 4.2 traduit `CMAKE_CXX_STANDARD 23` en `-clang:-std=c++23` pour clang-cl. MSVC, lui, n'offre que
  `/std:c++latest`. `docs/QA.md` affirmait que clang-cl reproduit le piège `__cplusplus` de MSVC (199711) :
  c'est faux pour clang-cl 23.
- **Direct3D 12 sur la 4070.** Une sonde compile en 8,7 s, sans avertissement en `/W4 /WX`. Lancée depuis le
  terminal de WSL, elle tourne sous Windows et crée un device D3D12 sur la « RTX 4070 Laptop GPU » (shader model
  6.8). La couche de debug D3D12 est installée.
- **Les dépendances.** Les 25 ports vcpkg se compilent pour Windows depuis Linux, en 16 min au plus la première
  fois, dont 8,3 pour Dawn (la somme des temps que donne vcpkg). Trois ont demandé une parade (§ Conséquences).
- **Le moteur.** Il n'a pas une ligne propre à Linux : `grep` de `_WIN32`, `__linux__`, `unistd.h`, `dlfcn.h`,
  `sys/`, `inotify` et `pthread` ne trouve rien. Six fichiers ont dû être corrigés : des chemins en `wchar_t`
  passés à des fonctions C, un `<ostream>` manquant (le bug de M0.2), les macros `min`/`max` de `windows.h`, et
  `getenv`. Les shaders DXIL se compilaient déjà sous Linux (slangc et `libdxcompiler.so`).
- **Les tests.** `levain_tests.exe` passe 293 cas sur 295 sous Windows, en 8 s. `ctest` sur la Release en passe
  315 sur 337, et chacun des 22 échecs a sa cause, toutes liées aux outils ou aux tests eux-mêmes
  (§ Conséquences), sauf une : `gpu.environment.webgpu` trouve sur la 4070 une BRDF fausse (0,394 au lieu de 1
  vue de face), qu'il ne voit pas sous lavapipe.
- **Le sandbox tourne sur la 4070**, en Release et en Vulkan 1.4.351 : device créé en 321 ms, 822 images en
  5,0 s, 0,696 ms de GPU par image, capture juste. Ces chiffres sont indicatifs : les mesures du projet restent
  sur la machine de référence (SPECS § 10).

## Options envisagées

### 1. La chaîne de compilation

| Option | Pour | Contre |
|---|---|---|
| **A. clang-cl depuis Linux** (LLVM 23, lld-link, la STL et le SDK de Microsoft dans un `winsysroot`) | Un compilateur, une version, un environnement : ceux de la CI et de la machine de référence ; exactement C++23 ; mêmes avertissements et clang-tidy ; prouvé par l'essai | La STL et le SDK de Microsoft à fournir hors de Windows, sous la licence de Microsoft ; une toolchain et un triplet à nous ; les ports vcpkg ne sont pas testés avec clang-cl |
| B. clang-cl sous Windows | Même compilateur ; le chemin balisé de vcpkg et Visual Studio | Un second environnement à tenir (vcpkg, scripts, CMake sous Windows), que la machine de référence ne peut pas exécuter |
| C. MSVC sous Windows (l'ADR-0001) | Le chemin par défaut, le mieux outillé | `/std:c++latest` au lieu de C++23 ; d'autres avertissements ; les deux bugs de M0.2 ; un second environnement aussi |
| D. clang et MinGW-w64 depuis Linux (llvm-mingw) | Pas de SDK Microsoft ; la voie des binaires officiels de Godot | Une autre ABI que celle de Windows ; des triplets mingw de vcpkg communautaires ; D3D12 et sa couche de debug moins balisés |

### 2. L'API graphique sous Windows

| Option | Pour | Contre |
|---|---|---|
| **G1. Vulkan d'abord, D3D12 ensuite** | Rien de graphique à écrire pour voir le moteur sur la 4070 ; D3D12 vient sur une base qui compile et se teste | Deux étapes |
| G2. D3D12 tout de suite | Le contenu prévu de M1.4 d'un bloc | Toolchain, CI et backend dans la même étape |
| G3. Vulkan seul | Le moins de code | Ni WARP en CI, ni la couche de debug D3D12, ni le second backend que NVRHI rend presque gratuit |

### 3. La CI Windows

| Option | Pour | Contre |
|---|---|---|
| **C1. Compiler sur un runner Linux, tester sur un runner Windows** | La CI compile comme le développeur ; le runner Windows ne fait que lancer | Deux jobs liés par un artefact ; la STL et le SDK téléchargés par xwin [2] |
| C2. Tout sur un runner Windows (Visual Studio préinstallé) | Pas de xwin | La CI ne compilerait pas comme le développeur |
| C3. Linux seul, tests sous Wine | Un seul runner | Ni WARP ni couche de debug D3D12 sous Wine |

## Décision

**A, G1, C1, et maintenant** : la réponse de Donnovan au sondage, mot pour mot : « clang-cl depuis Linux
(Recommandé) », « Vulkan d'abord, D3D12 ensuite (Recommandé) », « Compiler sous Linux, tester sous Windows
(Recommandé) », « Maintenant, M7.2 ensuite (Recommandé) ».

1. **Windows se compile depuis Linux par clang-cl**, le LLVM de la CI, avec lld-link et les outils de `llvm-23`
   (`llvm-lib`, `llvm-rc`, `llvm-mt`). Une toolchain, `cmake/toolchains/windows-clang-cl.cmake`, que vcpkg
   charge pour chaque port comme pour Levain ; un triplet, `x64-windows-clang` (bibliothèques statiques, CRT en
   DLL) ; deux presets, `windows-debug` et `windows-release`.
2. **La STL et le SDK de Microsoft viennent d'un `winsysroot`**, désigné par `LEVAIN_WINSYSROOT` : la
   disposition de Visual Studio (`VC/`, `Windows Kits/`). Sur le portable, deux liens vers ses Build Tools ;
   sur la machine de référence et en CI, la sortie de xwin, à versions figées. Leur licence est celle de
   Microsoft, que Donnovan a acceptée par son choix (l'option du sondage le disait).
3. **Vulkan d'abord.** Le moteur tel quel tourne sous Windows ; le backend D3D12 (le contenu de M1.4 :
   `device_d3d12.cpp`, la swapchain DXGI, `--api vulkan|d3d12`, le test de fumée sous WARP) suit.
4. **La CI** : un job Linux compile `windows-release` et en fait un artefact ; un job Windows le lance. Les tests
   Vulkan n'ont pas de pilote sur un runner Windows : ils y sont écartés par un label, dont la liste s'affiche ;
   le test de fumée D3D12 sous WARP les y remplacera. Les checks requis de `main` ne changent qu'avec l'accord de
   Donnovan, après un premier passage.
5. **Maintenant** : M1.4 passe avant la fin de M7.2 (la PR des tests de refus, puis la clôture).

## Conséquences

### Ce que l'essai a dû régler, et que l'implémentation garde

- **clang-cl lit `-Wall` comme `/Wall`, c'est-à-dire `-Weverything`.** Notre `CMakeLists.txt` passe ses options
  de clang par `/clang:`. Un projet qui teste `CMAKE_CXX_COMPILER_ID` (« Clang » sous clang-cl) pour ajouter
  `-Wall -Werror` se compile avec tous les avertissements, en erreurs : ozz (son port overlay lui fait prendre sa
  branche MSVC, et la CRT du triplet), spirv-reflect. Les ports de vcpkg ne sont pas testés avec clang-cl : un
  port qui casse reçoit sa parade dans un port overlay, notée dans `build/GOTCHA.md`.
- **vcpkg n'ôte `/MP` que pour un compilateur nommé `clang-cl.exe`** : la toolchain le retire.
- **Les couches de validation Vulkan** viennent du système, comme `apt` sous Linux : le Vulkan SDK de LunarG,
  installé sous Windows. Le port vcpkg, qui tirait spirv-reflect, est écarté. Le Debug refuse de démarrer sans
  elles, comme sous Linux (règle n°7) ; le message nommera l'installeur de Windows.
- **Dawn ne trouve pas `vulkan-1.dll`** (« Windows Error: 87 ») et retombe en silence sur son backend Null : il
  reçoit le dossier `System32`. Un test WebGPU refusera désormais le backend Null (règle n°7) : sous Windows,
  quatre tests sur sept passaient sans rien dessiner.
- **Chaque programme tourne en UTF-8** (un manifeste, `activeCodePage`, Windows 10 1903 et plus) : sans lui,
  argv arrive dans la page de code ANSI, et `ctest` ne trouvait pas les cas de test accentués.
- **Pas de `_CRT_SECURE_NO_WARNINGS`** : la CRT de Microsoft déclare « unsafe » `getenv`, et l'essai l'a défini
  pour avancer. Ce serait couper un avertissement (règle n°4) : les appels passent par des fonctions portables.
  `NOMINMAX`, lui, reste : il retire des macros, pas un avertissement.

### Ce qu'il reste à adapter, sans rien couper

- **Les contrôles par `nm`** (`build.no-tracy`, `build.no-editor`, `build.editor-symbols`) : sous Windows, les
  symboles sont dans le PDB ; ils le liront par `llvm-pdbutil`.
- **`runProcess`** et le rechargement à chaud des shaders lancent `cmake` et `slangc` de l'hôte, des binaires
  Linux qu'un exe Windows ne peut pas exécuter. Le test lancera un programme de la cible ; le rechargement des
  shaders, sous Windows, refusera avec un message, jusqu'à un `slangc` Windows si le besoin vient.
- **`cmake.plugins.*`** configurent un mini-moteur : ils transmettront la toolchain et le triplet.
- **La découverte de doctest** lance l'exe pendant le build : l'interop de WSL l'exécute, pas un runner Linux. Elle
  passera au moment des tests (`DISCOVERY_MODE PRE_TEST`).
- **`waitEvents`** : sous Windows, SDL reçoit d'autres événements que ceux de la fenêtre ; le test l'admettra.
- **Les chemins Linux compilés dans les tests** ne marchent sous Windows que lancés depuis WSL ; un runner
  Windows les recevra de `ctest`.
- **La BRDF de `gpu.environment.webgpu` sur la 4070** : une issue à part, peut-être un vrai bug que lavapipe
  cache.

### Le reste

- **Les PR**, une à la fois ou empilées (règle n°1) : cet ADR ; la chaîne (toolchain, triplet, presets,
  manifeste, corrections du moteur, Dawn) ; les tests ; la CI ; le backend D3D12. L'essai fait 190 lignes hors
  prototypes : chacune tient sous 400 lignes.
- **La distro WSL et la CI** installent `llvm-23` ; `tools/verify.sh` compilera aussi `windows-debug`.
- **Roadmap v0.15** : M1.4 rouvert, 1,5 h Donnovan (0,75 pour Windows en Vulkan, 0,75 pour D3D12) ; total
  65,2 → 66,7 h, échéances inchangées.
- **SPECS** : Windows 10/11 par clang-cl, compilé depuis Linux ; § 10, le portable de Donnovan devient la vraie
  machine Windows, sans en faire une machine de référence.

## Ce que font les autres moteurs

- **Unreal** compile pour Linux depuis Windows, avec une chaîne clang qu'Epic fournit par version du moteur ; seul
  Windows est pris en charge comme hôte de cette compilation croisée [3].
- **Unity** compile ses lecteurs Linux IL2CPP depuis n'importe quel système de bureau, par des paquets de sysroot
  et de toolchain [4] ; IL2CPP passe par le compilateur C++ natif de la plateforme [5].
- **Godot** construit tous ses binaires officiels dans des conteneurs, avec MinGW ; MinGW-LLVM est pris en charge,
  et D3D12 s'active par une option [6].
- **Firefox** compile ses builds Windows sur Linux par clang-cl, depuis 2020 : plus rapide à matériel égal, sans
  licence Windows par machine ; Mozilla ne livre plus de build MSVC depuis Firefox 63 [7].
- **Chromium** compile la plupart de ses cibles Windows depuis Linux ; le SDK s'empaquette depuis une machine
  Windows, et un bot lance les tests du build croisé [8].

## Sources

[essai]: https://github.com/PhantomDO/Levain/blob/spike/windows/prototypes/windows/README.md

1. Dozen à Vulkan 1.2 : <https://www.phoronix.com/news/Microsoft-Dzn-Vulkan-1.2> ; pas de `nvidia_icd.json` sous
   WSL : <https://github.com/microsoft/WSL/issues/12313>
2. xwin : <https://github.com/Jake-Shadle/xwin>
3. <https://dev.epicgames.com/documentation/unreal-engine/linux-development-requirements-for-unreal-engine>
4. <https://docs.unity3d.com/Packages/com.unity.sysroot@2.0/manual/index.html>
5. <https://docs.unity3d.com/Manual/scripting-backends-intro.html>
6. <https://docs.godotengine.org/en/4.6/engine_details/development/compiling/compiling_for_windows.html>
7. <https://glandium.org/blog/?p=4020>
8. <https://chromium.googlesource.com/chromium/src/+/main/docs/win_cross.md>
