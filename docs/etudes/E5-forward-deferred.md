# E5 — Forward, deferred, forward+

> Écrite le 03/10/2026, en fin de M5.7. Lecture : 10 minutes.
> Convention : ce qui est **documenté** renvoie à une source ; ce qui est **déduit** est signalé.

## La question

Un pixel reçoit la lumière de toutes les sources qui le touchent. La question est de savoir **à quel moment** on
fait ce calcul, et **sur quelles lumières** :

- **Forward** : chaque objet calcule son éclairage au moment où il se dessine, en parcourant une liste de
  lumières. C'est simple, et un matériau peut faire ce qu'il veut. Mais un pixel caché par un objet dessiné après
  lui est éclairé pour rien, et la liste doit rester courte.
- **Deferred** : on dessine d'abord les propriétés des surfaces visibles dans plusieurs images, le **G-buffer**
  (couleur, normale, rugosité, profondeur), puis on éclaire chaque pixel une seule fois. Le coût ne dépend plus
  que des pixels et des lumières. Mais le G-buffer coûte de la mémoire et de la bande passante, la transparence
  n'y entre pas, et le MSAA devient très cher.
- **Forward+ (ou clustered)** : c'est du forward, avec une liste de lumières **par case de l'écran** au lieu
  d'une liste par objet. Un compute range les lumières dans une grille du volume de la caméra, et chaque pixel ne
  lit que celles de sa case. On garde la souplesse du forward, avec des centaines de lumières.

## Ce que fait notre moteur (et pourquoi)

Levain est en **forward+ en clusters** (ADR-0024, M5.1) : une grille de 16 × 9 × 24 cases, 32 lumières au plus
par case, et le soleil à part, avec ses cascades. Trois raisons au choix :

1. **WebGPU** est une cible (ADR-0023). Il plafonne les octets qu'un pixel peut écrire dans ses cibles de rendu
   (32 par défaut), et un navigateur mobile manque de bande passante : un G-buffer y serait à l'étroit, et
   cher (déduit).
2. **La transparence** passe par le même code : l'eau de M5.7 appelle les fonctions de `lighting.slang`, comme le
   terrain et les meshes. En deferred, il aurait fallu un second chemin d'éclairage pour elle.
3. **Les mesures** le permettent : Sponza s'éclaire en 0,29 ms (M5.5), et la vallée avec son lac et son herbe en
   moins de 1 ms GPU par image (M5.7).

Ce que le choix nous coûte : un pixel de brin d'herbe caché par un autre brin est éclairé pour rien. Avec 240 000
brins, c'est la première passe où ce gaspillage se voit (0,24 ms de plus au bord du lac). Le remède du forward est
une **pré-passe de profondeur**, qu'on n'a pas encore (déduit).

## Unreal

- **Deferred par défaut**, parce qu'il « provides the most versatility and grants access to more rendering
  features » (documenté [1]).
- Un **Forward Shading Renderer** au choix, recommandé pour la VR : des passes plus rapides au départ, et le MSAA,
  qui évite le flou du TAA quand la tête bouge (documenté [1]).
- En forward, Unreal **perd les techniques en espace écran** (SSR, SSAO, ombres de contact) et l'ombre dynamique
  des translucides (documenté [1]). Elles lisent les normales et la rugosité de chaque pixel, que seul le G-buffer
  conserve (déduit).
- **Déduit** : même en deferred, les translucides d'Unreal (l'eau, le verre) s'éclairent en forward, après la
  passe deferred. C'est le mélange qu'on retrouve partout.

## Unity

- **URP** propose trois chemins, au choix par renderer (documenté [2]) :
  - **Forward** : 9 lumières temps réel au plus par objet ;
  - **Forward+** : pas de limite par objet, et de 256 à 257 par caméra selon la plateforme ;
  - **Deferred** : pas de limite pour les opaques, mais 9 pour les transparents, et pas de MSAA. Il coûte cher sur
    mobile, à cause des passes du G-buffer.
