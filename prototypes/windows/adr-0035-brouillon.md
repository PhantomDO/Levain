# ADR-0035 — Windows revient, compilé depuis Linux par clang-cl

- **Statut** : proposé le 2026-10-08, en attente du sondage
- **Modifie** : [ADR-0011](0011-retour-au-cpp.md) § 1 (« Linux d'abord » : Windows revient, et le compilateur
  est choisi) ; [ADR-0001](0001-langage-cpp23.md) (« MSVC sous Windows ») ; corrige `docs/QA.md` sur
  `__cplusplus`
- **Date** : 2026-10-08
- **Milestone** : M1.4, rouvert (placement au sondage)

## Contexte

L'ADR-0011 différait Windows et Direct3D 12 « jusqu'à ce qu'une machine Windows soit disponible ». Depuis le
2026-10-08, Donnovan travaille aussi depuis un portable Windows : RTX 4070 Laptop (8 Go) et iGPU Intel UHD,
64 Go de RAM, avec la distro WSL `levain-dev` de `tools/wsl/`. M8.2 demande de toute façon des binaires
Windows produits par la CI.

**Sous WSL, pas de Vulkan Linux sur ce GPU.** WSL partage la carte (CUDA, D3D12), mais le pilote NVIDIA pour WSL
ne fournit pas de pilote Vulkan : `/usr/lib/wsl/lib` n'a pas de `nvidia_icd.json` (pilote 617.42). Le seul
chemin, Dozen (Mesa, Vulkan traduit en D3D12), plafonne à Vulkan 1.2, quand `engine/gpu/src/device_vk.cpp`
exige 1.3 ; le Mesa d'Ubuntu 26.04 ne le contient pas. **Faire tourner Levain sur ce GPU, c'est produire un
`.exe` Windows.**

**Donnovan veut garder clang sur les deux plateformes**, en compilant pour Windows depuis Linux, « comme pour
Unreal » (qui fait le chemin inverse, de Windows vers Linux).

### Ce que l'essai a mesuré

Branche jetable `spike/windows`, `prototypes/windows/README.md` ; sur le portable, dans la distro WSL ; la STL et
le SDK de Microsoft lus dans l'installation de Visual Studio Build Tools 2026 du PC (MSVC 14.51, SDK
10.0.26100), par un dossier `winsysroot` de deux liens.

- **Une sonde D3D12 compile en 8,7 s** par clang-cl 23.1.3, le LLVM de la CI, sans avertissement en `/W4 /WX`.
  **Lancée depuis le terminal WSL**, elle tourne sous Windows et crée un device D3D12 sur la « RTX 4070 Laptop
  GPU », shader model 6.8 ; la couche de debug D3D12 est installée (`prototypes/windows/probes.sh`).
- **`__cplusplus` est juste sous clang-cl**, quelle que soit l'option, et `docs/QA.md` (« clang-cl reproduit
  volontairement le bug `__cplusplus` ») est faux pour clang-cl 23 :

  | Option | `__cplusplus` et `_MSVC_LANG` |
  |---|---|
  | `/std:c++20` | 202002 |
  | `/std:c++latest` | 202700 (le brouillon de C++26) |
  | `/clang:-std=c++23` | **202302** |

  CMake 4.2 traduit `CMAKE_CXX_STANDARD 23` en `-clang:-std=c++23` pour clang-cl : **exactement C++23**, là où
  MSVC n'offre que `/std:c++latest`.
- **Le moteur n'a pas une ligne propre à Linux** : `grep` de `_WIN32`, `__linux__`, `unistd.h`, `dlfcn.h`,
  `sys/`, `inotify`, `pthread` dans `engine/`, `plugins/`, `editor/`, `sandbox/`, `tests/` : rien.
- **Les shaders DXIL se compilent déjà sous Linux** (slangc et `libdxcompiler.so`, `cmake/LevainShaders.cmake`),
  et un test les désassemble.
- **Les dépendances** : À MESURER (30 paquets vcpkg pour `x64-windows-clang`, Dawn compris ; temps ; ce qui a
  cassé).
- **Le moteur** : À MESURER (`levain_tests.exe` sous Windows ; `levain_sandbox.exe` en Vulkan sur la 4070).

## Options envisagées

### 1. La chaîne de compilation

