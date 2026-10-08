# Essai : Levain compilé pour Windows depuis Linux, par clang-cl

Branche jetable `spike/windows`, faite le 2026-10-08 avant l'ADR-0035, dans la distro WSL `levain-dev` du
portable de Donnovan (RTX 4070 Laptop, Windows 11). Les mesures de l'ADR viennent des commandes ci-dessous.

## Ce que l'essai ajoute au dépôt

- `cmake/toolchains/windows-clang-cl.cmake` : clang-cl, lld-link, llvm-lib, llvm-rc et llvm-mt de LLVM 23, la
  STL et le SDK de Microsoft lus dans `LEVAIN_WINSYSROOT` ; les options de vcpkg pour Windows, reprises de son
  `scripts/toolchains/windows.cmake`.
- `triplets/x64-windows-clang.cmake` : bibliothèques statiques, CRT en DLL ; la toolchain ci-dessus.
- Les presets `windows-debug` et `windows-release`.
- `vcpkg.json` : Wayland et X11 réservés à Linux. Les ports overlay `tracy` et `ozz-animation` acceptent
  Windows ; celui d'ozz prend sa branche MSVC.
- `CMakeLists.txt` : les avertissements passés par `/clang:` sous clang-cl, qui lit `-Wall` comme `/Wall`.

## Préparer

```bash
sudo apt install llvm-23                      # llvm-lib, llvm-rc, llvm-mt
# Le winsysroot : la disposition de Visual Studio. Ici, des liens vers les Build Tools 2026 installés sous
# Windows (MSVC 14.51, SDK 10.0.26100) ; ailleurs, la sortie de xwin.
mkdir -p ~/winsysroot
ln -sfn "/mnt/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC" ~/winsysroot/VC
ln -sfn "/mnt/c/Program Files (x86)/Windows Kits" "$HOME/winsysroot/Windows Kits"
export LEVAIN_WINSYSROOT=$HOME/winsysroot
```

## Rejouer

```bash
prototypes/windows/probes.sh                  # __cplusplus selon l'option, puis la sonde D3D12 sous Windows
cmake --preset windows-debug                  # vcpkg compile les dépendances pour Windows
cmake --build --preset windows-debug
```

## Mesuré

Sur le portable (Core i9-14900HX, 32 threads, et RTX 4070 Laptop ; 47 Go donnés à WSL), le 2026-10-08. Indicatif : les
mesures de performance du projet restent sur la machine de référence (SPECS § 10).

- **Les sondes** (`probes.sh`) : `__cplusplus` vaut 202002, 202700 et 202302 sous `/std:c++20`,
  `/std:c++latest` et `/clang:-std=c++23` ; la sonde D3D12 compile en 8,7 s sans avertissement en `/W4 /WX`,
  puis, lancée depuis WSL, crée un device D3D12 sur la 4070 (shader model 6.8), avec la couche de debug.
- **Les dépendances** (`cmake --preset windows-debug`, lignes « Elapsed time to handle » de vcpkg) : les 25
  ports se compilent pour Windows, 15,8 min au plus la première fois avec `VCPKG_MAX_CONCURRENCY=24` (la somme,
  pour chaque port, de son plus long passage), dont 8,3 min pour Dawn. Trois ports ont demandé une parade (Pièges) ; les couches de validation Vulkan sont
  retirées de l'essai.
