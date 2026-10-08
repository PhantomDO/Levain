#!/usr/bin/env bash
# La STL et le SDK de Microsoft pour compiler Windows depuis Linux (ADR-0035, décision 2), téléchargés par xwin dans
# un winsysroot, aux versions que fige cmake/toolchains/windows-clang-cl.cmake. Le script de la CI et de la machine de
# référence ; dans la distro WSL d'un PC Windows, le winsysroot lit plutôt les Build Tools installés (tools/wsl/).
#
#   tools/winsysroot.sh <dossier>   # puis : export LEVAIN_WINSYSROOT=<dossier>
#   tools/winsysroot.sh --stamp     # le tampon de ce winsysroot, sans rien télécharger (la CI en tire son chemin)
#
# Tout est figé et vérifié par SHA-256 : xwin, et le manifeste des paquets de Visual Studio 18.8.1, celui des Build
# Tools de Donnovan (octet pour octet le catalog.json de son installation), contre lequel xwin vérifie chaque paquet.
# Le manifeste du jour bougerait sous nos pieds : celui de 18.10.3 garde MSVC 14.51.36231, bibliothèques recompilées.
# Leur licence est celle de Microsoft, que Donnovan accepte par son choix (ADR-0035) : d'où --accept-license. Le
# winsysroot sert à compiler, et ne va ni dans le dépôt ni dans un artefact.
set -euo pipefail

dest=${1:?usage : tools/winsysroot.sh <dossier> | --stamp}
root=$(cd "$(dirname "$0")/.." && pwd)
# Les versions, lues dans la toolchain qui les vérifie : une seule source.
toolchain=$root/cmake/toolchains/windows-clang-cl.cmake
msvc=$(sed -nE 's/^set\(levainMsvcVersion ([0-9.]+)\)$/\1/p' "$toolchain")
sdk=$(sed -nE 's/^set\(levainWinSdkVersion ([0-9.]+)\)$/\1/p' "$toolchain")
[[ $msvc =~ ^14\.[0-9]+\.[0-9]+$ && $sdk =~ ^10\.0\.[0-9]+\.0$ ]] \
    || { echo "versions de MSVC ou du SDK illisibles dans $toolchain" >&2; exit 1; }

xwinVersion=0.10.0
xwinSha256=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88
vsmanUrl=https://download.visualstudio.microsoft.com/download/pr/2d2982b2-bb55-4ed1-981b-9c3fc7bf3b12/ce889cdc10c284ec9a5d14a0893d9fd0df6487cb6c076ddb631f45c0291b5416/VisualStudio.vsman
vsmanSha256=530f1ebd84e4bbd51f646cbb41459f5aeaa78a5e716009d9257ea29a5dc5c94d

# Le tampon nomme ce qui fait le contenu du winsysroot : xwin, le manifeste, les versions, et le code de ce script
# (ses lignes de commentaire et ses lignes vides exclues). La CI en tire le chemin du winsysroot, qui entre dans l'ABI
# de tous les ports Windows (ENV:LEVAIN_WINSYSROOT, dans leur vcpkg_abi_info.txt) : changer une ligne de code ici
# les recompile tous sur le runner (1 h 40), corriger un commentaire, non. Les versions seules ne suffiraient pas :
# un autre manifeste garde MSVC 14.51.36231 avec d'autres bibliothèques, et le chemin doit alors changer.
code=$(grep -vE '^[[:space:]]*(#|$)' "$0" | sha256sum | cut -c1-12)
stamp="xwin $xwinVersion, manifeste $vsmanSha256, MSVC $msvc, SDK $sdk, code $code"
if [[ $dest == --stamp ]]; then
    echo "$stamp"
    exit 0
fi
if [[ -f $dest/.levain-winsysroot && $(< "$dest/.levain-winsysroot") == "$stamp" ]]; then
    echo "$dest : déjà le winsysroot de $stamp"
    exit 0
fi
[[ ! -e $dest ]] || { echo "$dest existe et n'est pas le winsysroot de $stamp : l'effacer d'abord" >&2; exit 1; }
mkdir -p "$(dirname "$dest")"
work=$(mktemp -d "$(dirname "$dest")/.winsysroot.XXXXXX") # même disque que $dest : le mv final ne copie rien
trap 'rm -rf "$work"' EXIT

