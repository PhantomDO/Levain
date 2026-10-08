#!/usr/bin/env bash
# Redimensionne, réduit et restaure la fenêtre du sandbox Windows lancé de la distro WSL, comme un utilisateur :
# le pendant de tools/kwin-window-smoke.sh pour un exe Windows (ADR-0035), par le pilote resize-sandbox.ps1, à côté.
#
# Usage, de la distro, depuis la racine du dépôt (assez de secondes pour la séquence, qui en prend une douzaine
# après l'ouverture de la fenêtre) :
#   tools/wsl/resize-sandbox.sh build/windows-debug/sandbox/levain_sandbox.exe --gpu d3d12 --seconds 20
#
# Le sandbox tourne avec les arguments donnés, son journal dans build/resize-sandbox.log (RESIZE_LOG pour un
# autre). Le pilote attend sa fenêtre, la passe en 1280 × 720 puis 1600 × 900, la réduit, la restaure, la passe en
# 1920 × 1080. Le script échoue bruyamment (règle n°7) si le pilote échoue, si le sandbox sort en erreur (en Debug,
# une erreur des couches de debug est une assertion : code 3), si le journal n'a pas vu les trois redimensionnements,
# la fenêtre masquée puis visible, ou s'il porte un message de Direct3D 12, de DXGI ou de NVRHI.
set -u

if [ $# -lt 1 ]; then
    echo "usage : tools/wsl/resize-sandbox.sh <levain_sandbox.exe> [options du sandbox…]" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
log=${RESIZE_LOG:-$(cd "$here/../.." && pwd)/build/resize-sandbox.log}
mkdir -p "$(dirname "$log")"
exe=$1
shift

"$exe" "$@" > "$log" 2>&1 &
sandbox=$!
# -EncodedCommand : le script en UTF-16LE, en base64 ; « -Command - < resize-sandbox.ps1 » ne ferait rien, sans un
# message (build/GOTCHA.md). powershell.exe termine ses lignes par CRLF.
powershell.exe -NoProfile -NonInteractive -EncodedCommand \
    "$(iconv -f utf-8 -t utf-16le "$here/resize-sandbox.ps1" | base64 -w0)" | tr -d '\r'
driverCode=${PIPESTATUS[0]}
# Même quand le pilote a échoué : le sandbox tourne jusqu'à --seconds, et son code compte aussi.
wait "$sandbox"
sandboxCode=$?

failed=0
fail() {
    echo "resize-sandbox : FAIL ($1)"
    failed=1
}
[ "$driverCode" -eq 0 ] || fail "pilote, code $driverCode"
[ "$sandboxCode" -eq 0 ] || fail "sandbox, code $sandboxCode, $log"
# Au moins trois : la fenêtre peut aussi changer de taille à son ouverture.
resized=$(grep -c 'redimensionnée :' "$log")
[ "$resized" -ge 3 ] || fail "$resized redimensionnements dans le journal, 3 au moins attendus"
grep -q 'masquée :' "$log" || fail "la fenêtre n'a pas été vue masquée"
grep -q 'visible :' "$log" || fail "la fenêtre n'a pas été vue de nouveau visible"
layers=$(grep -cE '\] \[(d3d12|dxgi|nvrhi)\] ' "$log")
[ "$layers" -eq 0 ] || fail "$layers messages de Direct3D 12, DXGI ou NVRHI dans $log"
if [ "$failed" -eq 0 ]; then
    echo "resize-sandbox : OK ($resized redimensionnements, masquée puis visible, sans message des couches ; $log)"
fi
exit "$failed"