- **Le moteur** : 200 étapes ; six fichiers ne compilaient pas : quatre corrigés dans le code (chemins en
  `wchar_t`, `<ostream>`), deux par des définitions (`NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, que l'ADR-0035 ne
  garde pas). Puis tout se lie. Release en 71 s, configuration comprise (`cmake --preset windows-release`, puis
  `cmake --build --preset windows-release`).
- **`levain_tests.exe` sous Windows** (`time build/windows-debug/tests/levain_tests.exe`, depuis la distro) :
  293 cas sur 295 en 8,5 s (deux passages) ; les deux échecs lancent le `cmake` de Linux depuis l'exe (`runProcess`). Les tests
  WebGPU tournent sur la 4070 par Dawn et Vulkan ; avant la parade de Dawn, six des neuf passaient sur son
  backend Null, sans rien dessiner.
- **`ctest` sur la Release** (`SDL_VIDEO_DRIVER=offscreen ctest --test-dir build/windows-release -j 8`) : 315 sur
  337. Les 22 échecs : 8 programmes GPU et tests de fumée qui exigent les couches de validation, absentes, en
  Release aussi (refus bruyant) ; 7 `cmake.plugins.*`, qui configurent un mini-moteur sans la toolchain croisée ;
  3 contrôles par `nm`, dont les symboles sont dans le PDB sous Windows ; les 2 `runProcess` ; `waitEvents`, qui
  passe quand l'exe tourne seul et échoue sous `ctest -j 8`, sans sortie (cause à trouver) ; et
  `gpu.environment.webgpu`, qui trouve sur la 4070 un reflet préfiltré et une BRDF faux (0,394 au lieu de 1 vue de
  face), que lavapipe ne montre pas (#347).
- **Le sandbox sur la 4070**, Release, Vulkan 1.4.351, pilote 617.42 :
  `levain_sandbox.exe --seconds 5 --capture … --model assets-cache/Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf`
  (le camion et les HDRI copiés dans `assets-cache` du worktree) : device en 321 ms, 822 images en 5,0 s,
  0,696 ms de GPU par image, capture de 1920 × 1080 juste.

## Pièges rencontrés

- **Dans un `try_compile`, `CMAKE_TOOLCHAIN_FILE` n'est pas défini** : la toolchain trouve le dossier de vcpkg
  par `CMAKE_PARENT_LIST_FILE`, le `vcpkg.cmake` qui l'inclut.
- **vcpkg n'ôte `/MP` que pour un compilateur nommé `clang-cl.exe`** : sous Linux, ktx (compilé en `-Werror`)
  s'arrêtait sur « argument unused during compilation: '/MP' ». La toolchain le retire.
- **clang-cl lit `-Wall` comme `/Wall`, c'est-à-dire `-Weverything`** : un projet qui teste
  `CMAKE_CXX_COMPILER_ID` (« Clang » sous clang-cl) pour ajouter `-Wall -Werror` se compile avec tous les
  avertissements, traités en erreurs. Rencontré trois fois : notre `CMakeLists.txt` (passé par `/clang:`), ozz
  (son port overlay lui fait prendre sa branche MSVC, qui définit aussi `_CRT_SECURE_NO_WARNINGS`), et
  spirv-reflect, dépendance des couches de validation Vulkan : retirées de vcpkg pour l'essai. Les ports de vcpkg
  ne sont pas testés avec clang-cl : il faudra une parade générale.
- **Les couches de validation Vulkan** viennent du système sous Linux (`apt`) ; sous Windows, l'essai les a
  retirées. L'ADR-0035 les prend du port vcpkg (décision 6), avec une parade pour spirv-reflect.
- **ozz choisit sa CRT** sur sa branche MSVC, statique par défaut : lld-link refusait de le lier au reste
  (`/failifmismatch` sur `RuntimeLibrary`). Son port suit le triplet (`ozz_build_msvc_rt_dll`).
- **Dawn ne trouve pas `vulkan-1.dll`** sous Windows (« Windows Error: 87 ») : il ne cherche qu'à côté de lui et
  de l'exe, puis sans chemin avec `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`, qui exige un chemin complet. Il retombe en
  silence sur son backend Null, où les tests qui ne relisent pas d'image passent sans rien dessiner.
  `engine/gpu/src/webgpu/create.cpp` lui donne `System32`. Son backend D3D12, lui, veut copier
  `d3dcompiler_47.dll` d'un SDK lu dans le registre, et compiler DXC : écarté de l'essai.
- **Un programme Windows reçoit argv dans la page de code ANSI** : les cas de test accentués, lancés un par un
  par `ctest`, n'étaient pas trouvés (« test cases: 0 », que le garde-fou du projet refuse). Le manifeste
  `cmake/windows/utf8.manifest` met chaque programme en UTF-8.
- **Lancé depuis WSL, un exe lit un chemin Linux absolu** (`/home/…`) sous la racine de la distro, parce que son
  dossier courant est `\\wsl.localhost\levain-dev\…` : les chemins compilés dans les tests marchent ici, pas sur
  un runner Windows. En revanche, Windows ne suit pas un lien symbolique de la distro.
- **La découverte des tests de doctest lance l'exe pendant le build** : ici l'interop de WSL l'exécute ; sur un
  runner Linux, il faudra la découverte au moment des tests.
- **Modifier la toolchain change l'ABI de chaque port** : vcpkg recompile toutes les dépendances Windows (Dawn
  compris, 8 min).
