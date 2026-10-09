#!/usr/bin/env bash
# La vérification complète d'une branche avant de pousser, comme la CI : le format de tout l'arbre, les trois
# presets natifs compilés et testés, le build Windows (compilé seulement), le build web et ses tests, puis
# clang-tidy sur les fichiers changés, avec les options du build Linux, puis avec celles du build Windows pour tous
# ceux qu'il compile.
# Dans la distrobox dev-ubuntu, depuis la racine du dépôt (ou d'un worktree) :
#
#   tools/verify.sh                  # BASE=origin/main : les fichiers changés depuis main, pour clang-tidy
#   BASE=<branche> tools/verify.sh   # une PR empilée : relire contre la précédente
#   NO_WEB=1 tools/verify.sh         # sans le build web (il n'y a ni physique ni app dans le navigateur)
#   NO_WINDOWS=1 tools/verify.sh     # sans le build Windows (pas de winsysroot : LEVAIN_WINSYSROOT, docs/SETUP.md),
#                                    # ni l'analyse avec ses options
#
# Une ligne par étape, OK ou FAIL ; les journaux complets dans build/verify/. Le code de sortie est non nul dès
# qu'une étape échoue (règle n°7) : un script qui rend 0 quoi qu'il arrive laisse pousser une branche rouge.
#
# Quatre tâches de compilation et quatre tests à la fois par défaut : la machine de référence a 16 Go, et des
# builds à toutes ses tâches l'ont fait planter (build/GOTCHA.md, « 16 Go de RAM »). Les ports de vcpkg suivent la
# même limite. Une machine plus grande la lève : CMAKE_BUILD_PARALLEL_LEVEL=16 LEVAIN_TEST_JOBS=8 tools/verify.sh.
#
# Les tests GPU tournent sur RADV, le pilote de la machine de référence, seul et préchargé : avec les huit pilotes
# de Mesa, LeakSanitizer voit une fuite dans ceux que le loader décharge (build/GOTCHA.md, « LeakSanitizer et
# lavapipe »). VK_DRIVER_FILES et LEVAIN_VK_PRELOAD changent de pilote, lavapipe pour refaire la CI.
set -u
cd "$(dirname "$0")/.." || exit 1

export CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-4}
# vcpkg ne lit pas CMAKE_BUILD_PARALLEL_LEVEL : un port reconstruit (le cache binaire raté) compilerait à toutes
# les tâches de la machine.
export VCPKG_MAX_CONCURRENCY=${VCPKG_MAX_CONCURRENCY:-$CMAKE_BUILD_PARALLEL_LEVEL}
testJobs=${LEVAIN_TEST_JOBS:-4}
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
    LD_PRELOAD=$preload ctest --test-dir "build/$preset" -j"$testJobs" --timeout 120 --output-on-failure \
        > "$logs/ctest-$preset.log" 2>&1
    summary=$(grep -E 'tests passed' "$logs/ctest-$preset.log")
    if grep -q ', 0 tests failed' <<< "$summary"; then
        step "$preset" OK "$summary"
    else
        step "$preset" FAIL "$summary"
    fi
done

# Windows, compilé depuis Linux par clang-cl (ADR-0035) : le build seulement. Les binaires Windows se lancent depuis
# la distro WSL (ctest) ou sur le runner Windows de la CI. Sans winsysroot (tools/winsysroot.sh le télécharge),
# l'étape ÉCHOUE en nommant la variable (règle n°7) : un contrôle qui se contenterait de ne pas s'exécuter
# laisserait passer une branche qui casse Windows. NO_WINDOWS=1 la saute, et le dit comme NO_WEB.
if [ -z "${NO_WINDOWS:-}" ]; then
    winsysroot=${LEVAIN_WINSYSROOT:-}
    if [ -z "$winsysroot" ]; then
        step windows-debug FAIL "LEVAIN_WINSYSROOT n'est pas posée (docs/SETUP.md) ; NO_WINDOWS=1 saute l'étape"
    elif [ ! -d "$winsysroot/VC/Tools/MSVC" ] || [ ! -d "$winsysroot/Windows Kits/10" ]; then
        step windows-debug FAIL "LEVAIN_WINSYSROOT=$winsysroot : il y manque VC/Tools/MSVC ou « Windows Kits/10 »"
    elif { cmake --preset windows-debug && cmake --build --preset windows-debug; } \
        > "$logs/build-windows-debug.log" 2>&1; then
        step windows-debug OK "build seulement"
    else
        step windows-debug FAIL "build, $logs/build-windows-debug.log"
    fi
else
    echo "windows-debug : SAUTÉ (NO_WINDOWS)"
fi

if [ -z "${NO_WEB:-}" ]; then
    source ~/emsdk/emsdk_env.sh > /dev/null 2>&1
    if { cmake --preset web && cmake --build --preset web; } > "$logs/build-web.log" 2>&1; then
        ctest --test-dir build/web -j"$testJobs" --timeout 120 --output-on-failure > "$logs/ctest-web.log" 2>&1
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

