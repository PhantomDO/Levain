#!/usr/bin/env bash
# Le coût de l'annulation (ADR-0038, « Le coût de l'annulation », morceau A5) : supprimer puis annuler un sous-arbre
# de 1 000 entités (lire l'enregistrement, supprimer en gardant les index, rendre les pierres et l'enregistrement),
# et 150 changements de valeur suivis de 100 annulations. Le script compile le banc (tests/undo_bench.cpp) en Release,
# le lance RUNS fois, et imprime les millisecondes de chaque passage et leur médiane ; elles vont au journal si elles
# viennent de la machine de référence seulement (SPECS §10, règle n°6) : ailleurs, ce sont des ordres de grandeur.
#
# Usage : ./tools/undo-cost.sh
#   PRESET=linux-release (défaut), RUNS=5 (défaut). Le banc n'ouvre ni fenêtre ni device (la pile, le monde et la
#   scène suffisent) : « hors écran » est donc vrai par construction, `SDL_VIDEO_DRIVER=offscreen` ne sert que si
#   l'exécutable venait à en ouvrir une. La dernière ligne dit le processeur, pour que le chiffre porte sa provenance.
# Échoue bruyamment (règle n°7) : un passage sans ligne `undo.cost`, à un autre nombre d'entités que les 1 000 du
# critère, ou dont le banc sort en erreur (une annulation qui ne rend pas l'état) ne rend aucun chiffre.
set -euo pipefail

cd "$(dirname "$0")/.."
readonly preset="${PRESET:-linux-release}"
readonly runs="${RUNS:-5}"
[ $# -eq 0 ] || { echo "usage : $0 (PRESET et RUNS par l'environnement)" >&2; exit 2; }
[[ "$runs" =~ ^[1-9][0-9]*$ ]] || { echo "RUNS doit être un entier positif : $runs" >&2; exit 2; }

cmake --preset "$preset" > /dev/null
cmake --build --preset "$preset" --target levain_undo_bench > /dev/null
readonly bench="build/$preset/tests/levain_undo_bench"

linePattern="undo\.cost subtree=[0-9]+ delete_ms=[0-9.]+ undo_ms=[0-9.]+ values_ms=[0-9.]+"
readonly subtreeSize=1000
deletes=()
undos=()
values=()
for ((run = 1; run <= runs; run++)); do
    out=$(SDL_VIDEO_DRIVER=offscreen "$bench" 2>&1) \
        || { echo "passage $run : le banc a échoué : $out" >&2; exit 1; }
    line=$(grep -Eo "$linePattern" <<<"$out") \
        || { echo "passage $run : pas de ligne undo.cost : $out" >&2; exit 1; }
    [ "$(wc -l <<<"$line")" -eq 1 ] || { echo "passage $run : plusieurs lignes undo.cost : $line" >&2; exit 1; }
    size=$(sed -E 's/.* subtree=([0-9]+) .*/\1/' <<<"$line")
    [ "$size" -eq "$subtreeSize" ] \
        || { echo "passage $run : le sous-arbre compte $size entités, $subtreeSize attendues : $line" >&2; exit 1; }
    deletes+=("$(sed -E 's/.* delete_ms=([0-9.]+) .*/\1/' <<<"$line")")
    undos+=("$(sed -E 's/.* undo_ms=([0-9.]+) .*/\1/' <<<"$line")")
    values+=("$(sed -E 's/.* values_ms=([0-9.]+)$/\1/' <<<"$line")")
    echo "passage $run : $line"
done

median() {
    printf '%s\n' "$@" | sort -n |
        awk '{ v[NR] = $1 } END { print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2 }'
}

cpu=$(grep -m1 "model name" /proc/cpuinfo | sed -E 's/^[^:]*: //') || cpu="processeur non donné"
echo "Annulation : supprimer $(median "${deletes[@]}") ms, annuler $(median "${undos[@]}") ms (sous-arbre de" \
    "$subtreeSize entités) ; 150 changements et 100 annulations $(median "${values[@]}") ms ;" \
    "médianes de $runs passages ; preset $preset, $cpu"
