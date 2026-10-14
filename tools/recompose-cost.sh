#!/usr/bin/env bash
# Le coût de la recomposition après `ui` (ADR-0036, morceau 4) : une image de la boucle sur les
# 10 000 cubes de la démo, sans puis avec `App::recomposeAfterUi`. Compile le banc en Release, le
# lance hors écran, et imprime ses chiffres ; ils vont au journal s'ils viennent de la machine de
# référence seulement (SPECS §10, règle n°6).
#
# Usage : ./tools/recompose-cost.sh [vulkan|d3d12|d3d12-warp]
#   PRESET=linux-release (défaut) ; le GPU de la boucle est lavapipe hors écran, sans fenêtre.
set -euo pipefail

cd "$(dirname "$0")/.."
readonly preset="${PRESET:-linux-release}"

cmake --preset "$preset" > /dev/null
cmake --build --preset "$preset" --target levain_recompose_bench > /dev/null
# Le pilote offscreen : aucune fenêtre ne s'ouvre sur le bureau de la machine.
SDL_VIDEO_DRIVER=offscreen "build/$preset/tests/levain_recompose_bench" "${@:-vulkan}"
