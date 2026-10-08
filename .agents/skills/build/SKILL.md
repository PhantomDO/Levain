---
name: build
description: Compiler, tester et vérifier Levain — presets CMake et vcpkg, tests doctest, format, clang-tidy, sanitizers, profilage Tracy, CI, et tests manuels de la fenêtre sous KWin. À utiliser pour tout build, toute mesure et toute modification de la CI.
---

# Build, tests et CI

Lire d'abord [`GOTCHA.md`](GOTCHA.md).

## Prérequis, une seule fois

vcpkg cloné et bootstrappé, puis `VCPKG_ROOT` exporté : les presets lisent cette variable, et le
`CMakeLists.txt` racine refuse de se configurer sans la toolchain vcpkg. `python3` aussi, pour le build natif :
le test `scene.field-ladder` relance `tools/generate_field_ladder.py`.

```bash
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
set -Ux VCPKG_ROOT ~/vcpkg   # fish ; bash : echo 'export VCPKG_ROOT=~/vcpkg' >> ~/.bashrc
```

## Compiler et tester

| Preset | Contenu |
|---|---|
| `linux-debug` | Debug, assertions actives |
| `linux-release` | RelWithDebInfo |
| `linux-asan` | Debug + AddressSanitizer, LeakSanitizer, UBSan |
| `windows-debug`, `windows-release` | Windows par clang-cl, compilé depuis Linux (ADR-0035) : `LEVAIN_WINSYSROOT` désigne la STL et le SDK de Microsoft (docs/SETUP.md). Les `.exe` se lancent depuis une distro WSL (`tools/wsl/README.md`) |
| `web` | WebAssembly par Emscripten (ADR-0023) : le moteur sans fenêtre (`core`, `scene`, `assets`, `animation`, `gpu` sur WebGPU, `render`), ses tests lancés par Node, et la page du cube pour le navigateur |

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=4 VCPKG_MAX_CONCURRENCY=4   # la machine de référence a 16 Go (GOTCHA)
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```

Windows (`windows-debug`, `windows-release`), depuis la distro WSL : `LEVAIN_WINSYSROOT` posée, puis
`ctest --test-dir build/windows-debug -j 4 --timeout 120 --output-on-failure`. Les contrôles qui lancent un outil de
l'hôte (`cmake -P`, Python, la lecture des symboles dans le PDB par `llvm-pdbutil`, `cmake.plugins.*`, `dxc` pour
les `dxil.*`) portent le label `host` (`levain_add_host_test`, `tests/CMakeLists.txt`) : `ctest -L host` les lance seuls, `ctest -LE host`
lance les programmes de la cible, ce que fera un runner Windows (#346).

Le premier `cmake --preset` est long : vcpkg compile les dépendances depuis les sources. Les suivants sont
instantanés (cache `~/.cache/vcpkg`). Pour clangd : `ln -sf build/linux-debug/compile_commands.json .`

Les contrôles de la CI sur les sources et les tests, d'un coup, avant chaque push : `tools/verify.sh` (format,
trois presets, `windows-debug` compilé sans être lancé, clang-tidy des fichiers changés, web ; code de sortie non
nul dès qu'une étape échoue). Sans `LEVAIN_WINSYSROOT` (la machine de référence, jusqu'au winsysroot de xwin de
#346), l'étape Windows échoue en nommant la variable ; `NO_WINDOWS=1` la saute. Il ne lance pas le sandbox comme la CI (Fox, Sponza, terrain, hot-reload). Le détail, étape par étape :

Format et analyse statique, comme la CI :

```bash
find editor engine plugins sandbox tests tools -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror
# les fichiers du build natif seulement : ceux du navigateur ne s'analysent pas sans leurs options
jq -r '.[].file' build/linux-debug/compile_commands.json | grep -E "^$PWD/(editor|engine|plugins|sandbox|tests|tools)/" \
  | sort -u | xargs clang-tidy -p build/linux-debug --warnings-as-errors='*'
