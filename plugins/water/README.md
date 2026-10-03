# Plugin `water`

L'eau de M5.7, deuxième plugin moteur (ADR-0018) : un lac calme, posé sur le terrain. Il dessinera dans
l'étape `Transparent` du renderer (ADR-0025) ; le moteur ne le connaît pas.

## Rôle

- **Le lac** (`lake.hpp`) : un disque d'eau plate à une hauteur donnée (`Lake`). Son fond est le terrain : la
  vallée de *Rando* a désormais un creux pour lui (`ValleySettings::lakeCenter`, `lakeRadius`, `lakeDepth`).
  L'eau se dessine sur le carré du disque, et le terrain qui dépasse la cache.
- **Les vaguelettes** (`rippleNormalMapOf`) : la surface reste plate, ce sont ses normales qui bougent. La carte
  est un bruit fractal à quatre octaves dont on prend la pente, calculé par le code, sur une grille qui se
  referme sur elle-même : la carte se répète sans couture. Ses mips se moyennent sur les octets
  (`ImageEncoding::Linear`) : une moyenne en sRGB pencherait les normales.

## Invariants

- La carte de vaguelettes est en espace tangent : x dans le rouge, y dans le vert, le haut dans le bleu.
- Le plugin ne dépend que de `assets` (`levain_add_plugin`, ADR-0018), pour `Image`.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/water/lake.hpp`](include/levain/water/lake.hpp) | `Lake`, `rippleNormalMapOf` |

Les tests : `tests/water_test.cpp`.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Water (plugin) | Un plugin du moteur : lacs, rivières et océans tracés par des splines, qui creusent le *Landscape* (**documenté** : documentation d'Epic). |
| **Unity** | Water System (HDRP) | Dans le pipeline HDRP seulement ; l'URP n'a pas d'eau intégrée (**documenté** : manuel de HDRP). |
| **Godot** | aucune eau intégrée | Un shader à écrire soi-même, ou un addon (**documenté** : la documentation n'en propose pas). |
