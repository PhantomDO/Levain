# ADR-0033 — La cible processeur des dépendances : x86-64, écrite dans le triplet

- **Statut** : accepté le 2026-10-07, sur la réponse de Donnovan au sondage du jour ; relu par la session
  Levain, qui a trouvé la panne sur la CI de #322
- **Date** : 2026-10-07
- **Milestone** : M7.1 (outillage, au passage à Ubuntu 26.04)

## Contexte

Le développement est passé de la distrobox Arch à une distrobox Ubuntu 26.04, et la CI à `ubuntu-26.04`
(#322). Sur ce runner, la première CI a échoué dans vcpkg, sur ktx : `basisu_kernels_sse.cpp` s'arrête sur
`#error Please check your compiler options`. Ce fichier compile les noyaux SSE de basisu avec `-msse4.1` et
refuse de le faire si `__AVX__` est défini.

La cause : l'image du runner active la variante **amd64v3** des paquets d'Ubuntu, et le GCC de cette variante
vise `-march=x86-64-v3` par défaut (AVX, AVX2). Reproduit dans un conteneur `ubuntu:26.04` : le même
`g++ 15.2.0-16ubuntu1` donne `-march=x86-64` sans la variante et `-march=x86-64-v3` avec ; le fichier de
basisu y échoue avec `-msse4.1` seul et compile avec `-march=x86-64 -msse4.1`. La distrobox de référence, sans
la variante, vise `x86-64`, comme la boîte Arch et les runners 24.04 avant elle.

Ni le dépôt ni vcpkg n'écrivaient de cible : elle dépendait du compilateur de la machine.

Cette cible n'est que la base des ports qui ne choisissent pas la leur. Jolt choisit : son port garde
`USE_AVX2`, `USE_F16C` et `USE_FMADD`, actifs par défaut, et exporte `-mavx2 -mbmi -mpopcnt -mlzcnt -mf16c
-mfma` dans ses `INTERFACE_COMPILE_OPTIONS`. Les sources de `levain_physics` compilent donc en AVX2 et FMA :
tout binaire qui lie la physique demande déjà un processeur de niveau `x86-64-v3`, avant comme après cet ADR.

## Options envisagées

| Option | Pour | Contre |
|---|---|---|
| **A. `-march=x86-64` dans le triplet x64-linux** | Ce que tous les builds faisaient déjà pour les ports : rien ne change, sinon que c'est écrit | Ne profite pas des instructions récentes (sauf Jolt, qui choisit les siennes) |
| B. `-march=x86-64-v2` (SSE4.2, POPCNT) | Un peu plus de performance possible ; ce que la doc de Godot 4.5 exige | Un vrai changement de cible, sans mesure qui le demande |
| C. Revenir à `ubuntu-24.04` en CI | Aucun triplet | La CI ne serait plus sur la version d'Ubuntu de la machine de référence |
| D. Un port overlay de ktx, `-mno-avx` sur le fichier de basisu | Corrige ktx seul | La CI garderait en silence une autre cible que la machine de référence pour tous les autres ports |
| E. Les ports compilés par clang (`CC`, `CXX`) | Le même compilateur que le moteur | Change le compilateur de tous les ports, pour régler un défaut de cible |

`x86-64-v3` est exclu : basisu refuse précisément de compiler ses noyaux SSE sous AVX.

## Décision

**A** : `triplets/x64-linux.cmake` reprend le triplet de vcpkg et pose `VCPKG_C_FLAGS` et `VCPKG_CXX_FLAGS` à
`-march=x86-64`. *Rando* reçoit le même triplet. La réponse de Donnovan, mot pour mot : « x86-64 (Recommended) ».
Le sondage présentait A par « le jeu tourne sur tout PC 64 bits » : c'est faux à cause de Jolt (Contexte), ce
qui a été corrigé auprès de Donnovan après le vote. A reste le statu quo pour les ports, ce qu'il a choisi.

## Conséquences

- Les dépendances compilent pareil sur la machine de référence et en CI, quelle que soit la variante d'Ubuntu.
- Le triplet change l'ABI de chaque paquet : tout vcpkg se recompile une fois, en local comme en CI. Les clés
  du cache vcpkg des jobs natifs comprennent maintenant `triplets/**`, sans quoi un triplet modifié serait
  restauré sous l'ancienne clé, recompilé à chaque exécution et jamais réécrit.
- Le plancher réel du jeu reste celui de Jolt : `x86-64-v3` (AVX2, FMA, F16C, BMI, LZCNT) pour tout binaire
  qui lie `levain_physics`. Le reste du moteur, compilé par le clang d'apt.llvm.org, vise `x86-64` par défaut.
  Abaisser ce plancher, c'est couper les options `USE_*` de Jolt : un autre ADR.
- Monter la base des ports (v2, v3) reste possible : ce sera un nouvel ADR, avec une mesure qui le justifie,
  et pour v3 une parade pour basisu.

## Ce que font les autres moteurs

- **Godot** : la documentation de la 4.5 exige SSE4.2 pour les binaires x86_64, soit l'équivalent de
  `x86-64-v2` ; celle de la 4.2 acceptait n'importe quel processeur x86_64
  ([4.5](https://docs.godotengine.org/en/4.5/about/system_requirements.html),
  [4.2](https://docs.godotengine.org/en/4.2/about/system_requirements.html)).
- **Unity 6** : le lecteur demande un processeur x64 avec SSE2, la cible de base
  ([configuration requise](https://docs.unity3d.com/6000.0/Documentation/Manual/system-requirements.html)).
- **Unreal** : pas de source publique fiable trouvée sur sa cible par défaut sous Linux.

## Sources

- runner-images, `install-git.sh` : l'index amd64v3 sur l'image Ubuntu 26.04
  (<https://github.com/actions/runner-images/blob/main/images/ubuntu/scripts/build/install-git.sh>).
- Le journal de la CI de #322 (run 37620665079, job `linux-release`) : la ligne de compilation de basisu, sans
  `-march`, et l'erreur.
