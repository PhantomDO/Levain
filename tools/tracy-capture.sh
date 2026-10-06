#!/usr/bin/env bash
# Capture Tracy du sandbox, sans interface : lance le sandbox profilé, enregistre quelques
# secondes avec tracy-capture, puis résume les zones et les courbes (tools/tracy-summary.py).
#
# Prérequis : le build profilé (skill build, « Profilage Tracy ») et les outils Tracy de la même
# version que le client, 0.14.1 (ports/tracy) : github.com/wolfpld/tracy/releases/tag/v0.14.1
#
# Usage : ./tools/tracy-capture.sh [secondes] [fichier.tracy] [arguments du sandbox…]
#   ./tools/tracy-capture.sh 5 captures/hike.tracy --view hike --walk 1,0
# La capture s'ouvre ensuite dans le profileur : tracy-profiler-x86_64.AppImage <fichier.tracy>
set -euo pipefail

readonly seconds="${1:-3}"
readonly output="${2:-captures/sandbox.tracy}"
readonly tracy_dir="${TRACY_DIR:-$HOME/.local/opt/tracy-0.14.1}"
readonly sandbox=build/prof/sandbox/levain_sandbox

mkdir -p "$(dirname "$output")"

# TRACY_NO_EXIT : sans lui, le sandbox pourrait se terminer avant la connexion, sans un mot.
TRACY_NO_EXIT=1 "$sandbox" "${@:3}" > /dev/null 2>&1 &
readonly sandbox_pid=$!

stop_sandbox() {
    kill -TERM "$sandbox_pid" 2>/dev/null || return 0
    for _ in 1 2 3 4 5; do
        kill -0 "$sandbox_pid" 2>/dev/null || return 0
        sleep 1
    done
    # TRACY_NO_EXIT fait attendre un profileur qui ne reviendra pas : on force.
    kill -KILL "$sandbox_pid" 2>/dev/null || true
}
trap stop_sandbox EXIT

"$tracy_dir/tracy-capture" -a 127.0.0.1 -o "$output" -f -s "$seconds"
TRACY_DIR="$tracy_dir" "$(dirname "$0")/tracy-summary.py" "$output"