| Option | Pour | Contre |
|---|---|---|
| **A. clang-cl depuis Linux** (LLVM 23, lld-link, la STL et le SDK de Microsoft dans un `winsysroot`) | Un compilateur, une version, un environnement : celui de la CI et de la machine de référence ; exactement C++23 ; mêmes avertissements et clang-tidy ; prouvé par l'essai | La STL et le SDK de Microsoft à fournir hors de Windows (xwin, sous licence Microsoft) ; un fichier de toolchain et un triplet à nous ; chemin moins courant que Visual Studio |
| B. clang-cl sous Windows | Même compilateur ; chemin balisé (vcpkg, Visual Studio) | Un second environnement à tenir (vcpkg, scripts, CMake sous Windows), que la machine de référence ne peut pas exécuter |
| C. MSVC sous Windows (l'ADR-0001) | Le chemin par défaut, le mieux outillé | `/std:c++latest` au lieu de C++23 ; d'autres avertissements ; les deux bugs de M0.2 ; un second environnement aussi |
| D. clang et MinGW-w64 depuis Linux (llvm-mingw) | Pas de SDK Microsoft à télécharger ; la voie des binaires officiels de Godot | Une autre ABI que celle de Windows ; triplets mingw de vcpkg communautaires ; D3D12 et sa couche de debug moins balisés |

### 2. Quelle API graphique sous Windows

| Option | Pour | Contre |
|---|---|---|
| **G1. Vulkan d'abord, D3D12 ensuite** | Rien de graphique à écrire pour voir le moteur sur la 4070 ; D3D12 (M1.4) vient après, sur une base qui compile | Deux étapes |
| G2. D3D12 tout de suite | Le contenu prévu de M1.4 d'un bloc | Toolchain, CI et backend dans la même étape |
| G3. Vulkan seul sous Windows | Le moins de code | Abandonne D3D12, WARP en CI, et la couche de debug D3D12 |

### 3. La CI Windows

| Option | Pour | Contre |
|---|---|---|
| **C1. Compiler sur un runner Linux (xwin), tester sur un runner Windows** | La CI compile comme le développeur ; un runner Windows ne fait que lancer | Deux jobs liés par un artefact ; la STL et le SDK téléchargés par xwin, sous la licence de Microsoft |
| C2. Tout sur un runner Windows (Visual Studio préinstallé) | Pas de xwin | La CI ne compilerait pas comme le développeur |
| C3. Linux seul, tests sous Wine | Un seul runner | Ni WARP ni couche de debug D3D12 sous Wine ; un environnement de plus à comprendre |

Sous Windows en CI, pas de GPU : D3D12 a WARP, intégré à Windows ; Vulkan n'a pas d'équivalent fourni, et ses tests
n'y tournent pas tant qu'un pilote logiciel (lavapipe pour Windows) n'est pas ajouté.

## Décision

Recommandé : **A, G1, C1**. À compléter après le sondage.

## Conséquences

- Nouveaux fichiers : `cmake/toolchains/windows-clang-cl.cmake`, `triplets/x64-windows-clang.cmake`, presets
  `windows-debug` et `windows-release` ; `LEVAIN_WINSYSROOT` désigne la STL et le SDK (une installation de Visual
  Studio, ou la sortie de xwin).
- `vcpkg.json` : Wayland et X11 réservés à Linux ; les ports overlay `tracy` et `ozz-animation` acceptent
  Windows.
- `CMakeLists.txt` : clang-cl lit `-Wall` comme `/Wall` (`-Weverything`) ; les options de clang y passent par
  `/clang:`.
- Les outils LLVM (`llvm-lib`, `llvm-rc`, `llvm-mt`) entrent dans la distro et la CI (`llvm-23`).
- Règle n°4 sous Windows : les couches de validation Vulkan (port vcpkg) et la couche de debug D3D12.
- SPECS § 10 : un vrai PC Windows est disponible, mais **les mesures de performance restent sur la machine de
  référence**.

## Ce que font les autres moteurs

- **Unreal** compile pour Linux depuis Windows, avec une chaîne clang qu'Epic fournit par version du moteur ; seul
  Windows est pris en charge comme hôte de cette compilation croisée ([doc d'Epic][ue-linux]).
- **Unity** compile ses lecteurs Linux IL2CPP depuis n'importe quel système de bureau, par des paquets de sysroot
  et de toolchain ([sysroot][unity-sysroot]) ; IL2CPP passe par le compilateur C++ natif de la plateforme
  ([backends][unity-backends]).
- **Godot** construit tous ses binaires officiels dans des conteneurs, avec MinGW ; MinGW-LLVM est pris en charge,
  et D3D12 s'active par une option ([compiler pour Windows][godot-win]).
- **Firefox** compile ses builds Windows sur Linux par clang-cl depuis 2020 (moins cher, plus rapide à matériel
  égal) ; Mozilla ne livre plus de build MSVC depuis Firefox 63 ([Mike Hommey][ff-cross]).
- **Chromium** compile la plupart de ses cibles Windows depuis Linux ; le SDK est empaqueté depuis une machine
  Windows, et un bot lance les tests du build croisé ([win_cross.md][cr-cross]).

[ue-linux]: https://dev.epicgames.com/documentation/unreal-engine/linux-development-requirements-for-unreal-engine
[unity-sysroot]: https://docs.unity3d.com/Packages/com.unity.sysroot@2.0/manual/index.html
[unity-backends]: https://docs.unity3d.com/Manual/scripting-backends-intro.html
[godot-win]: https://docs.godotengine.org/en/4.6/engine_details/development/compiling/compiling_for_windows.html
[ff-cross]: https://glandium.org/blog/?p=4020
[cr-cross]: https://chromium.googlesource.com/chromium/src/+/main/docs/win_cross.md

## Sources

- Dozen à Vulkan 1.2 : <https://www.phoronix.com/news/Microsoft-Dzn-Vulkan-1.2>
- Pas de `nvidia_icd.json` sous WSL : <https://github.com/microsoft/WSL/issues/12313>
- xwin : <https://github.com/Jake-Shadle/xwin>
