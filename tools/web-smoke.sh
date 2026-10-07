#!/usr/bin/env bash
# Le test de fumée du backend WebGPU dans un vrai navigateur (ADR-0023, #184, #186) : sert les pages
# construites par le preset web, et les ouvre dans un Firefox headless au profil jetable (WebGPU
# activé). Le cube doit être celui de tests/data/cube.ppm, rendue par Vulkan ; le sandbox doit
# tourner (renard compris).
#   tools/web-smoke.sh [dossier de build]      (défaut : build/web)
# Firefox n'active pas WebGPU sous Linux par défaut : le profil jetable le fait, sans toucher au
# profil de l'utilisateur.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=$(realpath "${1:-$root/build/web}")
for page in tests/levain_web_cube.html sandbox/levain_sandbox.html; do
    [[ -f $build/$page ]] || { echo "ÉCHEC : $build/$page absent (preset web)" >&2; exit 1; }
done
command -v firefox >/dev/null || { echo "ÉCHEC : firefox introuvable" >&2; exit 1; }

work=$(mktemp -d)
httpPort=8765
bidiPort=9222
# Les PID viennent du lancement : pkill -f se reconnaîtrait dans la ligne de commande de ce script
# (build/GOTCHA.md).
cleanup() {
    [[ -f $work/firefox.pid ]] && kill "$(cat "$work/firefox.pid")" 2>/dev/null || true
    [[ -f $work/http.pid ]] && kill "$(cat "$work/http.pid")" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

mkdir "$work/profile"
cat > "$work/profile/user.js" <<'PREFS'
user_pref("dom.webgpu.enabled", true);
user_pref("gfx.webgpu.ignore-blocklist", true);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
PREFS

python3 -m http.server "$httpPort" --bind 127.0.0.1 --directory "$build" \
    >"$work/http.log" 2>&1 & echo $! > "$work/http.pid"
firefox --headless --no-remote --profile "$work/profile" \
    --remote-debugging-port "$bidiPort" >"$work/firefox.log" 2>&1 & echo $! > "$work/firefox.pid"

# Firefox ouvre son port BiDi après quelques secondes.
for _ in $(seq 50); do
    grep -q "WebDriver BiDi listening" "$work/firefox.log" && break
    sleep 0.2
done
grep -q "WebDriver BiDi listening" "$work/firefox.log" \
    || { echo "ÉCHEC : Firefox n'a pas ouvert BiDi" >&2; cat "$work/firefox.log" >&2; exit 1; }

node "$root/tools/web-smoke.mjs" "http://127.0.0.1:$httpPort/tests/levain_web_cube.html" \
    "$root/tests/data/cube.ppm" "$build/web-smoke.png" "$bidiPort"
# Les panneaux de l'interface ouverts (ADR-0032) : la capture les montre, ils doivent dessiner, et
# le calque rendre compte de leur temps CPU.
node "$root/tools/web-smoke.mjs" \
    "http://127.0.0.1:$httpPort/sandbox/levain_sandbox.html?args=--view%20hike%20--ui%20on" \
    - "$build/web-sandbox.png" "$bidiPort" ui
