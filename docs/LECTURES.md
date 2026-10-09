# Lectures commentées

Des sources choisies pour comprendre ce que le moteur utilise et comment font les autres. Pour chacune : ce qu'on
y apprend, le temps de lecture (approximatif) et le bon moment pour la lire. Toutes sont gratuites.

**Si tu n'en lis que trois** : le *Programming Guide* de NVRHI (A2), le *Quickstart* de flecs (D1), et
*Archetypes and Vectorization* de Sander Mertens (D8).

## A. NVRHI et son écosystème (phase 1)

| # | Source | Durée | Ce qu'on y apprend | Quand |
|---|---|---|---|---|
| A1 | [NVRHI — README](https://github.com/NVIDIA-RTX/NVRHI) | 10 min | Ce que fait la bibliothèque, ses backends (D3D11, D3D12, Vulkan 1.3), son intégration CMake | Avant M1.2 |
| A2 | [NVRHI — Programming Guide](https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md) | 45 min | **Le modèle complet** : device, ressources, binding layouts et sets, command lists, suivi d'état, durée de vie, validation. La lecture la plus importante de la phase 1 | Avant M1.2 |
| A3 | [NVRHI — Tutorial](https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/Tutorial.md) | 30 min | Les étapes pour créer un device et dessiner : suit presque exactement M1.2 et M1.3 | Pendant M1.2–M1.3 |
| A4 | [Donut](https://github.com/NVIDIA-RTX/Donut) | Référence | Le `DeviceManager` par API (notre modèle pour M1.2 et M1.4) et des passes de rendu complètes (forward, deferred, ombres, tonemapping) | M1.2, phase 5 |
| A5 | [Donut-Samples](https://github.com/NVIDIA-RTX/Donut-Samples) | Référence | Exemples ciblés : *Basic Triangle* (M1.3), *Vertex Buffer* (M2.1), *Bindless Rendering* (ADR-0009), *Headless Device* (tests en CI), *Deferred Shading* (étude E5), *Threaded Rendering* (v2) | Au fil des milestones |
| A6 | [ShaderMake](https://github.com/NVIDIA-RTX/ShaderMake) | 10 min | Comment compiler une source de shader vers SPIR-V et DXIL avec FXC, DXC ou Slang | M1.3 |
| A7 | [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG) | À parcourir | Un vrai jeu (Doom 3 BFG) dont le renderer a été porté sur NVRHI, en DX12 et Vulkan : NVRHI en conditions réelles | Facultatif |

## B. Comprendre ce que NVRHI fait à ta place (facultatif)

| # | Source | Durée | Ce qu'on y apprend | Quand |
|---|---|---|---|---|
| B1 | Arseny Kapoulkine, [*Writing an efficient Vulkan renderer*](https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/) — [traduction française](https://www.fevrierdorian.com/carnet/pages/ecrire-un-moteur-de-rendu-vulkan-performant.html) | 1 h | Mémoire, descripteurs, command buffers, pipelines, synchronisation : exactement ce que NVRHI cache, expliqué par un spécialiste. Existe en français | Phase 1 ou 2 |
| B2 | Alain Galvan, [*A Comparison of Modern Graphics APIs*](https://alain.xyz/blog/comparison-of-modern-graphics-apis) | 30 min | Tables de correspondance entre Vulkan, Direct3D 12, Metal, WebGPU et OpenGL : pourquoi une RHI est nécessaire | Phase 1 |
| B3 | Themaister, [*Yet another blog explaining Vulkan synchronization*](https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/) | 40 min | Pourquoi les barrières sont si difficiles ; fait apprécier le suivi d'état automatique de NVRHI. Avancé | Facultatif |
| B4 | Sebastian Aaltonen, [*No Graphics API*](https://www.sebastianaaltonen.com/blog/no-graphics-api) | 40 min | Une critique de la complexité des API graphiques (explosion des PSO) et une proposition beaucoup plus simple : où va le domaine | Facultatif |

## C. Les RHI des grands moteurs (étude E1)

| # | Source | Durée | Ce qu'on y apprend | Quand |
|---|---|---|---|---|
| C1 | Epic, [*Parallel Rendering Overview*](https://dev.epicgames.com/documentation/unreal-engine/parallel-rendering-overview-for-unreal-engine) | 15 min | Game thread, render thread, RHI thread ; comment `FRHICommandList` est traduite vers l'API réelle | Phase 1 |
| C2 | Godot, [*Internal rendering architecture*](https://docs.godotengine.org/en/4.6/engine_details/architecture/internal_rendering_architecture.html) | 40 min | La meilleure documentation publique d'une RHI de production : `RenderingDevice`, ses drivers, les trois renderers | Phase 1 |
| C3 | Godot, [*GPU synchronization in Godot 4.3 is getting a major upgrade*](https://godotengine.org/article/rendering-acyclic-graph/) | 15 min | Comment Godot place les barrières automatiquement grâce à un graphe de commandes | Phase 1 ou 2 |
| C4 | Unity, [*Scriptable Render Pipeline fundamentals*](https://docs.unity3d.com/6000.5/Documentation/Manual/scriptable-render-pipeline-introduction.html) | 10 min | Ce que Unity expose en C# au-dessus de sa couche graphique interne | Phase 1 |
| C5 | Alain Galvan, [*Unreal Engine Architecture*](https://alain.xyz/blog/unreal-engine-architecture) | 30 min | Vue d'ensemble des modules d'Unreal, RHI comprise | Facultatif |
| C6 | Yuriy O'Donnell, [*FrameGraph* (Frostbite, GDC 2017)](https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in) | 1 h (vidéo) | Le graphe de rendu (niveau 3) : l'étape au-dessus de NVRHI, candidat pour la v2 | Phase 5 ou v2 |

## C bis. Shaders, variantes et pipelines (étude E2)

| # | Source | Durée | Ce qu'on y apprend | Quand |
|---|---|---|---|---|
| C7 | Godot, [*Reducing stutter from shader (pipeline) compilations*](https://docs.godotengine.org/en/stable/tutorials/performance/pipeline_compilations.html) | 15 min | **La meilleure explication publique des saccades de pipelines**, et des ubershaders de Godot 4.4 qui les évitent | Phase 2 ou 5 |
| C8 | Epic, [*PSO Precaching for Unreal Engine*](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine) | 20 min | Comment Unreal précompile les pipelines au chargement des assets, en tâche de fond | Phase 5 |
| C9 | Unity, [*Strip shader variants*](https://docs.unity3d.com/6000.1/Documentation/Manual/shader-variant-stripping.html) | 10 min | Les variantes par mots-clés (`multi_compile`, `shader_feature`) et ce qu'elles coûtent | Avant l'ADR sur les variantes |

## D. flecs et l'ECS (phase 3)

| # | Source | Durée | Ce qu'on y apprend | Quand |
|---|---|---|---|---|
| D1 | [flecs — Quickstart](https://www.flecs.dev/flecs/Quickstart.html) | 30 min | Entités, composants, systèmes, requêtes : tout ce qu'il faut pour M3.1 | Avant M3.1 |
| D2 | [flecs — Queries](https://www.flecs.dev/flecs/Queries.html) | 40 min | Le langage de requêtes, cœur de flecs | M3.1–M3.2 |
| D3 | [flecs — Systems](https://www.flecs.dev/flecs/Systems.html) | 30 min | Phases, pipelines, intervalles, tick sources : la base de la boucle à pas fixe | M3.3 |
| D4 | [flecs — Hierarchies](https://www.flecs.dev/flecs/HierarchiesManual.html) | 20 min | Les deux stockages de hiérarchie (`ChildOf` et `Parent`) et ce que chacun coûte ; section « Hierarchy storage » pour l'ADR-0015 | M3.2 |
| D5 | [flecs — Remote API et explorer](https://www.flecs.dev/flecs/FlecsRemoteApi.html) ([explorer](https://flecs.dev/explorer)) | 10 min | Voir et modifier le monde en direct depuis le navigateur | M3.1 |
| D6 | [flecs — Relationships](https://www.flecs.dev/flecs/Relationships.html) | 40 min | Les relations entre entités, point fort de flecs, utiles pour le gameplay | Phase 8 |
| D7 | [flecs — Documentation](https://www.flecs.dev/flecs/Docs.html) | Référence | Index de tous les manuels, dont la réflexion (meta) et le JSON pour l'éditeur | Phase 7 |
| D8 | Sander Mertens, *Building an ECS* : [#1 Where are my entities and components](https://ajmmertens.medium.com/building-an-ecs-1-where-are-my-entities-and-components-63d07c7da742), [#2 Archetypes and Vectorization](https://ajmmertens.medium.com/building-an-ecs-2-archetypes-and-vectorization-fe21690805f9), [#3 Storage in pictures](https://ajmmertens.medium.com/building-an-ecs-storage-in-pictures-642b8bfd6e04), [Data Oriented Hierarchies](https://ajmmertens.medium.com/building-an-ecs-data-oriented-hierarchies-62fb2847d100) | 1 h 15 | Comment flecs fonctionne à l'intérieur, par son auteur ; plus efficace que lire son code C | Phase 3, étude E3 |
| D9 | [ECS FAQ](https://github.com/SanderMertens/ecs-faq) | 30 min | Vocabulaire, familles d'ECS, liste des implémentations | Phase 3 |
| D10 | Michele Caini (auteur d'EnTT), [*ECS back and forth*](https://skypjack.github.io/2019-02-14-ecs-baf-part-1/) | 1 h (série) | L'autre grande famille : les sparse sets. Indispensable pour l'étude E3 | Étude E3 |
| D12 | Glenn Fiedler, [*Fix Your Timestep!*](https://gafferongames.com/post/fix_your_timestep/) | 20 min | Le patron de la boucle à pas fixe : accumulateur, plafond de rattrapage, interpolation du rendu ([ADR-0016](adr/0016-boucle-a-pas-fixe.md)) | Avant M3.3 |
| D13 | Juan Linietsky, [*Why isn't Godot an ECS-based game engine?*](https://godotengine.org/article/why-isnt-godot-ecs-based-game-engine/) | 20 min | La défense d'un modèle objet (nœuds, héritage, composition par enfants) par l'auteur de Godot, et où il place l'orienté données : dans les serveurs ([E3](etudes/E3-modeles-objets.md)) | Étude E3 |
| D11 | Mike Acton, [*Data-Oriented Design and C++* (CppCon 2014)](https://www.youtube.com/watch?v=rX0ItVEVjHc) | 1 h 30 (vidéo) | La conférence fondatrice de la pensée orientée données, à l'origine de l'ECS moderne | Facultatif |

## W. Windows : ce que devient un programme qui plante (ADR-0035)

| # | Source | Durée | Ce qu'on y apprend | Quand |
|---|---|---|---|---|
| W1 | Microsoft, [*Using WER*](https://learn.microsoft.com/windows/win32/wer/using-wer) | 15 min | Ce que Windows Error Reporting fait d'une exception non gérée, quand il affiche une interface (un processus interactif), et pourquoi une application ne doit pas gérer les exceptions fatales : la raison de laisser WER avoir le dernier mot en Release ([QA](QA.md#que-fait-un-moteur-quand-le-jeu-plante-sous-windows-en-release--2026-10-09-m14)) | Avant de toucher à `crt_report.cpp` |
| W2 | Microsoft, [*Games for Windows : Technical Requirements*](https://learn.microsoft.com/windows/win32/dxtecharts/games-for-windows-technical-requirements-1-1-0006), § 4.3 | 10 min | Ce que Microsoft demandait aux jeux : un gestionnaire maison doit transmettre l'erreur à WER, qui rapporte le plantage au développeur. Un cahier des charges ancien (Windows XP à 8), mais la seule source qui parle des jeux | Facultatif |
| W3 | Unity, [*Windows forensic debugging*](https://docs.unity3d.com/Manual/WindowsDebugging-forensic.html) | 10 min | Comment un moteur du marché récupère le plantage d'un joueur : les `LocalDumps` de WER, un minidump par plantage dans un dossier, sans code dans le jeu | Avant de décider du rapport de plantage du jeu distribué |
| W4 | Godot, [`crash_handler_windows_seh.cpp`](https://github.com/godotengine/godot/blob/65e8d16951d6963cb3984c090e45f40d1ba5f704/platform/windows/crash_handler_windows_seh.cpp) | 15 min | Un gestionnaire de plantage de 279 lignes qui imprime la pile puis « passe l'exception à l'OS » (lignes 126-128 et 258-259) : le modèle de Levain en Release, en plus court | Facultatif |

## Ajouter une lecture

Toute bonne source trouvée en chemin s'ajoute ici, dans la bonne section, avec ce qu'on y apprend et quand la
lire.
