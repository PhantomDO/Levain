# Plugin `grass`

L'herbe de M5.7, troisième plugin moteur (ADR-0018) : des brins denses, répartis sur le terrain par une carte de
densité. Elle dépend du plugin `terrain`, déclaré : ses brins se posent sur sa heightmap.

## Rôle

- **La carte de densité** (`densityMapOf`) : le poids de la couche d'herbe du terrain (`weightMapOf`), éteint
  sous l'eau et sur les 30 cm de rivage au-dessus. L'herbe pousse donc là où le sol est déjà vert.
- **Combien de brins** (`bladeCountOf`) : par parcelle du terrain, tous les brins jusqu'à 16 m de la caméra
  (48 par m² là où la densité vaut 1), puis de moins en moins, comme l'inverse du carré de la distance : autant
  de brins par pixel d'écran, à peu près. Plus aucun au-delà de 64 m, où la couleur du sol suffit.
- **La forme d'un brin** (`bladeGeometryOf`) : des trapèzes qui s'affinent de la base à la pointe. Une seule
  forme pour tous les brins, que le shader étire, tourne et courbe.
- **Le rendu** (`grass_pass.hpp`, `shaders/grass.slang`) : un dessin instancié par parcelle proche de la caméra
  et dans son frustum. Le shader tire tout du numéro d'instance :
  - la place du brin, un point de la **suite R2** de Roberts, à faible discrépance : ses premiers points couvrent
    la parcelle régulièrement, quel qu'en soit le nombre, et une parcelle lointaine en dessine moins sans trou ;
  - le reste par un hachage : l'orientation, la taille (de 30 à 70 cm), la teinte, et s'il pousse là, selon la
    carte de densité. Un brin qui ne pousse pas est réduit à un point hors de l'écran : le GPU l'écarte ;
  - le vent, une onde de rafales qui avance dans sa direction, et un frémissement propre à chaque brin ; la
    courbure croît comme le carré de la hauteur, et la base ne bouge pas.

  Les brins rapetissent sur le dernier quart de la portée : l'herbe s'efface au lieu de s'arrêter net. Ils sont
  éclairés par `lighting.slang`, avec une normale arrondie en travers du brin, et reçoivent l'ombre du soleil.
  Le sandbox les montre : `levain_sandbox --view terrain`.

## Invariants

- Aucun buffer d'instances : la place d'un brin se tire de son numéro d'instance, dans sa parcelle. Le CPU ne
  choisit que le nombre de brins de chaque parcelle.
- Le plugin dépend de `render` et du plugin `terrain`, déclaré (`levain_add_plugin`, ADR-0018).
- L'herbe ne projette pas d'ombre : 240 000 brins dans quatre cascades coûteraient plus que ce qu'ils montrent.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/grass/grass.hpp`](include/levain/grass/grass.hpp) | `GrassSettings`, `densityMapOf`, `bladeCountOf`, `bladeGeometryOf` |
| [`include/levain/grass/grass_pass.hpp`](include/levain/grass/grass_pass.hpp) | `createGrassPass`, `addGrassPasses`, `drawGrass` |
| [`shaders/grass.slang`](shaders/grass.slang) | la place, la forme et le vent de chaque brin |

Les tests : `tests/grass_test.cpp`.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Landscape Grass Type | Des meshes semés sur les couches du *Landscape*, selon leur poids, autour de la caméra (**documenté** : documentation d'Epic). Foliage, pour ce qu'on peint à la main. |
| **Unity** | Terrain Details | Des brins ou des meshes peints sur le *Terrain*, avec une distance d'affichage et une densité (**documenté** : manuel). |
| **Godot** | MultiMeshInstance3D | Pas d'herbe intégrée : un *MultiMesh* d'instances, réparties par un script ou un addon (**documenté** : documentation de Godot). |
