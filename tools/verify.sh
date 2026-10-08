#!/usr/bin/env bash
# La vérification complète d'une branche avant de pousser, comme la CI : le format de tout l'arbre, les trois
# presets natifs compilés et testés, clang-tidy sur les fichiers changés, puis le build web et ses tests.
# Dans la distrobox dev-ubuntu, depuis la racine du dépôt (ou d'un worktree) :
#
#   tools/verify.sh                  # BASE=origin/main : les fichiers changés depuis main, pour clang-tidy
#   BASE=<branche> tools/verify.sh   # une PR empilée : relire contre la précédente
#   NO_WEB=1 tools/verify.sh         # sans le build web (il n'y a ni physique ni app dans le navigateur)
#
# Une ligne par étape, OK ou FAIL ; les journaux complets dans build/verify/. Le code de sortie est non nul dès
# qu'une étape échoue (règle n°7) : un script qui rend 0 quoi qu'il arrive laisse pousser une branche rouge.
#
# Les tests GPU tournent sur RADV, le pilote de la machine de référence, seul et préchargé : avec les huit pilotes
# de Mesa, LeakSanitizer voit une fuite dans ceux que le loader décharge (build/GOTCHA.md, « LeakSanitizer et
# lavapipe »). VK_DRIVER_FILES et LEVAIN_VK_PRELOAD changent de pilote, lavapipe pour refaire la CI.
set -u
cd "$(dirname "$0")/.." || exit 1

logs=build/verify
mkdir -p "$logs"
failed=0
step() { # $1 = nom de l'étape, $2 = OK ou FAIL, $3 = détail
    echo "$1 : $2${3:+ ($3)}"
    [ "$2" = OK ] || failed=1
}

if find editor engine plugins sandbox tests tools -name '*.cpp' -o -name '*.hpp' \
    | xargs clang-format --dry-run --Werror > "$logs/format.log" 2>&1; then
    step format OK
else
    step format FAIL "$logs/format.log"
fi

export VK_DRIVER_FILES=${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/radeon_icd.json}
preload=${LEVAIN_VK_PRELOAD:-/usr/lib/x86_64-linux-gnu/libvulkan_radeon.so}
for preset in ${PRESETS:-linux-debug linux-release linux-asan}; do
    if ! { cmake --preset "$preset" && cmake --build --preset "$preset"; } > "$logs/build-$preset.log" 2>&1; then
        step "$preset" FAIL "build, $logs/build-$preset.log"
        continue
    fi
    # Comme la CI : un test bloqué échoue en 2 min, et son journal dit pourquoi.
    LD_PRELOAD=$preload ctest --test-dir "build/$preset" -j8 --timeout 120 --output-on-failure \
        > "$logs/ctest-$preset.log" 2>&1
    summary=$(grep -E 'tests passed' "$logs/ctest-$preset.log")
    if grep -q ', 0 tests failed' <<< "$summary"; then
        step "$preset" OK "$summary"
    else
        step "$preset" FAIL "$summary"
    fi
done

# clang-tidy sur les seuls .cpp changés que le build natif compile : ceux du navigateur ne s'analysent pas sans
# leurs options (build/GOTCHA.md). Une base inconnue ou un build Debug absent échouent : sans eux, la liste serait
# vide, et l'étape dirait « aucun fichier changé » sans avoir rien analysé.
base=${BASE:-origin/main}
tidy=listed
if ! git rev-parse --verify -q "$base^{commit}" > /dev/null; then
    step clang-tidy FAIL "BASE inconnue : $base"
    tidy=refused
elif ! python3 -c 'import json; print("\n".join(sorted({e["file"] for e in json.load(open(
        "build/linux-debug/compile_commands.json"))})))' > "$logs/tidy-compiled.txt" 2>&1; then
    step clang-tidy FAIL "pas de build/linux-debug/compile_commands.json"
    tidy=refused
else
    { git diff --name-only "$base" -- '*.cpp'; git ls-files --others --exclude-standard -- '*.cpp'; } \
        | sed "s|^|$PWD/|" | grep -F -x -f - "$logs/tidy-compiled.txt" > "$logs/tidy-files.txt" || true
fi
if [ "$tidy" = refused ]; then
    : # l'étape a déjà dit pourquoi
elif [ ! -s "$logs/tidy-files.txt" ]; then
    step clang-tidy OK "aucun fichier changé"
elif xargs clang-tidy -p build/linux-debug --warnings-as-errors='*' < "$logs/tidy-files.txt" \
    > "$logs/tidy.log" 2>&1; then
    step clang-tidy OK "$(wc -l < "$logs/tidy-files.txt") fichiers"
else
    step clang-tidy FAIL "$logs/tidy.log"
fi

if [ -z "${NO_WEB:-}" ]; then
    source ~/emsdk/emsdk_env.sh > /dev/null 2>&1
    if { cmake --preset web && cmake --build --preset web; } > "$logs/build-web.log" 2>&1; then
        ctest --test-dir build/web -j8 --timeout 120 --output-on-failure > "$logs/ctest-web.log" 2>&1
        summary=$(grep -E 'tests passed' "$logs/ctest-web.log")
        if grep -q ', 0 tests failed' <<< "$summary"; then
            step web OK "$summary"
        else
            step web FAIL "$summary"
        fi
    else
        step web FAIL "build, $logs/build-web.log"
    fi
else
    echo "web : SAUTÉ (NO_WEB)"
fi

exit $failed
