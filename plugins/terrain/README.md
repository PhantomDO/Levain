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
  (`patchBoundsOf`), pour le frustum culling du moteur (#132).
- **Les couches** (`layers.hpp`) : l'herbe sur le plat du fond, un sol rocailleux en hauteur, la roche dans les
  pentes de plus de 28 à 45°. `layerWeightsOf` les dose, de somme 1 ; `weightMapOf` en fait la carte de poids,
  en RGBA 8 bits, que lira le shader.

## Invariants

- Un terrain fait un nombre entier de parcelles de côté : `(size − 1)` est un multiple de 32.
- Les hauteurs sont en mètres, l'échantillon (0, 0) à l'origine, x le long d'une ligne et z d'une ligne à
  l'autre.
- Le plugin ne dépend que de `core` et de `render` (`levain_add_plugin`, ADR-0018) : `render` pour `Box`, et pour
  ses passes de rendu.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/terrain/heightmap.hpp`](include/levain/terrain/heightmap.hpp) | `Heightmap`, `valleyOf`, `heightAt`, `normalAt` |
| [`include/levain/terrain/patches.hpp`](include/levain/terrain/patches.hpp) | `patchesFor`, `lodOf`, `patchBoundsOf` |
| [`include/levain/terrain/layers.hpp`](include/levain/terrain/layers.hpp) | `layerWeightsOf`, `weightMapOf` |

Les tests : `tests/terrain_test.cpp`.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Landscape | Dans le cœur du moteur ; des composants carrés, à plusieurs niveaux de détail, et des couches peintes dans l'éditeur (**documenté** : documentation d'Epic). |
| **Unity** | Terrain | Une heightmap, des *Terrain Layers* mélangés par une *splatmap*, des niveaux de détail réglés par *Pixel Error* (**documenté** : manuel). |
| **Godot** | aucun terrain intégré | Godot 4 n'en a pas ; des addons, comme Terrain3D, le fournissent (**documenté** : dépôt public de Terrain3D). |
