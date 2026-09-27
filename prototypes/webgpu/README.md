# Prototype WebGPU (jetable, branche `spike/webgpu`)

Ce que ce prototype a vérifié, pour l'ADR-0023 (le 27/09/2026, sur la machine de référence) :

1. **Le cube texturé de M2.2 tourne en WebGPU dans Firefox 156**, compilé par Emscripten 6.0.10 avec
   `emdawnwebgpu` : 60 images/s en headless (la limite de synchronisation), aucune erreur WebGPU. `.wasm` de
   173 Ko, `.js` de 124 Ko.
2. **Les shaders du moteur passent en WGSL par Slang**, et le compilateur WGSL de Firefox les accepte : `triangle`,
   `mesh` (avec les décalages de binding de NVRHI, `-fvk-*-shift`), et `skinning` une fois ses `ByteAddressBuffer`
   remplacés par des `StructuredBuffer<uint>` (`skinning-portable.slang`) : la cible WGSL de Slang ne sait pas les
   lire.

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
