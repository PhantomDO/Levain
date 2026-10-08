#!/usr/bin/env bash
# Les deux sondes de l'essai Windows, compilées sous Linux par clang-cl. La seconde ne se lance que là où
# Windows est joignable : la distro WSL, dont l'interop exécute un .exe sous Windows.
#   LEVAIN_WINSYSROOT=~/winsysroot prototypes/windows/probes.sh
set -euo pipefail

: "${LEVAIN_WINSYSROOT:?le dossier qui contient VC/Tools/MSVC et « Windows Kits/10 »}"
llvm=/usr/lib/llvm-23/bin
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cl=("$llvm/clang-cl" --target=x86_64-pc-windows-msvc /winsysroot "$LEVAIN_WINSYSROOT" /nologo)

echo "== __cplusplus selon l'option de norme"
for std in /std:c++20 /std:c++latest /clang:-std=c++23; do
    printf '%-18s ' "$std"
    "${cl[@]}" "$std" /E "$here/cplusplus-probe.cpp" | grep -E '^(long|int) ' | tr '\n' ' '
    echo
done

echo "== sonde D3D12"
start=$(date +%s.%N)
"${cl[@]}" /clang:-std=c++23 /EHsc /W4 /WX /MT /O2 /Z7 -fuse-ld=lld "$here/d3d12-probe.cpp" \
    /Fe:"$out/d3d12-probe.exe" /Fo:"$out/" /link d3d12.lib dxgi.lib /DEBUG
printf 'compilée en %.1f s\n' "$(echo "$(date +%s.%N) - $start" | bc)"
if [[ -e /proc/sys/fs/binfmt_misc/WSLInterop || -e /proc/sys/fs/binfmt_misc/WSLInterop-late ]]; then
    "$out/d3d12-probe.exe" | tr -d '\r'
else
    echo "pas d'interop WSL ici : l'exe n'est pas lancé"
fi
