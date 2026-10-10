# Spécifications — Levain

> Version 0.6 — 08/10/2026 — statut : **validé par Donnovan** (ADR-0035 accepté)
> Documents liés : [ROADMAP](ROADMAP.md) · [JOURNAL](JOURNAL.md) · [ADR](adr/) · [Études](etudes/) ·
> [Lectures](LECTURES.md) · [Q&R](QA.md)
>
> v0.2 : couche graphique NVRHI (ADR-0002) et ECS flecs (ADR-0004) à la place d'une RHI et d'un ECS maison.
> v0.3 : C++23 au lieu de C++20 (ADR-0001 amendé), machine de référence renseignée, licence MIT, moteur nommé
> **Levain**.
> v0.4 : passage à Rust (ADR-0010), annulé le jour même.
> v0.5 : **retour au C++23** (ADR-0011). Périmètre **Linux d'abord** ; Windows et Direct3D 12 différés. La
> forme du code est fixée : fonctions libres, dépendances dans la signature, pièges nommés.
> v0.6 : **Windows revient** (ADR-0035), compilé depuis Linux par clang-cl : Vulkan d'abord, puis Direct3D 12.

## 1. Vision

**Faire un jeu avec un moteur 3D construit ensemble.** Le moteur est écrit en C++23, sur NVRHI (Vulkan et
Direct3D 12) et flecs, et grandit par étapes mesurables.

La compréhension reste un objectif : chaque système est accompagné d'une note qui explique ce qu'il fait, pourquoi
il est conçu ainsi, et comment Unreal, Unity et Godot résolvent le même problème (et REEngine, Anvil ou Frostbite
quand des sources publiques existent : conférences GDC/CEDEC, blogs techniques). Quand on s'appuie sur une
bibliothèque, on la comprend **par la lecture** (études, lectures commentées) plutôt qu'en la réécrivant.

## 2. Rôles

| Qui | Rôle |
|---|---|
| **Donnovan** | Décideur et relecteur. Valide les specs et les ADR, relit le code, pose les questions, arbitre les compromis, choisit le jeu. Budget : **1 à 2 h par semaine**. |
| **Claude (Claude Code sur le PC de Donnovan)** | Concepteur-développeur. Écrit le code, les tests, la CI, la documentation et les études ; tient le board et le journal à jour. |

Le temps de Donnovan est la ressource rare du projet. Trois règles en découlent :