fetch() { # $1 = adresse, $2 = fichier, $3 = SHA-256 attendu
    curl --fail --silent --show-error --location --retry 3 "$1" --output "$2"
    echo "$3  $2" | sha256sum --check --status || { echo "SHA-256 inattendu pour $1" >&2; exit 1; }
}
fetch "https://github.com/Jake-Shadle/xwin/releases/download/$xwinVersion/xwin-$xwinVersion-x86_64-unknown-linux-musl.tar.gz" \
    "$work/xwin.tar.gz" "$xwinSha256"
tar -xzf "$work/xwin.tar.gz" -C "$work" --strip-components=1
fetch "$vsmanUrl" "$work/VisualStudio.vsman" "$vsmanSha256"

# La CRT universelle (les en-têtes C de Windows) : xwin préfère le paquet à part (10.0.26624.1), quand Visual Studio
# installe celle du SDK, celle du PC, octet pour octet ; et il ne la trouve que sous « Installers/ », que ce
# manifeste écrit « Installers\ » (« unable to find Universal CRT MSI », xwin #188). Deux corrections du manifeste,
# vérifiées : une autre version du manifeste les ferait échouer au lieu de passer à côté (règle n°7).
mkdir -p "$work/cache/dl"
vsmanKey=$(basename "$(dirname "$vsmanUrl")") # le nom sous lequel xwin cherche le manifeste dans son cache
python3 -I - "$work/VisualStudio.vsman" "$work/cache/dl/pkg_manifest_$vsmanKey.vsman" "Win11SDK_${sdk%.0}" << 'PATCH'
import json, sys
source, target, sdk = sys.argv[1:]
manifest = json.load(open(source, encoding="utf-8-sig"))
packages = [p for p in manifest["packages"] if p["id"] != "Microsoft.Windows.UniversalCRT.HeadersLibsSources.Msi"]
dropped = len(manifest["packages"]) - len(packages)
msi = "Universal CRT Headers Libraries and Sources-x86_en-us.msi"
fixed = 0
for payload in (pl for p in packages if p["id"] == sdk for pl in p.get("payloads", [])):
    if payload["fileName"] == "Installers\\" + msi:
        payload["fileName"] = "Installers/" + msi
        fixed += 1
if dropped == 0 or fixed != 1:
    sys.exit(f"manifeste inattendu : {dropped} paquet(s) de la CRT universelle retiré(s), {fixed} chemin(s) corrigé(s)")
manifest["packages"] = packages
json.dump(manifest, open(target, "w"))
PATCH
# xwin lit d'abord le manifeste du canal, qui donne le lien du manifeste des paquets : un canal réduit à ce lien.
printf '{"channelItems":[{"id":"Microsoft.VisualStudio.Manifests.VisualStudio","version":"18.8.1","type":"Manifest","payloads":[{"fileName":"VisualStudio.vsman","sha256":"%s","size":0,"url":"%s"}]}]}' \
    "$vsmanKey" "$vsmanUrl" > "$work/channel.json"

# Les bibliothèques de Debug de la CRT (--include-debug-libs) : le preset windows-debug lie /MDd.
"$work/xwin" --accept-license --log-level warn --manifest "$work/channel.json" --cache-dir "$work/cache" \
    --crt-version "${msvc%.*}" --sdk-version "${sdk%.0}" \
    splat --include-debug-libs --preserve-ms-arch-notation --use-winsysroot-style --output "$work/out"

# xwin nomme les dossiers des versions courtes ; clang-cl (/vctoolsversion, /winsdkversion) et la toolchain veulent
# les longues. Le paquet de la STL dit la sienne : elle doit être celle de la toolchain.
python3 -I -c 'import sys, zipfile; names = zipfile.ZipFile(sys.argv[1]).namelist()
sys.exit(0 if any(n.startswith(sys.argv[2]) for n in names) else f"le paquet de la STL n est pas {sys.argv[2]}")' \
    "$work/cache/dl/Microsoft.VC.${msvc%.*}.CRT.Headers.base.vsix" "Contents/VC/Tools/MSVC/$msvc/"
mv "$work/out/VC/Tools/MSVC/${msvc%.*}" "$work/out/VC/Tools/MSVC/$msvc"
for kind in Include Lib; do
    mv "$work/out/Windows Kits/10/$kind/${sdk%.0}" "$work/out/Windows Kits/10/$kind/$sdk"
done
echo "$stamp" > "$work/out/.levain-winsysroot"
mv "$work/out" "$dest"
echo "$dest : winsysroot de $stamp ; export LEVAIN_WINSYSROOT=$dest"
