# Plugin `water`

L'eau de M5.7, deuxième plugin moteur (ADR-0018) : un lac calme, posé sur le terrain. Il dessine dans l'étape
`Transparent` du renderer (ADR-0025), après le ciel ; le moteur ne le connaît pas.

## Rôle

- **Le lac** (`lake.hpp`) : un disque d'eau plate à une hauteur donnée (`Lake`). Son fond est le terrain : la
  vallée de *Rando* a désormais un creux pour lui (`ValleySettings::lakeCenter`, `lakeRadius`, `lakeDepth`).
  L'eau se dessine sur le carré du disque, et le terrain qui dépasse la cache.
- **Les vaguelettes** (`rippleNormalMapOf`) : la surface reste plate, ce sont ses normales qui bougent. La carte
  est un bruit fractal à quatre octaves dont on prend la pente, calculé par le code, sur une grille qui se
  referme sur elle-même : la carte se répète sans couture. Ses mips se moyennent sur les octets
  (`ImageEncoding::Linear`) : une moyenne en sRGB pencherait les normales.
- **Le rendu** (`water_pass.hpp`, `shaders/water.slang`) : deux triangles, sans vertex buffer, et un mélange
  prémultiplié avec l'image derrière. Le shader lit deux copies des vaguelettes qui glissent, à 9 et 23,3 m de
  côté, et en tire :
  - le ciel reflété, selon le Fresnel de l'eau (2 % de face, presque tout de biais) ;
  - le reflet du soleil, par la BRDF commune (`lighting.slang`), à l'ombre près ;
  - le fond, vu au travers d'une eau qui absorbe le rouge d'abord, d'autant plus qu'elle est profonde, et la
    lumière que l'eau diffuse à sa place. La profondeur se lit dans la heightmap du terrain : le niveau de l'eau
    moins la hauteur du fond. Sous 15 cm, l'eau s'efface : le rivage n'a pas d'arête.

  L'eau teste la profondeur sans l'écrire : le terrain qui dépasse la cache, et elle ne cache rien derrière
  elle. Le sandbox la montre : `levain_sandbox --view terrain`.

## Invariants

- La carte de vaguelettes est en espace tangent : x dans le rouge, y dans le vert, le haut dans le bleu.
- Le plugin dépend d'`assets`, de `render` et du plugin `terrain`, déclaré (`levain_add_plugin`, ADR-0018) : le
  fond du lac est la heightmap du terrain.
- Ce qui se trouve sous l'eau sans être le terrain (un rocher, un personnage) ne l'assombrit pas : il faudrait
  la profondeur de l'image, que le renderer ne copie pas.
- Seul le ciel se reflète, pas le terrain : un reflet de la scène (plan ou écran) viendra quand il manquera.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/water/lake.hpp`](include/levain/water/lake.hpp) | `Lake`, `rippleNormalMapOf` |
| [`include/levain/water/water_pass.hpp`](include/levain/water/water_pass.hpp) | `createWaterPass`, `addWaterPasses`, `drawWater` |
| [`shaders/water.slang`](shaders/water.slang) | le Fresnel, le reflet du soleil, la couleur selon la profondeur |

Les tests : `tests/water_test.cpp`.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Water (plugin) | Un plugin du moteur : lacs, rivières et océans tracés par des splines, qui creusent le *Landscape* (**documenté** : documentation d'Epic). |
| **Unity** | Water System (HDRP) | Dans le pipeline HDRP seulement ; l'URP n'a pas d'eau intégrée (**documenté** : manuel de HDRP). |
| **Godot** | aucune eau intégrée | Un shader à écrire soi-même, ou un addon (**documenté** : la documentation n'en propose pas). |
