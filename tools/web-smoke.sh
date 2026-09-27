#!/usr/bin/env bash
# Le test de fumée du backend WebGPU dans un vrai navigateur (ADR-0023, #184) : sert la page du cube
# construite par le preset web, l'ouvre dans un Firefox headless au profil jetable (WebGPU activé),
# et compare la capture à tests/data/cube.ppm, rendue par Vulkan.
#   tools/web-smoke.sh [dossier de build]      (défaut : build/web)
# Firefox n'active pas WebGPU sous Linux par défaut : le profil jetable le fait, sans toucher au
# profil de l'utilisateur.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=$(realpath "${1:-$root/build/web}")
page="$build/tests/levain_web_cube.html"
[[ -f $page ]] || { echo "ÉCHEC : $page absent, compiler levain_web_cube (preset web)" >&2; exit 1; }
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

python3 -m http.server "$httpPort" --bind 127.0.0.1 --directory "$build/tests" \
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

node "$root/tools/web-smoke.mjs" "http://127.0.0.1:$httpPort/levain_web_cube.html" \
    "$root/tests/data/cube.ppm" "$build/web-smoke.png" "$bidiPort"
