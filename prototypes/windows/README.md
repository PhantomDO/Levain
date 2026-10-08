# Essai : Levain compilé pour Windows depuis Linux, par clang-cl

Branche jetable `spike/windows`, faite le 2026-10-08 avant l'ADR-0035, dans la distro WSL `levain-dev` du
portable de Donnovan (RTX 4070 Laptop, Windows 11). Les mesures de l'ADR viennent des commandes ci-dessous.

## Ce que l'essai ajoute au dépôt

- `cmake/toolchains/windows-clang-cl.cmake` : clang-cl, lld-link, llvm-lib, llvm-rc et llvm-mt de LLVM 23, la
  STL et le SDK de Microsoft lus dans `LEVAIN_WINSYSROOT` ; les options de vcpkg pour Windows, reprises de son
  `scripts/toolchains/windows.cmake`.
- `triplets/x64-windows-clang.cmake` : bibliothèques statiques, CRT en DLL ; la toolchain ci-dessus.
- Les presets `windows-debug` et `windows-release`.
- `vcpkg.json` : Wayland et X11 réservés à Linux ; les couches de validation Vulkan pour Windows. Les ports
  overlay `tracy` et `ozz-animation` acceptent Windows.
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

À compléter.

## Pièges rencontrés

- **Dans un `try_compile`, `CMAKE_TOOLCHAIN_FILE` n'est pas défini** : la toolchain trouve le dossier de vcpkg
  par `CMAKE_PARENT_LIST_FILE`, le `vcpkg.cmake` qui l'inclut.
- **vcpkg n'ôte `/MP` que pour un compilateur nommé `clang-cl.exe`** : sous Linux, ktx (compilé en `-Werror`)
  s'arrêtait sur « argument unused during compilation: '/MP' ». La toolchain le retire.
- **clang-cl lit `-Wall` comme `/Wall`, c'est-à-dire `-Weverything`** : un projet qui prend clang-cl pour GCC
  (ozz, dont le test vise `CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"`) se compile avec tous les avertissements,
  traités en erreurs. Notre `CMakeLists.txt` et le port overlay d'ozz passent par `/clang:-Wall`.
- **Modifier la toolchain change l'ABI de chaque port** : vcpkg recompile toutes les dépendances Windows (Dawn
  compris, 8 min).
