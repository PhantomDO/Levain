# `engine/assets`

## Rôle

Transformer les fichiers du disque en données prêtes pour le moteur. Plus tard : base d'assets par GUID,
cuisson, cache et hot-reload (SPECS §7).

**État en M4.1** : le chargement d'images (`loadImage`, stb_image), le calcul de leurs mipmaps
(`buildMipChain`), l'écriture de PNG (`savePng`, pour les captures), et l'**import glTF** (`loadGltf`,
fastgltf) : meshes, nœuds et couleur de base des matériaux lus en mémoire, puis instanciés en entités
(`instantiateModel`).

**En M4.2** : l'identité des assets ([ADR-0019](../../docs/adr/0019-identifiants-d-assets.md)). Chaque fichier
importable reçoit un GUID, écrit dans un `.meta` voisin avec le hash de son contenu ; `scanAssets` construit le
registre des chemins, et retrouve un fichier renommé hors du moteur par son hash. Les composants ne gardent qu'une
référence (`MeshRef`, un GUID et un indice), et `AssetsModule` les compte par des hooks flecs : ce qui tombe à
zéro se décharge en fin d'image (`takeUnusedAssets`).

**En M4.3** : la cuisson ([ADR-0020](../../docs/adr/0020-cuisson-des-assets.md)). `levain_cook <racine>…`
écrit dans `<racine>/.cooked/`, sans GPU : chaque glTF en `.lvmesh` (binaire brut, avec un champ `encoding`
réservé à une compression), chaque image en maître UASTC (`.ktx2`) et en cache BC7 du PC (`.bc7.ktx2`). Au
chargement, `loadModel` et `loadTextureData` prennent le plus rapide des fichiers à jour, et retombent sur la
source en le signalant. Sponza charge en 19 ms au lieu de 442, et ses textures tiennent 32 Mo de mémoire vidéo
au lieu de 128.

## Invariants

1. **Aucun GPU ici.** Le module rend des pixels en mémoire (`Image`) ; c'est `engine/render` qui les envoie au
   GPU. La cuisson des assets pourra donc tourner sur une machine de build sans carte graphique.
2. **stb reste privé** : seul `src/image.cpp` l'inclut, et l'API n'expose que des types standard.
3. **Une image est en RGBA8, couleurs en sRGB**, quel que soit le fichier d'origine (niveaux de gris, RGB sans
   alpha…) : un seul format à gérer en aval.
4. Le module dépend de `core` et de `scene` : l'import crée des entités. SPECS §7 le place au-dessus de
   `scene`, jamais l'inverse.
5. **fastgltf reste privé** : seul `src/gltf.cpp` l'inclut (`deps.asset-libraries-visibility`). L'import rend un
   `Model` fait de nos types, testable sans GPU ni monde flecs.
6. **Un nœud glTF devient une entité**, sous une racine qui déplace tout le modèle, avec son `Transform` et sa
   hiérarchie (`flecs::Parent`, ADR-0015). Un nœud qui porte un mesh reçoit un `MeshRef` : le GUID du modèle et
   l'indice du mesh (ADR-0019).
7. **Les matériaux sont ceux de glTF, metallic-roughness** (M5.1) : couleur de base, métal et rugosité, normal
   map, chacun avec son facteur. Leurs textures se désignent par référence ; une image embarquée est décodée à
   l'import, une image dans son fichier se charge quand le rendu en a besoin. La couleur de base est en sRGB ;
   rugosité-métal et normal map sont des données linéaires, ce que le rendu choisit à l'envoi. Une image peut
   venir d'un fichier, d'octets embarqués en base64 ou d'un buffer (`.glb`) : `decodeImage` lit la mémoire,
   `loadImage` un fichier.
8. **Les chemins ne vivent que dans le registre** (`AssetRegistry`). Tout le reste du moteur désigne un asset
   par son `AssetId` (ADR-0019).
