# ADR-0020 — Cuisson des assets : UASTC transcodé au chargement, meshes en binaire brut

- **Statut** : accepté le 2026-09-24 (validé par Donnovan)
- **Date** : 2026-09-24
- **Milestone** : M4.3

## Contexte

M4.1 a mesuré le chargement de Sponza depuis ses sources, en Release : **215 ms** pour lire le glTF et décoder
ses JPEG, puis **252 ms** pour calculer les mips et tout envoyer au GPU. Le critère de M4.3 demande **au moins 5
fois plus vite**, soit moins de 93 ms, avec un outil de cuisson qui tourne **sans GPU** (#91). Il demande aussi de
mesurer la mémoire vidéo des textures avant et après leur compression en BC7 (#92). SPECS §6 prévoit libktx.

La cuisson convertit une source (glTF, PNG, JPEG) en un format que le moteur charge sans analyse, et qu'il copie
presque tel quel au GPU. Deux choix y sont durables : le format des fichiers cuits, et le moment où l'on cuit.
Donnovan les a tranchés sur sondages le 24/09/2026. Il a aussi demandé de laisser une place pour la Switch 2 et
le mobile.

## Options envisagées

**1. Les textures**, en KTX2, le format de conteneur de Khronos :

| Option | Pour | Contre |
|---|---|---|
| **UASTC (Basis Universal), transcodé au chargement** | Un seul fichier pour toutes les plateformes : libktx le transcode en BC7 sur PC, en ASTC sur Switch 2 et mobile. Aucune dépendance de plus que libktx | Un transcodage à chaque chargement, à mesurer face au critère « 5 fois plus vite » |
| BC7 écrit directement | Rien à faire au chargement | Un format PC : la Switch 2 et le mobile demanderaient une cuisson par plateforme ; libktx n'encode pas le BC7, il faudrait un autre encodeur |

**2. Les meshes**

| Option | Pour | Contre |
|---|---|---|
| **Binaire brut, avec une place pour une compression** | Le chargement n'est qu'une lecture et une copie. Un champ « encodage » dans l'en-tête réserve la place d'une compression (meshoptimizer, par exemple), si la mémoire ou l'espace d'une console ou d'un téléphone la réclament (demande de Donnovan) | Des fichiers plus gros qu'une version compressée |
| Binaire compressé (meshoptimizer) | 2 à 4 fois plus petit | Un décodage au chargement et une dépendance, pour un besoin qui n'existe pas encore |

**3. Le moment de la cuisson**

| Option | Pour | Contre |
|---|---|---|
| **Un outil explicite, et le repli sur la source** | `levain_cook` cuit sans GPU. Au chargement, le moteur prend la version cuite si elle est à jour, sinon la source, avec un avertissement : on a toujours une image, et le retard se voit | Deux chemins de chargement à garder vivants |
| À la demande, au chargement (l'import d'Unity) | Rien à lancer | Le premier démarrage est long, et le moteur embarque l'encodeur |
| Un outil explicite, sans repli | Le plus strict, ce qu'on veut dans un jeu livré | Pénible en développement : un oubli de cuisson bloque tout |

**4. L'emplacement** : un **dossier `.cooked/`** dans chaque racine d'assets, ignoré par git, les fichiers
nommés par GUID. C'est le `Library/` d'Unity, le *Derived Data Cache* d'Unreal, le `.godot/imported` de Godot. Un
dossier dans `build/` a été écarté : il serait recuit pour chaque preset.

## Décision

**`levain_cook <racine>`** est un exécutable sans GPU. Il scanne la racine (ADR-0019), puis écrit dans
`<racine>/.cooked/` :

- pour chaque image, `<guid>.ktx2` : les mips calculées par `buildMipChain` (en lumière linéaire, M2.2), encodées
  en **UASTC**, avec la supercompression zstd de KTX2 ;
- pour chaque glTF, `<guid>.lvmesh` : les nœuds, les meshes et les matériaux. Une texture y est désignée par un
  `AssetRef` : le GUID de son fichier image, ou celui du glTF avec l'indice de l'image si elle y est embarquée.
  **Plus aucun chemin, même dans un fichier cuit** : la limite des références internes du glTF disparaît.

**Le format `.lvmesh`**, en petit-boutiste : un en-tête (la signature `LVMS`, la version du format, un champ
**`encoding`**, les tailles), puis les tableaux tels que le GPU et le moteur les attendent. `encoding = 0` veut
dire « brut ». Toute autre valeur est réservée à une compression future, et refusée aujourd'hui par un échec
explicite.

**Le chargement** : un fichier cuit est **à jour** si son en-tête porte le hash de la source (celui du `.meta`)
et la version courante du cuiseur. Sinon, le moteur charge la source et le signale. Les textures UASTC sont
transcodées en **BC7** si le GPU le prend en charge, en RGBA8 sinon.

**libktx n'est visible que dans `engine/assets` et dans le cuiseur**, avec un contrôle comme celui de fastgltf.

## Conséquences

- **Le risque principal est le transcodage.** Basis annonce l'UASTC vers BC7 comme très rapide, mais 25 textures
  de 1024 × 1024 avec leurs mips restent à mesurer. **La première PR de texture le mesure.** Si le critère
  « 5 fois plus vite » est manqué à cause de lui, la parade est connue : garder aussi, dans `.cooked/`, le BC7
  transcodé pour la plateforme courante, c'est-à-dire une cuisson par plateforme, comme Unreal et Unity. Ce serait
  un amendement de cet ADR, pas un nouveau choix.
- **La mémoire vidéo baisse d'un facteur 4** entre le RGBA8 et le BC7, en théorie. #92 le mesure.
- **Deux chemins de chargement** : la source (M4.1) et le cuit. La CI charge les deux, sinon celui qui ne sert pas
  pourrirait.
- **Nouvelles dépendances** : `ktx` (Apache 2.0) et `zstd` (BSD). *Rando* devra les recopier dans son manifeste,
  et le contrôle de #122 le lui rappellera.
- **L'encodage UASTC est lent** : de l'ordre de la seconde par texture selon mon estimation, non mesurée. Cuire
  Sponza prendra donc sans doute plus longtemps que la charger depuis ses sources. C'est le prix d'un chargement
  rapide, payé une fois par modification.
- **Pour la Switch 2 et le mobile** : le transcodage vers l'ASTC est déjà dans libktx, et le champ `encoding` des
  meshes attend une compression. Rien n'est à refaire, seulement à ajouter.

## Amendement du 24/09/2026 : un cache par plateforme

Le risque de la première conséquence s'est réalisé, et sa parade a été appliquée. Mesuré sur une texture de Sponza
de 1024 × 1024 avec ses mips (Release) : **9,4 ms de transcodage** UASTC vers BC7, pour 1,5 ms de lecture
(zstd compris), contre 26 ms depuis la source. Sur 25 textures, le « 5 fois plus vite » aurait été manqué.

Le cuiseur écrit donc **deux fichiers par texture** : le maître UASTC (`<guid>.ktx2`), universel, d'où partira
l'ASTC de la Switch 2 ou du mobile ; et **le cache de la plateforme** (`<guid>.bc7.ktx2`), transcodé une fois à la
cuisson, sans supercompression, et qui se charge en **0,57 ms**. Au chargement, le moteur essaie le cache, puis
le maître (transcodé), puis la source. Une image embarquée dans un glTF prend le nom `<guid>.<indice>`.

Résultat sur Sponza (Release) : **19 ms au lieu de 442** (×23 ; ×7,9 en comptant le scan des racines), et **32 Mo
de mémoire vidéo au lieu de 128**. L'image rendue s'écarte de celle des sources de 0,64 niveau sur 255 en moyenne
(PSNR de 48 dB, imperceptible).

## Amendement du 03/10/2026 : couleur ou données

Les mips se moyennaient toujours en lumière linéaire, les octets lus comme du sRGB (M2.2). C'est juste pour une
couleur, et faux pour une **donnée** : une normal map ou une rugosité-métal. Deux normales penchées en sens
contraires (64 et 192) donnaient un mip penché (≈ 150) au lieu d'une normale droite (128) : les normales des
surfaces lointaines penchaient toutes du même côté.

Le cuiseur ne savait pas à quoi sert une image : un PNG de Sponza n'est qu'un fichier pour lui. Sondage du
03/10 : **il l'apprend des matériaux glTF**, plutôt que d'un réglage à poser dans le `.meta` de chaque normal map.

- Le cuiseur lit d'abord les modèles. `textureUsesOf` donne chaque texture de leurs matériaux avec son
  encodage : la couleur de base en sRGB, la normal map et la rugosité-métal en données (`ImageEncoding::Linear`).
- Une image lue comme des données est cuite sous `<guid>.linear.ktx2` et `<guid>.linear.bc7.ktx2`, ses mips
  moyennées sur les octets. Une image qui sert aux deux usages est cuite deux fois, et une image qu'aucun matériau
  ne désigne reste une couleur.
- Au chargement, `loadTextureData` reçoit l'encodage du matériau. Le repli sur la source calcule les mêmes mips.

Unity et Godot font de même à l'import : une texture marquée *Normal map* se filtre et se compresse comme des
données, et Godot la détecte quand un matériau l'utilise comme telle.

## Ce que font les autres moteurs

- **Unreal** (documenté, [E2](../etudes/E2-ressources-gpu.md)) : les données dérivées vont dans le *Derived Data
  Cache*, sous une empreinte de leurs entrées ; la cuisson produit des fichiers **par plateforme**.
- **Unity** (documenté [1]) : un asset importé va dans `Library/`, selon les réglages de son `.meta`. Les formats
  de texture se règlent par plateforme.
- **Godot** (documenté [2]) : le mode *VRAM Compressed* produit du BC7 (BPTC) sur PC et de l'ASTC sur mobile, en
  haute qualité. Un mode **Basis Universal** encode une texture « that can be transcoded to most GPU-compressed
  formats at load-time » : c'est notre choix. Godot le dit de moindre qualité et lent à encoder, ce qui vise
  surtout le mode ETC1S de Basis ; l'UASTC que nous prenons est son mode haute qualité.

## Sources

1. Unity, *Asset metadata* — https://docs.unity3d.com/6000.2/Documentation/Manual/AssetMetadata.html
2. Godot, *Importing images* — https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_images.html
3. Khronos, *KTX-Software* (libktx, Apache 2.0) — https://github.com/KhronosGroup/KTX-Software
