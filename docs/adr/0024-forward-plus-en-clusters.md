# ADR-0024 — Un rendu forward+ en clusters

- **Statut** : accepté
- **Date** : 2026-10-02
- **Milestone** : M5.1

## Contexte

La phase 5 commence par le PBR (M5.1) : le modèle de matériau de glTF (metallic-roughness), une BRDF
Cook-Torrance, une lumière directionnelle et des lumières ponctuelles. Avant d'écrire le shader, il faut choisir
**où se calcule l'éclairage** : dans la passe qui dessine chaque objet (forward), après une passe qui trie les
lumières par zone de l'écran (forward+), ou dans une passe à part, sur des images intermédiaires (deferred).

Ce choix décide de presque tout le reste de la phase : la place des ombres (M5.3) et de l'IBL (M5.4), la
transparence de l'eau (M5.7), l'anticrénelage de l'herbe (M5.7), le coût sur le web (ADR-0023).

Ce que demande *Rando* v1 ([JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md)) : **une seule
heure de la journée**, donc un soleil et une HDRI pour l'IBL ; pas de cycle jour/nuit (candidat v2) ; quelques
lumières ponctuelles au plus (le sanctuaire). De l'eau transparente et de l'herbe dense. Et, de Donnovan : des
techniques récentes, **activables selon la plateforme**, le web compris.

## Options envisagées

| Option | Pour | Contre |
|---|---|---|
| A. **Forward** : chaque pixel parcourt toutes les lumières | Une passe, un shader. Transparence et MSAA sans effort (l'eau, l'herbe en alpha-to-coverage). Le moins de bande passante : le meilleur choix sur mobile et sur le web. | Le coût croît avec le nombre de lumières × pixels : au-delà de quelques dizaines de lumières ponctuelles, il faut trier. |
| B. **Forward+** (lumières triées par tuiles ou par clusters, en compute) — **choisi** | Des centaines de lumières. Garde les avantages du forward (transparence, MSAA). C'est le rendu par défaut de Godot 4 et une option d'Unity. | Une passe compute de plus et des listes de lumières à tenir : une PR de plus en M5.1, pour des lumières que *Rando* v1 n'a pas encore. |
| C. **Deferred** (G-buffer, puis l'éclairage en plein écran) | Des milliers de lumières, l'éclairage découplé de la géométrie. Le choix d'Unreal. | La transparence demande quand même une passe forward (l'eau). Pas de MSAA. Un G-buffer de 4 à 5 cibles : beaucoup de bande passante, coûteux sur mobile et sur le web. |

## Décision

**Un rendu forward+ en clusters (B), dès M5.1**, par décision de Donnovan : le moteur doit tenir des scènes plus
riches que *Rando* v1, et la technique se pose une fois, au début de la phase, plutôt qu'en réécrivant le shader
plus tard.

- **Clusters plutôt que tuiles.** Le volume de la caméra se découpe en une grille 3D (par exemple 16 × 9 cases à
  l'écran et 24 tranches en profondeur, réparties de façon logarithmique). Un compute shader range chaque
  lumière ponctuelle dans les clusters qu'elle touche. Les tuiles, elles, s'appuient sur la profondeur de
  l'image ; un cluster ne dépend pas de ce qui est déjà dessiné : **la transparence** (l'eau de M5.7) s'éclaire
  par la même grille, et la pré-passe de profondeur reste facultative.
- **Le shader ne voit les lumières que par une fonction**, `lightsAffecting`, qui lit la liste du cluster du
  pixel. La lumière directionnelle (le soleil) est à part : elle touche tout l'écran.
- **Des limites nommées et bruyantes** (règle n°7) : un nombre maximal de lumières dans la scène et dans un
  cluster ; un dépassement se signale au lieu de couper en silence.
- **Réglable selon la plateforme** (ADR-0023) : la taille de la grille et les limites. Un repli sans tri (toutes
  les lumières pour tous les pixels) resterait possible là où le compute manquerait ; le web et le natif l'ont
  tous les deux, ce repli n'est donc pas prévu en v1.

## Conséquences

- **M5.1 grossit d'une PR** : la grille de clusters et son compute de tri, avant la BRDF. Estimation : +0,5 h
  pour Donnovan (1,75 h → 2,25 h).
- Une passe compute par image avant le rendu des objets : son temps GPU se mesure dès M5.1, et ses compteurs
  (lumières par cluster) rejoignent ceux de M5.5.
- Les ombres en cascades (M5.3) et l'IBL (M5.4) s'ajoutent au même shader : le soleil lit sa shadow map,
  l'éclairage ambiant lit les cartes d'IBL. Pas de passe d'éclairage séparée.
- L'eau et l'herbe (M5.7) se dessinent dans la même passe, avec le même éclairage, transparence et MSAA compris.
- Le même shader tourne sur Vulkan et sur WebGPU : le compute et les storage buffers lus dans le fragment shader
  font partie du socle de WebGPU (au plus 8 storage buffers par étape, `maxStorageBuffersPerShaderStage`).
- Pour *Rando* v1, qui a peu de lumières, le gain ne se verra pas : il se vérifiera sur une scène de test à
  beaucoup de lumières ponctuelles (Sponza avec une centaine de lumières).

## Ce que font les autres moteurs

- **Unreal** : deferred par défaut ; un « Forward Shading Renderer » optionnel, recommandé pour la VR (MSAA,
  coût par pixel plus faible) ; la transparence passe toujours en forward.
- **Unity** : l'URP propose Forward, Forward+ et Deferred, au choix par projet (Forward+ depuis Unity 2022.2) ;
  HDRP fait du deferred et du forward, avec un tri des lumières par tuiles et clusters.
- **Godot 4** : trois moteurs de rendu. Forward+ (clustered) sur PC, Mobile (forward, des listes de lumières par
  objet) et Compatibility (forward, OpenGL, le web).
- **Donut** (NVIDIA, la référence de NVRHI) : un `ForwardShadingPass` et une chaîne deferred (`GBufferFillPass`,
  `DeferredLightingPass`), côte à côte.

## Sources

- Unreal Engine, « Forward Shading Renderer » (documentation d'Unreal Engine 5).
- Unity, « Rendering paths in URP » (manuel de l'URP).
- Godot, « Overview of renderers » (documentation de Godot 4).
- NVIDIA, Donut : `include/donut/render/ForwardShadingPass.h`, `DeferredLightingPass.h`.
- O. Olsson, M. Billeter, U. Assarsson, « Clustered Deferred and Forward Shading », HPG 2012.