1. **Le code est écrit pour être lu.** Lisibilité avant astuce. Chaque module a un `README.md` (rôle, invariants,
   points d'entrée). Chaque PR contient un *guide de lecture* qui dit quoi lire, dans quel ordre, et pourquoi.
2. **Une PR se relit en 30 minutes au plus** (environ 400 lignes hors code tiers ou généré). Au-delà, on découpe.
3. **Une seule PR ouverte à la fois.** Claude n'attaque pas l'étape suivante tant que la précédente n'a pas été
   relue. Sans cette règle, le code avance plus vite que la compréhension.

## 3. Principes directeurs

1. **Ne pas recréer l'existant.** Quand une bibliothèque open-source reconnue couvre un domaine, on l'utilise :
   couche graphique (NVRHI), modèle objet (flecs), physique (Jolt), fenêtre et input (SDL3), UI d'outils (ImGui),
   audio (miniaudio), import glTF, profiling. On écrit ce qui fait **notre** moteur : l'assemblage et la boucle,
   l'initialisation des devices, le renderer et ses passes, le pipeline d'assets, l'input par actions, l'éditeur,
   et le jeu.
2. **Toujours un exécutable qui tourne.** Chaque milestone se termine par une démo lançable sur Windows et Linux.
3. **Mesurer.** Chaque milestone a des critères chiffrés (frame time, nombre d'entités, temps de chargement…),
   mesurés par une commande reproductible et consignés dans le journal.
4. **Tracer les décisions.** Toute décision structurante fait l'objet d'un ADR (`docs/adr/`) qui liste les
   alternatives écartées et pourquoi.
5. **Comparer.** Chaque phase produit une étude courte (`docs/etudes/`) : « comment les autres font ».
6. **Planifier par vagues.** Seules les deux prochaines phases sont découpées en issues détaillées ; les suivantes
   le sont à l'approche, avec ce qu'on aura appris.

## 4. Périmètre

### Dans le périmètre v1 (fin de la roadmap actuelle)

- **Plateformes** : Windows 10 (1903 et plus) et 11, et Linux, x86-64, compilés par Clang : clang-cl pour
  Windows, depuis Linux ([ADR-0035](adr/0035-windows-compile-depuis-linux.md)).
- **Rendu** : NVRHI avec backends Vulkan (Windows et Linux) et Direct3D 12 (Windows) ; rendu forward PBR, ombres
  en cascades, HDR et tonemapping, éclairage d'environnement (IBL), frustum culling, hot-reload des shaders.
- **Cœur** : boucle à pas fixe pour la simulation, ECS flecs, hiérarchie de transforms, input par actions,
  logs, assertions, allocateurs, profiling.
- **Assets** : import glTF 2.0, base d'assets à identifiants stables (GUID), cuisson (cooking) vers un format
  binaire, textures compressées KTX2, hot-reload.
- **Physique** : corps rigides, colliders, requêtes (raycasts), character controller.
- **Audio** : sons 2D et 3D.
- **Outils** : éditeur (hiérarchie, inspecteur, gizmos, picking), sérialisation de scènes, undo/redo, mode Play.
- **Monde** (pour le jeu) : animation squelettique, terrain, eau, herbe dense.
- **Le jeu** : un petit jeu 3D, choisi en M3.5 ([JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md)), dans son propre dépôt, construit uniquement
  avec le moteur et son éditeur.

### Hors périmètre v1 (candidats pour la v2)

Render graph, GPU-driven rendering, ray tracing, scripting, réseau, streaming de monde,
consoles, mobile, macOS (NVRHI n'a pas de backend Metal), VR.

## 5. Contraintes

- **Coût zéro** : outils et bibliothèques gratuits, sous licence permissive (MIT, BSD, zlib, Apache 2.0, Boost).
  Une police est une donnée, pas du code : la SIL Open Font License 1.1 (OFL) est admise pour les polices, et pour
  elles seules, car elle permet de les livrer avec le jeu, leur licence jointe, sans les vendre seules ; une police
  modifiée (un sous-ensemble) reste sous OFL et perd son nom réservé, s'il y en a un
  ([ADR-0036](adr/0036-mode-edition-vue-et-camera-de-l-editeur.md)).
- **Build reproductible** : un preset CMake et une commande de build, sans étape manuelle hormis l'installation
  des outils listés dans `docs/SETUP.md`.
- **CI verte obligatoire** avant toute fusion dans `main` : Linux, et Windows dès que Donnovan ajoute ses jobs
  aux checks requis (ADR-0035).
- **Zéro erreur de validation** en Debug : couche de validation NVRHI, validation layers Vulkan et couche de debug
  D3D12. Une erreur de validation est un bug bloquant.
- **Pas de code tiers copié dans le dépôt** : les dépendances passent par vcpkg, ou par `FetchContent` avec un
  commit figé quand il n'existe pas de port.

## 6. Stack technique

| Domaine | Choix | Justification |
|---|---|---|
| Langage | C++23, sans modules | [ADR-0001](adr/0001-langage-cpp23.md) |
| Couche graphique | NVRHI : Vulkan (Windows, Linux) et Direct3D 12 (Windows) | [ADR-0002](adr/0002-nvrhi.md) |
| Fenêtre, input, surface | SDL3 | [ADR-0003](adr/0003-plateforme-sdl3.md) |
| Modèle objet | flecs (ECS à archetypes) | [ADR-0004](adr/0004-ecs-flecs.md) |
| Shaders | Slang → SPIR-V et DXIL | [ADR-0005](adr/0005-shaders-slang.md) |
| Hébergement et suivi | GitHub (dépôt public) | [ADR-0006](adr/0006-hebergement-github.md) |
| Build et dépendances | CMake (presets) + Ninja + vcpkg (manifeste) | [ADR-0007](adr/0007-build-cmake-vcpkg.md) |
| Référence d'intégration NVRHI | Donut et Donut-Samples (NVIDIA, MIT) | Lus et adaptés, pas utilisés comme dépendance (ADR-0002) |
| Initialisation Vulkan | vk-bootstrap | [ADR-0012](adr/0012-vk-bootstrap.md) |
| Maths | GLM | Conventions proches de celles des shaders |
| Import glTF | fastgltf | Rapide, C++ moderne |
| Images | stb_image, puis libktx (KTX2) | Simple d'abord, format GPU compressé ensuite |
| Simplification de maillages | meshoptimizer | La collision du décor, simplifiée à la cuisson ([ADR-0028](adr/0028-personnage.md)) |
| Physique | Jolt Physics | Utilisé par Horizon Forbidden West et Death Stranding 2, intégré à Godot 4.4 |
| Audio | miniaudio | Multiplateforme, spatialisation incluse |
| UI de l'éditeur | Dear ImGui (branche docking) + ImGuizmo | Standard des outils internes ; Donut fournit un renderer ImGui pour NVRHI |
| Profiling | Tracy | CPU et GPU, standard de l'industrie |
| Logs | spdlog | — |
| Tests | doctest | Léger, rapide à compiler |
| Sérialisation | JSON de flecs (addon meta) pour les valeurs ; l'enveloppe et le chargeur des scènes à nous ([ADR-0034](adr/0034-reflexion-des-composants.md)) | Réflexion et JSON intégrés ; format binaire des assets en phase 4 |

## 7. Architecture cible

```
engine/
├── core/       types de base, logs, asserts, allocateurs, temps, fichiers
├── platform/   SDL3 : fenêtre, événements, input brut
├── gpu/        DeviceManager par backend (Vulkan, D3D12), swapchain, cadence des frames ; expose nvrhi::IDevice
├── render/     renderer sur NVRHI : caméras, matériaux, passes, éclairage, ombres
├── scene/      monde flecs, composants de base, transforms, hiérarchie, modules flecs
├── assets/     import, base d'assets (GUID), cuisson, cache, hot-reload
├── animation/  squelettes, clips, poses et fondus, sur ozz-animation (ADR-0022)
├── physics/    intégration Jolt
├── audio/      intégration miniaudio
├── input/      actions et axes au-dessus de platform
├── ui/         Dear ImGui : son contexte, son rendu par NVRHI, l'input venu de platform (ADR-0032)
└── app/        boucle principale, cycle de vie, modèles sur le GPU (ADR-0029)
editor/         bibliothèque de l'éditeur ; le jeu et le sandbox en construisent l'exécutable (ADR-0018, ADR-0034)
plugins/        plugins moteur : terrain, eau, végétation, marche du personnage (ADR-0018, ADR-0028)
sandbox/        une démo par milestone
shaders/        sources Slang
tests/          tests unitaires et benchmarks
tools/          scripts (bootstrap GitHub, mesures)
docs/           SPECS, ROADMAP, JOURNAL, LECTURES, QA, SETUP, adr/, etudes/
```

**Dépendances entre modules** (du bas vers le haut, jamais l'inverse) :

```
core ← platform ← gpu ← render
core ← scene (flecs) ← assets, physics, audio, input
assets ← animation
platform, gpu, render ← ui (ADR-0032)
tout ce qui précède ← app ← editor, sandbox, jeu (ADR-0029)
editor ← exécutables éditeur du sandbox et du jeu (ADR-0034)
moteur ← plugins moteur ← jeu (autre dépôt) et ses plugins gameplay
```

**Le jeu vit dans son propre dépôt**, `Rando`, qui récupère le moteur par `FetchContent`. Le moteur ne dépend
jamais d'un plugin ; un plugin peut dépendre d'un autre s'il le déclare
([ADR-0018](adr/0018-moteur-plugins-et-jeu.md)).

Visibilité des bibliothèques :

- SDL3 : uniquement dans `platform/` (et `gpu/` pour la création de surface).
- NVRHI : dans `gpu/`, `render/`, `ui/` et au-dessus (`app`, les plugins de rendu, le sandbox).
- Dear ImGui : dans `ui/` et au-dessus (`app`, `editor`, le sandbox, le jeu) ; jamais dessous (contrôlé par
  `deps.imgui-visibility`, ADR-0032).
- flecs : c'est l'API du modèle objet, visible dans `scene/` et tout ce qui est au-dessus ; jamais dans `core/`,
  `platform/` ni `gpu/`.
- Jolt : uniquement dans `physics/`.
- fastgltf, libktx et meshoptimizer : uniquement dans `assets/src/`, et fastgltf aussi dans `animation/src/` pour la passerelle
  glTF (contrôlé par `deps.asset-libraries-visibility`).
- ozz-animation : uniquement dans `animation/src/` (ADR-0022).

**Boucle principale (cible)** : simulation à pas fixe (60 Hz par défaut) avec accumulateur, exécutée par un
pipeline flecs dédié ; rendu à fréquence libre avec interpolation. Détails dans un ADR en M3.3.

## 8. Conventions

- **Langue** : identifiants, commits, logs et messages d'erreur en anglais (norme de l'industrie) ;
  documentation, ADR, études, journal, Q&R **et commentaires de code** en français. Les commentaires sont écrits
  pour Donnovan, seul relecteur, qui lit le français plus vite (ADR-0009). **Autre exception assumée** : le
  moteur s'appelle `Levain`, nom propre français, donc le namespace racine est `levain` et les cibles CMake sont
  préfixées `levain_`. Comme Godot, le nom ne se traduit pas.
- **Style C++** : variante mixte (types `PascalCase`, fonctions et variables `camelCase`, membres `m_`),
  accolades Allman, 4 espaces, 100 colonnes, décoration modérée. Détails et justifications dans
  [ADR-0009](adr/0009-style-cpp.md) ; appliqué mécaniquement par `.clang-format` et, pour le nommage, par
  clang-tidy.
- **Forme du code** ([ADR-0011](adr/0011-retour-au-cpp.md)) : la logique s'écrit en **fonctions libres dont
  toutes les dépendances sont des paramètres** — pas de singleton récupéré dans le corps, pas d'état caché. La
  **glu ECS tient en une ligne**, à l'écart du fichier de logique. **Chaque piège porte son nom** :
  `normalizeOrZero`, `clampPitch`, `horizontalBasisFrom` plutôt que le calcul brut. Ce sont ces trois règles,
  et non le nommage, qui décident de la fatigue de relecture.
- **Plateforme** : **Linux et Clang d'abord**. Windows revient avec le PC Windows de Donnovan
  ([ADR-0035](adr/0035-windows-compile-depuis-linux.md)) : compilé depuis Linux par clang-cl, le même LLVM. On
  développe sous Linux ; la CI compile sous Linux et lance le binaire Windows sur un runner Windows.
- **Style** : clang-format et clang-tidy versionnés dans le dépôt et vérifiés en CI.
- **Commits** : [Conventional Commits](https://www.conventionalcommits.org/fr/) (`feat(render): …`, `fix(gpu): …`,
  `docs(adr): …`).
- **Branches** : `main` protégée ; une branche par fonctionnalité (`m1.2/device-vulkan`), ses morceaux en PR vers elle
  sans CI, puis une PR vers `main` avec CI verte (AGENTS.md, règle n°1).
- **Licence** : MIT (fichier `LICENSE`), compatible avec le code adapté de Donut, qui garde son propre
  en-tête MIT.
- **Versions** : un tag par milestone terminé (`m1.3`), avec une GitHub Release qui contient les binaires de la
  démo produits par la CI et les mesures.

## 9. Définition de « terminé » pour un milestone

1. Démo `sandbox/` lançable sur Windows et Linux.
2. Critères chiffrés atteints, mesurés sur la machine de référence et consignés dans `docs/JOURNAL.md`.
3. CI verte, zéro erreur de validation.
4. `README.md` à jour dans chaque module touché.
5. Étude comparative écrite si la phase en prévoit une.
6. Temps estimé et temps passé renseignés sur le board GitHub.
7. Tag et GitHub Release publiés.

## 10. Machine de référence

Toutes les mesures de performance sont faites sur la même machine, pour que les chiffres soient comparables
d'un milestone à l'autre.

| Élément | Valeur |
|---|---|
| OS et version | Bazzite 44 (Fedora Kinoite, immuable), noyau 7.2.7-ogc1.1.fc44 ; les builds et les mesures dans une distrobox Ubuntu 26.04 (`dev-ubuntu`), la même version d'Ubuntu que la CI |
| CPU | AMD Ryzen 7 7800X3D (8 cœurs / 16 threads) |
| GPU | AMD Radeon RX 9070 XT (RDNA 4, GFX1201, `0x1002:0x7550`) |
| Pilote Vulkan | Mesa RADV 26.0.8-1ubuntu0.3 (dans la distrobox), Vulkan 1.4.335 sur le GPU |
| Compilateur | Clang 23.1.3, CMake 4.2.3 (dans la distrobox) ; LLVM 23 aussi en CI, sur `ubuntu-26.04` |
| RAM | 16 Go (15,5 Go vus par le noyau) |
| Résolution de mesure | 1920×1080 |

Relevé le 20/09/2026 avec `vulkaninfo --summary`, `uname -r`, `/proc/cpuinfo` et `/proc/meminfo` ; mis à jour le
05/10/2026 après le passage de la machine sous Bazzite (`uname -r`, `/etc/os-release`, la ligne `[gpu]` du journal
du moteur, `clang++ --version`). Le matériel n'a pas changé.
Mis à jour le 07/10/2026 au passage de la distrobox Arch à Ubuntu 26.04 (`vulkaninfo --summary`,
`clang++ --version`, `cmake --version`). Les mesures jusqu'à M7.1 compris ont été prises dans la boîte Arch,
avec Mesa 26.2.4 et Clang 23.1.1 : un écart de performance GPU entre les deux périodes peut venir du pilote,
pas du moteur.

Outils GPU sur cette machine : RenderDoc (captures), Tracy (profiling CPU et GPU), Radeon GPU Profiler (outil
du constructeur).

Deux points relevés par `vulkaninfo --summary`, à traiter en M1.2 :

1. **Deux GPU Vulkan** : le RX 9070 XT (`PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`) et l'iGPU du 7800X3D
   (`RAPHAEL_MENDOCINO`, intégré). Le choix du device ne peut donc pas être « le premier de la liste » : il faut
   préférer le discret, avec une option de forçage.
2. **Couches Vulkan implicites installées par des outils tiers** (MangoHud, Steam overlay et fossilize, gamescope
   WSI, MAKO, Lossless Scaling). Celle de Lossless Scaling est cassée sur cette machine et le loader affiche une
   erreur à chaque `vkCreateInstance`. Ces messages ne viennent pas de notre code : les mesures et les sessions de
   debug se font avec `VK_LOADER_LAYERS_DISABLE=*` (hors validation layers, activées explicitement par le moteur).

### Vérification sous Windows

La machine de référence est sous Linux. Le code Windows est vérifié à trois niveaux :

1. **CI (à la PR vers `main`, une fois par fonctionnalité)** : le build Windows, Debug et Release, compilé sur un runner
   Linux par clang-cl, puis lancé sur un runner Windows : tests unitaires, tests GPU en Vulkan sur lavapipe, et test de
   fumée D3D12 sous WARP (rendu logiciel) dès que le backend existe
   ([ADR-0035](adr/0035-windows-compile-depuis-linux.md)).
2. **Proton sur la machine de référence (à chaque milestone de rendu)** : le binaire Windows produit par la CI est
   lancé sous Proton. Direct3D 12 y est traduit en Vulkan par vkd3d-proton : ça vérifie notre code Windows et le
   backend D3D12 de NVRHI sur le vrai GPU, mais pas un pilote D3D12 natif, et la couche de debug D3D12 de
   Microsoft n'y est pas disponible (seule la validation NVRHI s'applique).
3. **Le PC Windows de Donnovan** (RTX 4070 Laptop, depuis le 2026-10-08) : le binaire compilé dans sa distro WSL
   (`tools/wsl/`) se lance sous Windows depuis le terminal de la distro, sur le vrai pilote, Vulkan comme D3D12,
   avec la couche de debug D3D12. Ce n'est pas une machine de référence : aucune mesure de performance n'en vient.

Une machine virtuelle Windows n'apporterait rien de plus : sans passthrough du GPU, elle n'a que du rendu
logiciel, comme WARP en CI.

## 11. Questions ouvertes

- Le jeu : choisi en M3.5, voir [JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md).
