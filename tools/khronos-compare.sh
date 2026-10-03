#!/usr/bin/env bash
# La comparaison avec la référence Khronos (#125, #131) : MetalRoughSpheres rendu par le glTF Sample
# Viewer hébergé, dans un Firefox headless au profil jetable, puis par le sandbox sous la même
# caméra (relue dans la page), le même ciel (Cannon_Exterior, tourné comme lui) et la même courbe
# (Khronos PBR Neutral). L'écart se mesure sphère par sphère, en ΔE76.
#   tools/khronos-compare.sh <dossier de sortie> [ibl|direct] [dossier de build]
# `ibl` (par défaut, #131) : le ciel seul. `direct` (#125) : l'IBL coupée dans le viewer, sa lumière
# principale relue dans la page et reprise comme soleil, sans ciel. Build : build/linux-release.
# Écrit viewer.png, levain.png et ecart.txt dans le dossier de sortie.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=$(realpath "${1:?usage : tools/khronos-compare.sh <dossier de sortie> [ibl|direct] [dossier de build]}")
mode=${2:-ibl}
[[ $mode == ibl || $mode == direct ]] || { echo "ÉCHEC : mode $mode, ibl ou direct attendu" >&2; exit 1; }
build=$(realpath "${3:-$root/build/linux-release}")
sandbox=$build/sandbox/levain_sandbox
model=$root/assets-cache/Models/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf
sky=$root/assets-cache/HDRIs/Cannon_Exterior.hdr
for file in "$sandbox" "$model" "$sky"; do
    [[ -f $file ]] || { echo "ÉCHEC : $file absent (build, ou tools/fetch-assets.sh)" >&2; exit 1; }
done
command -v firefox >/dev/null || { echo "ÉCHEC : firefox introuvable" >&2; exit 1; }
mkdir -p "$out"

# Le modèle au commit de tools/assets.lock : le même que celui du sandbox.
commit=$(awk '$1 == "commit" { print $2 }' "$root/tools/assets.lock")
url="https://github.khronos.org/glTF-Sample-Viewer-Release/?noUI&model=https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/$commit/Models/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf"

work=$(mktemp -d)
bidiPort=9226
cleanup() {
    [[ -f $work/firefox.pid ]] && kill "$(cat "$work/firefox.pid")" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
mkdir "$work/profile"
cat > "$work/profile/user.js" <<'PREFS'
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
PREFS
firefox --headless --no-remote --profile "$work/profile" \
    --remote-debugging-port "$bidiPort" >"$work/firefox.log" 2>&1 & echo $! > "$work/firefox.pid"
for _ in $(seq 50); do
    grep -q "WebDriver BiDi listening" "$work/firefox.log" && break
    sleep 0.2
done
grep -q "WebDriver BiDi listening" "$work/firefox.log" \
    || { echo "ÉCHEC : Firefox n'a pas ouvert BiDi" >&2; cat "$work/firefox.log" >&2; exit 1; }

node "$root/tools/khronos-compare.mjs" capture "$mode" "$url" "$out/viewer.png" "$bidiPort" \
    >"$work/viewer.txt"
camera=$(sed -n 1p "$work/viewer.txt")
echo "caméra du viewer : $camera"
lighting=(--sky "$sky")
if [[ $mode == direct ]]; then
    sun=$(sed -n 2p "$work/viewer.txt")
    echo "lumière principale du viewer, venant de : $sun"
    lighting=(--sky none --sun "$sun")
fi

# La fenêtre du sandbox fait 1920 × 1080, comme la page.
SDL_VIDEO_DRIVER=offscreen "$sandbox" --seconds 1 --time 1 --view khronos --tonemap neutral \
    --camera "$camera" --model "$model" "${lighting[@]}" --capture "$out/levain.png" \
    >"$work/sandbox.log"
grep -q "capture : " "$work/sandbox.log" \
    || { echo "ÉCHEC : le sandbox n'a pas capturé" >&2; cat "$work/sandbox.log" >&2; exit 1; }

node "$root/tools/khronos-compare.mjs" measure "$out/viewer.png" "$out/levain.png" "$camera" \
    | tee "$out/ecart.txt"
