# Plugin `terrain`

Le terrain de M5.6, premier plugin moteur (ADR-0018) : un relief par heightmap, dessiné par parcelles à plusieurs
niveaux de détail, et le mélange de trois textures par une carte de poids. Il dessine dans les étapes du
renderer (ADR-0025) ; le moteur ne le connaît pas.

## Rôle

- **Le relief** (`heightmap.hpp`) : une grille de hauteurs régulière, un échantillon par mètre (`Heightmap`), et
  ce qu'on en lit en tout point, la hauteur (`heightAt`, interpolée entre quatre échantillons) et la normale
  (`normalAt`). La vallée de *Rando* (`valleyOf`) est générée par le code : une cuvette aux bords irréguliers,
  avec un relief fractal par-dessus, la même pour la même graine. L'éditeur la sculptera (M7.6).
- **Les parcelles** (`patches.hpp`) : des carrés de 32 m (`PatchQuads`), 16 × 16 sur la vallée. Chacune a un
  niveau de détail selon sa distance à la caméra (`lodOf`) : un sommet tous les 1, 2, 4, 8 ou 16 m, un niveau de
  plus à chaque doublement de la distance. Et une boîte, de sa plus basse à sa plus haute hauteur
  (`patchBoundsOf`), pour le frustum culling du moteur (#132). Entre deux parcelles de niveaux différents, les
  bords ne tombent pas aux mêmes hauteurs : une **jupe**, une bande verticale qui descend sous chaque bord
  (`patchGeometryOf`), cache la fente. Le niveau se choisit par la distance à la caméra, la même dans l'image et
  dans les ombres : jusqu'à 64 m, un sommet par mètre.
- **Les couches** (`layers.hpp`) : l'herbe sur le plat du fond, un sol rocailleux en hauteur, la roche dans les
  pentes de plus de 28 à 45°. `layerWeightsOf` les dose, de somme 1 ; `weightMapOf` en fait la carte de poids,
  en RGBA 8 bits, que lira le shader.

- **Le rendu** (`terrain_pass.hpp`) : la heightmap (R16 flottant, de 0 à 1) et la carte de poids en textures,
  une grille de sommets par niveau de détail, et deux pipelines. Le shader (`shaders/terrain.slang`) pose chaque
  sommet sur la heightmap, calcule la normale au pixel, et éclaire par `lighting.slang` comme les meshes
  (ADR-0025). `addTerrainPasses` inscrit le dessin dans l'étape `Opaque` du renderer, et l'ombre portée dans
  `ShadowCasters` ; chaque parcelle hors du frustum de l'étape est écartée. Le sandbox le montre :
  `levain_sandbox --view terrain`.

## Invariants

- Un terrain fait un nombre entier de parcelles de côté : `(size − 1)` est un multiple de 32.
- Les hauteurs sont en mètres, l'échantillon (0, 0) à l'origine, x le long d'une ligne et z d'une ligne à
  l'autre.
- La heightmap est en R16 flottant, pas en R16 normalisé ni en R32 flottant : les deux manquent au cœur de
  WebGPU (absent pour l'un, non filtrable pour l'autre). Ramenée de 0 à 1, elle garde un pas de moins de 5 cm.
- Le plugin ne dépend que de `core` et de `render` (`levain_add_plugin`, ADR-0018) : `render` pour `Box`, et pour
  ses passes de rendu.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/terrain/heightmap.hpp`](include/levain/terrain/heightmap.hpp) | `Heightmap`, `valleyOf`, `heightAt`, `normalAt` |
| [`include/levain/terrain/patches.hpp`](include/levain/terrain/patches.hpp) | `patchesFor`, `lodOf`, `patchBoundsOf` |
| [`include/levain/terrain/layers.hpp`](include/levain/terrain/layers.hpp) | `layerWeightsOf`, `weightMapOf` |
| [`include/levain/terrain/terrain_pass.hpp`](include/levain/terrain/terrain_pass.hpp) | `createTerrainPass`, `addTerrainPasses`, `drawTerrain`, `drawTerrainShadow` |
| [`shaders/terrain.slang`](shaders/terrain.slang) | le relief, la normale au pixel, l'ombre portée |

Les tests : `tests/terrain_test.cpp`.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Landscape | Dans le cœur du moteur ; des composants carrés, à plusieurs niveaux de détail, et des couches peintes dans l'éditeur (**documenté** : documentation d'Epic). |
| **Unity** | Terrain | Une heightmap, des *Terrain Layers* mélangés par une *splatmap*, des niveaux de détail réglés par *Pixel Error* (**documenté** : manuel). |
| **Godot** | aucun terrain intégré | Godot 4 n'en a pas ; des addons, comme Terrain3D, le fournissent (**documenté** : dépôt public de Terrain3D). |
