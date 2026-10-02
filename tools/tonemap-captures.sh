#!/usr/bin/env bash
# Les captures comparatives du tonemapping (#127) : le même plan figé, pour chaque courbe (coupe nette,
# ACES, AgX) et deux expositions, dans <dossier>/<courbe>-x<exposition>.png.
#   tools/tonemap-captures.sh <dossier> [modèle glTF] [options du sandbox…]
# Par défaut : le camion de tools/assets.lock, à --time 2 (le soleil et les lumières de couleur).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage : tools/tonemap-captures.sh <dossier> [modèle glTF] [options du sandbox…]}
model=${2:-$root/assets-cache/Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf}
shift $(( $# < 2 ? $# : 2 ))
sandbox=${SANDBOX:-$root/build/linux-release/sandbox/levain_sandbox}
[[ -x $sandbox ]] || { echo "ÉCHEC : $sandbox absent (preset linux-release, ou SANDBOX=…)" >&2; exit 1; }
[[ -f $model ]] || { echo "ÉCHEC : $model absent (tools/fetch-assets.sh)" >&2; exit 1; }
mkdir -p "$out"

for tonemapper in clip aces agx; do
    for exposure in 1 2; do
        capture="$out/$tonemapper-x$exposure.png"
        SDL_VIDEO_DRIVER=${SDL_VIDEO_DRIVER:-offscreen} "$sandbox" --seconds 1 --time 2 \
            --model "$model" --tonemap "$tonemapper" --exposure "$exposure" --capture "$capture" "$@" \
            > "$out/$tonemapper-x$exposure.log" 2>&1
        # Une capture absente ou vide est un échec, pas une case vide de la comparaison (règle 7).
        [[ -s $capture ]] || { echo "ÉCHEC : $capture" >&2; cat "$out/$tonemapper-x$exposure.log" >&2; exit 1; }
        echo "$capture"
    done
done