```

Le build web demande Emscripten (emsdk dans `~/emsdk`, version figée par `EMSDK_VERSION` dans la CI) :

```bash
source ~/emsdk/emsdk_env.sh
cmake --preset web && cmake --build --preset web
ctest --test-dir build/web --output-on-failure
```

Le triplet `triplets/wasm32-emscripten.cmake` reprend celui de la communauté vcpkg, avec une parade pour ktx
(GOTCHA). Le triplet natif `triplets/x64-linux.cmake` reprend celui de vcpkg et écrit `-march=x86-64` pour
tous les ports (ADR-0033). Les tests lisent le disque par `-sNODERAWFS` : les chemins de `tests/data` restent
ceux de la machine.

Le backend WebGPU dans un vrai navigateur : `tools/web-smoke.sh` sert `build/web`, ouvre les pages dans un
Firefox headless au profil jetable (WebGPU activé, le profil de Donnovan n'est pas touché), compare le cube à
`tests/data/cube.ppm`, rendue par Vulkan, et vérifie que le sandbox tourne. Il faut un GPU : pas en CI pour
l'instant. Les captures restent dans `build/web/web-smoke.png` et `build/web/web-sandbox.png`.

Le sandbox web (`build/web/sandbox/levain_sandbox.html`) précharge `data/`, les shaders WGSL, le renard, le
camion et les textures du terrain (jamais Sponza, licence) : `tools/fetch-assets.sh` d'abord. Ses arguments
passent par l'URL, `?args=--model%20/assets-cache/Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf` ; par
défaut, `--view hike`, le renard qu'on dirige dans la vallée. Un calque en haut à droite (`sandbox/web/stats.js`,
#294) affiche chaque seconde les images/s, le temps passé dans le moteur et sa part de l'image, la résolution, et
la machine (GPU, navigateur, cœurs, mémoire) ; `tools/web-smoke.sh` vérifie qu'il se remplit. Pour
le voir soi-même : `python3 -m http.server -d build/web/sandbox`, puis `http://localhost:8000/levain_sandbox.html`.

`-pedantic-errors` (C++23 strict) et `-Wall -Wextra -Werror` sont dans le `CMakeLists.txt` racine : **ne jamais
les retirer**. Un avertissement ou une extension C++26 doit casser le build. Sous clang-cl ils passent par
`/clang:` (il lit `-Wall` comme `-Weverything`), et Levain ne définit jamais `_CRT_SECURE_NO_WARNINGS` (seul ozz,
port tiers, l'a dans sa branche MSVC, pour ses propres sources).

## Shaders

Les shaders Slang de `shaders/` sont compilés au build par `slangc` (port vcpkg `shader-slang`) en SPIR-V, en
DXIL et en WGSL (le navigateur, ADR-0023), une commande par point d'entrée (`levain_add_shader` dans
`shaders/CMakeLists.txt`, ADR-0005). Un shader doit rester dans le sous-ensemble que la cible WGSL de Slang
sait traduire : pas de `ByteAddressBuffer` (des `StructuredBuffer<uint>`), sinon le build natif échoue déjà. Sorties
dans `build/<preset>/shaders/`, lues à l'exécution par `engine/render`. Chaque DXIL est désassemblé par un test
ctest (`dxil.*`, natif seulement), faute de backend Direct3D 12 pour l'exécuter.

## Test de fumée du rendu

`smoke.triangle` et `smoke.cube` (ctest, `tests/smoke_render.cpp`) dessinent leur scène hors écran, relisent
l'image et la comparent à `tests/data/<scène>.ppm`, à ±2 près par canal. Après un changement voulu du rendu,
réécrire la référence, puis la regarder avant de la commiter :

```bash
LEVAIN_UPDATE_REFERENCE=1 SDL_VIDEO_DRIVER=offscreen ./build/linux-debug/tests/levain_smoke_render cube
magick tests/data/cube.ppm -filter point -resize 400% /tmp/cube.png   # pour la voir
```

En échec, l'image obtenue est écrite dans le dossier courant, `<scène>.actual.ppm`.

## Lancer le sandbox

```bash
SDL_VIDEO_DRIVER=x11 ./build/linux-debug/sandbox/levain_sandbox   # x11 : voir GOTCHA.md, jusqu'en M1.2
```

Comme la CI, avec les sanitizers (code 0 = ni fuite ni comportement indéfini) :

```bash
SDL_VIDEO_DRIVER=offscreen ./build/linux-asan/sandbox/levain_sandbox --seconds 3
```

## Montrer un rendu à Donnovan

Donnovan suit souvent à distance, sur tablette, sans voir l'écran de la machine. Pour lui montrer un rendu,
**le moteur capture sa propre image** (jamais le bureau) :

