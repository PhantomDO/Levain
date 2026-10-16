#!/usr/bin/env bash
# Le coût de Stop sur les 10 000 cubes de la démo (ADR-0037, morceau 7) : l'éditeur par défaut (sans `--view`) joue
# 40 pas (Alt+P puis Échap, par tests/data/editor/play_stop_cost.txt), et la ligne
# `editor.stop steps=S differences=D ms=T` donne le temps que Stop met à rendre la scène. Le script compile l'éditeur en
# Release, le lance hors écran RUNS fois, et imprime les millisecondes de chaque passage et leur médiane ; elles vont au
# journal si elles viennent de la machine de référence seulement (SPECS §10, règle n°6) : sous lavapipe ou sur le
# portable, ce sont des ordres de grandeur.
#
# Usage : ./tools/play-stop-cost.sh
#   PRESET=linux-release (défaut), RUNS=5 (défaut). Le GPU n'entre pas dans le coût de Stop (le processeur seul rend la
#   scène), mais la ligne finale dit le processeur et le GPU pour que le chiffre porte sa provenance.
# Échoue bruyamment (règle n°7) : un passage sans ligne `editor.stop`, à un autre nombre de pas que les 40 du script ou
# avec une différence ne rend aucun chiffre, pas plus qu'une démo qui n'aurait plus ses 10 000 cubes. L'éditeur ne dit
# pas le nombre d'entités du monde (sa ligne « éditeur : N entités » compte les lignes de la hiérarchie, 2 ici : la
# grille est repliée) : les cubes se comptent aux triangles que la caméra dessine, douze par cube.
set -euo pipefail

cd "$(dirname "$0")/.."
readonly preset="${PRESET:-linux-release}"
readonly runs="${RUNS:-5}"
[ $# -eq 0 ] || { echo "usage : $0 (PRESET et RUNS par l'environnement)" >&2; exit 2; }
[[ "$runs" =~ ^[1-9][0-9]*$ ]] || { echo "RUNS doit être un entier positif : $runs" >&2; exit 2; }

cmake --preset "$preset" > /dev/null
cmake --build --preset "$preset" --target levain_sandbox_editor > /dev/null
readonly editor="build/$preset/sandbox/levain_sandbox_editor"

log=$(mktemp)
trap 'rm -f "$log"' EXIT
stopPattern="editor\.stop steps=[0-9]+ differences=[0-9]+ ms=[0-9.]+"
readonly trianglesPerCube=12 # 6 faces de 2 triangles
readonly cubes=10000
readonly playedSteps=40 # Alt+P à l'image 2, Échap à l'image 43 : tests/data/editor/play_stop_cost.txt
times=()
for ((run = 1; run <= runs; run++)); do
    # Le pilote offscreen : aucune fenêtre ne s'ouvre sur le bureau de la machine. `--steps 50` : un pas par image,
    # quelle que soit la vitesse de la machine, donc 40 pas de jeu à chaque passage.
    SDL_VIDEO_DRIVER=offscreen "$editor" --steps 50 \
        --input-script tests/data/editor/play_stop_cost.txt > "$log" 2>&1 \
        || { echo "passage $run : l'éditeur a échoué" >&2; cat "$log" >&2; exit 1; }
    stop=$(grep -Eo "$stopPattern" "$log") \
        || { echo "passage $run : pas de ligne editor.stop" >&2; cat "$log" >&2; exit 1; }
    [ "$(wc -l <<<"$stop")" -eq 1 ] || { echo "passage $run : plusieurs lignes editor.stop : $stop" >&2; exit 1; }
    steps=$(sed -E 's/.* steps=([0-9]+) .*/\1/' <<<"$stop")
    differences=$(sed -E 's/.* differences=([0-9]+) .*/\1/' <<<"$stop")
    [ "$steps" -eq "$playedSteps" ] \
        || { echo "passage $run : Play a joué $steps pas, $playedSteps attendus : $stop" >&2; exit 1; }
    [ "$differences" -eq 0 ] || { echo "passage $run : Stop n'a pas tout rendu : $stop" >&2; exit 1; }
    triangles=$(sed -nE 's/.*caméra [0-9.]+ appels et ([0-9]+) triangles.*/\1/p' "$log")
    [ -n "$triangles" ] || { echo "passage $run : pas de ligne « dessins, par image » dans le journal" >&2; exit 1; }
    [ "$triangles" -ge $((cubes * trianglesPerCube)) ] \
        || { echo "passage $run : la caméra ne dessine que $triangles triangles, pas $cubes cubes" >&2; exit 1; }
    times+=("$(sed -E 's/.* ms=([0-9.]+)$/\1/' <<<"$stop")")
    echo "passage $run : $stop"
done

# Ce qui donne au chiffre sa provenance : le processeur (Stop ne travaille que lui) et le GPU, tel que le moteur le dit
# au démarrage (la ligne `[gpu]` du journal).
cpu=$(grep -m1 "model name" /proc/cpuinfo | sed -E 's/^[^:]*: //') || cpu="processeur non donné"
device=$(grep -m1 "\[gpu\]" "$log" | sed -E 's/^[^]]*\] \[info\] \[gpu\] //') || device="GPU non donné"
median=$(printf '%s\n' "${times[@]}" | sort -n |
    awk '{ v[NR] = $1 } END { print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2 }')
echo "Stop sur la démo : médiane ${median} ms sur ${runs} passages (${steps} pas de jeu," \
    "${triangles} triangles à l'image) ; preset ${preset}, ${cpu}, ${device}"
