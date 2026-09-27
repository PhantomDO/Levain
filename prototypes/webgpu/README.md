# Prototype WebGPU (jetable, branche `spike/webgpu`)

Ce que ce prototype a vérifié, pour l'ADR-0023 (le 27/09/2026, sur la machine de référence) :

1. **Le cube texturé de M2.2 tourne en WebGPU dans Firefox 156**, compilé par Emscripten 6.0.10 avec
   `emdawnwebgpu` : 60 images/s en headless (la limite de synchronisation), aucune erreur WebGPU. `.wasm` de
   173 Ko, `.js` de 124 Ko.
2. **Les shaders du moteur passent en WGSL par Slang**, et le compilateur WGSL de Firefox les accepte : `triangle`,
   `mesh` (avec les décalages de binding de NVRHI, `-fvk-*-shift`), et `skinning` une fois ses `ByteAddressBuffer`
   remplacés par des `StructuredBuffer<uint>` (`skinning-portable.slang`) : la cible WGSL de Slang ne sait pas les
   lire.

3. **Le même code tourne en natif sur Dawn** (port vcpkg `dawn[vulkan]`, compilé en 4,3 min, 28 Mo de sources,
   1,1 Go installé avec ses dépendances, Debug et Release) : même image, **0,065 ms par image** sur la RX 9070 XT,
   attente du GPU comprise (300 images, 3 lancements).

## Refaire les mesures

```bash
source ~/emsdk/emsdk_env.sh && ./prototypes/webgpu/build.sh
python3 -m http.server -d prototypes/webgpu/out 8765 &
# Un Firefox de test, profil jetable avec dom.webgpu.enabled et gfx.webgpu.ignore-blocklist :
firefox --headless --no-remote --profile <profil> --window-size 960,540 --remote-debugging-port 9222 &
node prototypes/webgpu/snapshot.mjs http://localhost:8765/ cube.png 6
node prototypes/webgpu/snapshot.mjs http://localhost:8765/validate.html validate.png 4
```

`snapshot.mjs` pilote Firefox par WebDriver BiDi : titre, console et capture, sans écran. Les WGSL du moteur
(`out/engine/`) se produisent avec `slangc -target wgsl` et les mêmes options que `shaders/CMakeLists.txt`.

En natif :

```bash
~/vcpkg/vcpkg install --x-manifest-root=prototypes/webgpu/native --x-install-root=<dawn> --triplet x64-linux
cmake -S prototypes/webgpu/native -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_INSTALLED_DIR=<dawn> \
  -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build <build>
cd prototypes/webgpu/out && <build>/cube_native   # écrit cube-natif.ppm
```