9. **Le hash du registre suit le fichier** (ADR-0021) : `takeChangedAssets` le recalcule quand la date change,
   dans le registre et dans le `.meta`. Un fichier cuit est jugé sur ce hash : après une retouche, il est
   périmé, et `loadTextureData` reprend la source jusqu'au prochain `levain_cook`. Vérifié sur le sandbox en
   marche par `tools/texture-hot-reload.sh`.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/assets/image.hpp`](include/levain/assets/image.hpp) | `Image`, `loadImage`, `decodeImage`, `mipCountFor`, `buildMipChain`, `savePng` |
| [`include/levain/assets/gltf.hpp`](include/levain/assets/gltf.hpp) | `Model`, `loadGltf`, `instantiateModel` |
| [`include/levain/assets/asset_ref.hpp`](include/levain/assets/asset_ref.hpp) | `MeshRef`, `AssetsModule` (le comptage), `takeUnusedAssets`, `ModelCache`, `loadModel`, `loadTexture`, `loadTextureData` |
| [`include/levain/assets/asset_id.hpp`](include/levain/assets/asset_id.hpp) | `AssetId`, `contentHash`, `readMeta`, `writeMeta` |
| [`include/levain/assets/registry.hpp`](include/levain/assets/registry.hpp) | `AssetRegistry` (fichier, racine et hash de chaque asset), `scanAssets` (les cinq cas de l'ADR-0019), `pathOf`, `cookedPathOf` ; `watchAssets` et `takeChangedAssets`, le hot-reload (ADR-0021) |
| [`include/levain/assets/cooked.hpp`](include/levain/assets/cooked.hpp) | Le format `.lvmesh` : `writeCookedModel`, `readCookedModel`, `CookerVersion`, `MeshEncoding` |
| [`include/levain/assets/cooked_texture.hpp`](include/levain/assets/cooked_texture.hpp) | `TextureData`, `writeCookedTexture` (UASTC), `writePlatformTexture` (BC7), `readCookedTexture` |
| [`../../tools/cook/main.cpp`](../../tools/cook/main.cpp) | `levain_cook`, le cuiseur |

## Les mipmaps

Une texture vue de loin couvre moins de pixels à l'écran qu'elle n'en contient : sans précaution, chaque pixel
de l'écran tombe sur un texel presque au hasard et l'image scintille. Les **mipmaps** sont des copies de la
texture, chacune deux fois plus petite que la précédente, jusqu'à 1 × 1. Le GPU choisit le niveau dont les
texels ont à peu près la taille d'un pixel de l'écran.

**Le piège : la moyenne doit se faire en lumière linéaire.** Les octets d'une image sRGB ne sont pas
proportionnels à la lumière. Leur moyenne directe assombrit chaque niveau : un damier noir et blanc devient un
gris à 128 au lieu de 188, et une texture paraît plus sombre de loin que de près. `downsampleInLinearSpace`
convertit en linéaire, fait la moyenne, puis reconvertit (test « buildMipChain fait la moyenne en lumière
linéaire »).

**Le piège inverse : une donnée n'est pas une couleur.** Une normal map ou une rugosité se moyenne sur ses
octets : lue comme du sRGB, la moyenne de deux normales penchées en sens contraires penche encore.
`buildMipChain(image, ImageEncoding::Linear)` le fait. Le cuiseur apprend l'usage de chaque image des matériaux
glTF (`textureUsesOf`), et cuit les données sous `<guid>.linear.*` (ADR-0020, amendement du 03/10).

**Pourquoi sur le CPU** : c'est le plus simple (aucun pipeline de calcul), et la cuisson des assets fera ce
travail hors ligne un jour. Le filtre est écrit à la main plutôt que pris dans stb_image_resize2, qui déclenche
UBSan (`.agents/skills/build/GOTCHA.md`). La génération sur le GPU, par un compute shader comme dans Donut, se
justifiera si des textures sont créées à l'exécution.

## Équivalents ailleurs

| Moteur | Où | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | Texture Import, `TextureCompressor` | Les mips sont calculées à l'import et à la cuisson, jamais à l'exécution (**documenté** : sources publiques). |
| **Unity** | Texture Importer, option *Generate Mip Maps* | Calcul à l'import ; l'option *sRGB (Color Texture)* dit si la moyenne se fait en linéaire (**documenté** : manuel). |
| **Godot** | `Image::generate_mipmaps` | Calcul à l'import, avec une option pour les images sRGB (**documenté** : dépôt public). |
| **Unreal** | Interchange, importeur glTF | Un glTF devient des Static Meshes, et ses nœuds des Actors dans le niveau (**documenté** : documentation d'Epic). |
| **Unity** | package glTFast | Import à l'exécution ou dans l'éditeur, les nœuds deviennent des GameObjects (**documenté** : manuel du package). |
| **Godot** | `GLTFDocument` | Le format 3D recommandé ; un glTF s'importe comme une scène de nœuds (**documenté** : documentation officielle). C'est le plus proche d'ici : une entité par nœud. |
