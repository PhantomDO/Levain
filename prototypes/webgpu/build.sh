#!/usr/bin/env bash
# Construit le prototype WebGPU (spike/webgpu) pour le navigateur, dans prototypes/webgpu/out/.
#   source ~/emsdk/emsdk_env.sh && ./prototypes/webgpu/build.sh
#   python3 -m http.server -d prototypes/webgpu/out 8000   # puis http://localhost:8000
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$here/out"
mkdir -p "$out"
slangc="$root/build/linux-debug/vcpkg_installed/x64-linux/tools/shader-slang/slangc"

# Le shader Slang en WGSL : le même compilateur que le moteur, une autre cible.
"$slangc" "$here/cube.slang" -target wgsl -entry vertexMain -stage vertex \
    -entry fragmentMain -stage fragment -matrix-layout-column-major -o "$out/cube.wgsl"

cd "$out"
em++ "$here/main.cpp" -std=c++23 -O2 --use-port=emdawnwebgpu \
    --embed-file cube.wgsl --shell-file "$here/shell.html" -o index.html
ls -l index.html index.js index.wasm