```bash
SDL_VIDEO_DRIVER=offscreen ./build/linux-release/sandbox/levain_sandbox --seconds 2 --capture <scratchpad>/rendu.png
```

Pour charger les versions cuites (ADR-0020), cuire d'abord : `./build/linux-release/tools/cook/levain_cook
assets-cache` (Release : l'encodage UASTC y est 5 fois plus rapide qu'en Debug ; environ 40 s pour Sponza).

Avec un modèle glTF devant la caméra : `--model assets-cache/Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf`
(ou `Sponza/glTF/Sponza.gltf`, vue de l'intérieur),
après `./tools/fetch-assets.sh` (assets de test tiers, vérifiés par SHA-256, jamais versionnés).

Regarder l'image soi-même d'abord (outil de lecture d'images), puis l'envoyer avec `SendUserFile` : elle
s'affiche dans l'application Claude. Pour un avant/après, deux captures dans le même envoi. La capture est une
dernière image rendue après la boucle, relue par `render::readBack` et écrite par `assets::savePng`.

## Tester la fenêtre comme un utilisateur

Sandbox lancé sous X11, puis `./tools/kwin-window-smoke.sh` (Plasma uniquement) : KWin redimensionne, minimise et
restaure la vraie fenêtre, et le script mesure le CPU consommé. Lire le titre réellement affiché :

```bash
gdbus call --session --dest org.kde.KWin --object-path /WindowsRunner --method org.kde.krunner1.Match Levain
```

## Profilage Tracy

Désactivé par défaut. Le client est Tracy **0.14.1**, par le port overlay `ports/tracy` (ADR-0007, amendement).
Les outils de la même version (profileur, `tracy-capture`, `tracy-csvexport`) viennent de la release officielle,
décompressée dans `~/.local/opt/tracy-0.14.1/` (`TRACY_DIR` pour un autre dossier).

```bash
cmake --preset linux-release -B build/prof -DLEVAIN_PROFILING=ON   # Release : des temps représentatifs
cmake --build build/prof --target levain_sandbox
SDL_VIDEO_DRIVER=offscreen ./tools/tracy-capture.sh 5 captures/hike.tracy --view hike --walk 1,0
./tools/tracy-summary.py captures/hike.tracy             # médianes des zones et des courbes GPU
~/.local/opt/tracy-0.14.1/tracy-profiler-x86_64.AppImage captures/hike.tracy   # ouvrir la capture
```

Ce que la capture contient (#295) : les zones du sandbox, celles du moteur (pas fixe, `stepPhysics`,
`advanceCharacter`, `sampleBlend`, `animateModels`, le chargement), une zone par fonction d'étape du rendu (« ombres/terrain »), et
une courbe « GPU étape/nom » par fonction : le temps GPU de la fonction, toutes cascades comprises, par les
minuteurs de NVRHI. Ces minuteurs coûtent (engine/render/README.md, « la mesure qui coûte ») : le sandbox ne les
active qu'en build profilé, et le journal y ajoute la ligne « étapes, GPU en moyenne ». Pas de zones GPU natives de Tracy (`TracyVkZone`) : elles demanderaient les poignées Vulkan hors du
module `gpu` (choix de Donnovan, 06/10/2026).

Pour profiler en direct : lancer le profileur, puis `TRACY_NO_EXIT=1 ./build/prof/sandbox/levain_sandbox`.

**Analyser une capture par MCP (à mettre en place en M1.2)**. Tracy 0.14.1 fournit un serveur MCP
(`extra/mcp/tracy_mcp.py` dans ses sources, manuel § « MCP Server ») : un agent y charge une capture `.tracy`
et l'interroge par du Python (`eval` sur l'objet `Worker`), ou compare deux captures. C'est la voie à préférer
aux captures d'écran. Prérequis : les bindings Python de Tracy (`-DTRACY_CLIENT_PYTHON=ON`), `pip install mcp`,
puis déclarer `http://127.0.0.1:47380/mcp` auprès de l'agent.

## CI

`.github/workflows/ci.yml` : une matrice `linux-debug`, `linux-release`, `linux-asan`, chacun compilé, testé et
lancé 3 s ; format et clang-tidy sur `linux-debug`. Les trois sont des checks requis pour fusionner sur `main`.
`linux-release` compile aussi le build profilé (`-DLEVAIN_PROFILING=ON`, sandbox et tests), sans le lancer (#298).