- **HDRP** choisit son mode dans son asset (*Lit Shader Mode*, documenté [3]). Certains matériaux passent
  toujours en forward, quel que soit le mode : les tissus, les cheveux, les transparents, et d'autres encore. Le
  deferred y est « faster in most scenarios » avec plusieurs lumières, et le forward peut gagner avec un seul
  soleil [3].

## Godot

- **Trois renderers**, tous en forward (documenté [4]) :
  - **Forward+** sur PC, en Vulkan, Direct3D 12 ou Metal : du forward en clusters, 512 lumières par case ;
  - **Mobile** : un forward en une passe, 8 lumières par mesh ;
  - **Compatibility** sur OpenGL, pour le web et les petites machines : 8 lumières par mesh, une passe de plus
    par lumière qui projette une ombre.
- Godot est le seul des trois à n'avoir **aucun deferred**. C'est aussi le moteur qui ressemble le plus au nôtre :
  forward+ sur les machines modernes, et un forward simple pour le web.

## Autres

- **id Tech 6** (*Doom*, 2016) : du **forward en clusters** sur PC et consoles, présenté à SIGGRAPH (documenté
  [5]). C'est la preuve qu'un jeu AAA à 60 images/s peut se passer de deferred.

## Côte à côte

| | Unreal | Unity URP | Unity HDRP | Godot | Levain |
|---|---|---|---|---|---|
| Par défaut | Deferred | Forward (au choix : Forward+, Deferred) | Au choix (*Lit Shader Mode*) | Forward+ (PC) | Forward+ |
| Lumières nombreuses | Deferred | Forward+ ou Deferred | Tuiles et clusters | Clusters (512 par case) | Clusters (32 par case) |
| Transparents | Forward | Forward (9 lumières en Deferred) | Forward | Forward | Forward, même code |
| MSAA | En forward seulement | Pas en Deferred | — | Oui | Possible, pas fait |
| Web et mobile | — | Forward | — | Mobile, Compatibility | Le même forward+ (WebGPU) |

## Ce qu'on en retient

1. **Personne ne fait du « tout deferred »** : les transparents passent toujours en forward. Un moteur deferred a
   donc **deux** chemins d'éclairage, et un moteur forward n'en a qu'un. Le nôtre n'a qu'un `shadeSurface`, et le
   lac de M5.7 en a profité directement.
2. **Le deferred achète les effets en espace écran** (SSR, SSAO, decals) : c'est la vraie raison d'Unreal, plus
   que le nombre de lumières. *Rando* est un jeu de plein air, avec un soleil et quelques lumières : rien de ce
   qu'il demande n'exige un G-buffer. Une SSAO, si elle manquait, peut se calculer en forward à
   partir de la profondeur et de normales écrites à part (déduit).
3. **Notre prochain gain en forward est la pré-passe de profondeur** : l'herbe dense est le cas d'école du pixel
   éclairé pour rien. À mesurer quand *Rando* aura sa vraie densité d'herbe, pas avant.
4. **Godot confirme notre trajectoire** : du forward+ sur les machines modernes, et un chemin plus simple pour le
   web. Nous servons le web avec le même forward+, ce qu'aucun des trois ne fait. WebGPU le permet, là où WebGL ne
   le permettait pas (déduit).

## Sources

1. Epic Games, *Forward Shading Renderer in Unreal Engine* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/forward-shading-renderer-in-unreal-engine
2. Unity, *Rendering paths comparison* (URP) —
   https://docs.unity3d.com/6000.0/Documentation/Manual/urp/rendering-paths-comparison.html
3. Unity, *Forward and Deferred rendering* (HDRP 17) —
   https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Forward-And-Deferred-Rendering.html
4. Godot, *Overview of renderers* — https://docs.godotengine.org/en/stable/tutorials/rendering/renderers.html
5. T. Sousa, J. Geffroy, *The devil is in the details: idTech 666*, SIGGRAPH 2016, Advances in Real-Time
   Rendering — https://advances.realtimerendering.com/s2016/