# clang-tidy sur les .cpp changés, avec les options de chaque build qui les compile. Linux d'abord ; puis, sauf
# NO_WINDOWS, tous ceux que le build Windows compile, sur sa base (build/windows-debug) : ceux qu'il compile seul
# que la base Linux ne connaît pas, et les fichiers communs, dont elle ne lit pas les blocs `#ifdef _WIN32`
# (device_vk.cpp…) ; sans cette passe, l'étape dirait OK sans les avoir lus (règle n°7). Un .cpp changé
# qu'aucune des deux ne compile fait échouer l'étape clang-tidy-orphelins, sauf ceux du build web seul
# (device_web.cpp…) : ils ne s'analysent pas sans les options d'Emscripten (build/GOTCHA.md), et le build web, juste
# au-dessus, les compile en -Werror. D'où l'étape après le build web, dont elle lit la base. Une base inconnue ou un
# build absent échouent : sans eux, la liste serait vide, et l'étape dirait « aucun fichier changé » sans avoir rien
# analysé.
base=${BASE:-origin/main}
compiledBy() { # $1 = preset : les fichiers que ce build compile, triés, dans $logs/tidy-compiled-$1.txt
    python3 -c 'import json, sys; print("\n".join(sorted({e["file"] for e in json.load(open(sys.argv[1]))})))' \
        "build/$1/compile_commands.json" > "$logs/tidy-compiled-$1.txt" 2>/dev/null
}
tidy() { # $1 = nom de l'étape, $2 = preset dont la base sert, $3 = les fichiers à analyser
    if [ ! -s "$3" ]; then
        step "$1" OK "aucun fichier changé"
    elif xargs clang-tidy -p "build/$2" --warnings-as-errors='*' < "$3" > "$logs/$1.log" 2>&1; then
        step "$1" OK "$(wc -l < "$3") fichiers"
    else
        step "$1" FAIL "$logs/$1.log"
    fi
}
if ! git rev-parse --verify -q "$base^{commit}" > /dev/null; then
    step clang-tidy FAIL "BASE inconnue : $base"
elif ! compiledBy linux-debug; then
    step clang-tidy FAIL "pas de build/linux-debug/compile_commands.json"
else
    # Les fichiers effacés n'ont rien à analyser (--diff-filter=d) ; les nouveaux, pas encore suivis, si. comm veut
    # deux listes triées dans le même ordre : celui des octets (LC_ALL=C), qui est celui de Python en UTF-8.
    { git diff --name-only --diff-filter=d "$base" -- '*.cpp'; git ls-files --others --exclude-standard -- '*.cpp'; } \
        | sed "s|^|$PWD/|" | LC_ALL=C sort -u > "$logs/tidy-changed.txt"
    LC_ALL=C comm -12 "$logs/tidy-changed.txt" "$logs/tidy-compiled-linux-debug.txt" > "$logs/tidy-files.txt"
    LC_ALL=C comm -23 "$logs/tidy-changed.txt" "$logs/tidy-compiled-linux-debug.txt" > "$logs/tidy-not-linux.txt"
    tidy clang-tidy linux-debug "$logs/tidy-files.txt"
    if [ -n "${NO_WINDOWS:-}" ]; then
        echo "clang-tidy-windows : SAUTÉ (NO_WINDOWS ; ni les $(wc -l < "$logs/tidy-not-linux.txt") fichiers changés" \
            "hors du build Linux, ni les blocs Windows des autres, non analysés)"
    elif ! compiledBy windows-debug; then
        step clang-tidy-windows FAIL "pas de build/windows-debug/compile_commands.json"
    else
        # Tous les .cpp changés que Windows compile, pas seulement ceux qu'il compile seul : un fichier commun n'est
        # lu par la base Linux que sans ses blocs `#ifdef _WIN32`.
        LC_ALL=C comm -12 "$logs/tidy-changed.txt" "$logs/tidy-compiled-windows-debug.txt" \
            > "$logs/tidy-files-windows.txt"
        tidy clang-tidy-windows windows-debug "$logs/tidy-files-windows.txt"
        compiledBy web || : > "$logs/tidy-compiled-web.txt"
        LC_ALL=C comm -23 "$logs/tidy-not-linux.txt" "$logs/tidy-compiled-windows-debug.txt" \
            > "$logs/tidy-not-native.txt"
        LC_ALL=C comm -12 "$logs/tidy-not-native.txt" "$logs/tidy-compiled-web.txt" > "$logs/tidy-web-only.txt"
        LC_ALL=C comm -23 "$logs/tidy-not-native.txt" "$logs/tidy-compiled-web.txt" | sed "s|^$PWD/||" \
            > "$logs/tidy-unknown.txt"
        if [ -s "$logs/tidy-web-only.txt" ]; then
            webCheck="compilés en -Werror par l'étape web"
            [ -z "${NO_WEB:-}" ] || webCheck="NO_WEB : pas même compilés"
            echo "clang-tidy-web : SAUTÉ ($(wc -l < "$logs/tidy-web-only.txt") fichiers du seul build web, $webCheck)"
        fi
        # Une étape à elle : sous le nom de clang-tidy-windows, un « OK (aucun fichier changé) » (rien de ce que
        # Windows compile n'a changé) et un FAIL (un .cpp que personne ne compile) se suivaient pour la même étape, et
        # qui ne lisait que la première ligne croyait la passe verte.
        if [ -s "$logs/tidy-unknown.txt" ]; then
            step clang-tidy-orphelins FAIL \
                "changés, compilés par aucun build : $(paste -sd ' ' "$logs/tidy-unknown.txt")"
        else
            step clang-tidy-orphelins OK "aucun .cpp changé que personne ne compile"
        fi
    fi
fi

exit $failed
