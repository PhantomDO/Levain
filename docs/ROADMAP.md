# Roadmap v1

> Version 0.9 — 02/10/2026 — statut : **validé par Donnovan** (forward+ en clusters, ADR-0024)
>
> v0.9 : **un rendu forward+ en clusters dès M5.1** ([ADR-0024](adr/0024-forward-plus-en-clusters.md)), par
> décision de Donnovan : une PR de plus (la grille de clusters et le tri des lumières), M5.1 passe de 1,75 à
> **2,25 h**. Total : **61,4 h → 61,9 h** ; échéances inchangées. Puis la comparaison avec la visionneuse
> Khronos (#125, 0,5 h) passe de M5.1 à M5.4, après l'IBL, à la demande de Donnovan : M5.1 revient à 1,75 h,
> M5.4 passe à 2,25 h.
>
> v0.8 : **la cible web** ([ADR-0023](adr/0023-cible-web-webgpu.md)) : un backend WebGPU pour NVRHI, le moteur
> compilé en WebAssembly, et des fonctionnalités de rendu activées selon la plateforme. M4.6 est ajouté avant la
> phase 5, à la demande de Donnovan, sur la foi d'un prototype. Total : **56,9 h → 60,4 h** ; échéances
> inchangées.
>
> v0.7 : **clôture de la phase 4**, ratio 0,73 : **aucun recalibrage**, par décision de Donnovan (le ratio cumulé
> des phases 0 à 4 vaut 0,87, dans la fourchette). Les **études** passent de 0,15 à **0,4 h** (E2 à E4 en ont
> coûté 0,33 à 0,5) ; E6, qui n'était comptée nulle part, est ajoutée à M6.3. Total : **56,9 h**. Échéances
> inchangées : le projet a environ onze semaines d'avance sur elles.
>
> v0.6 : **le jeu est choisi** ([page de game design](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md)). Il ajoute 9,5 h : un dépôt séparé pour le jeu
> (M3.6), l'animation squelettique (M4.5, sortie des candidats v2), le terrain (M5.6, M7.6), l'eau et l'herbe
> (M5.7), la caméra à la troisième personne (M6.4), la nage et le planeur (M6.5), la collision du terrain
> (M6.2) et le gameplay de santé (M8.2). Donnovan a arbitré chaque ajout. Total : **47 h → 56,5 h** ; la v1
> finit vers le 16/05/2027 au lieu du 21/03. Les échéances à partir de M3.6 sont recalculées à 1,5 h par
> semaine depuis celle de M3.5 (01/11/2026), arrondies au dimanche suivant.
>
> v0.5 : **recalibrage après la phase 1** (ratio 0,67) : les estimations des phases 2 à 8 sont multipliées par
> 0,67, arrondies au quart d'heure, et les échéances recalculées. Total : **68 h → 47 h** (5,0 h et 3,0 h réelles
> pour les phases 0 et 1, puis 39,0 h estimées au lieu de 58,5). Voir « Recalibrage ».
>
> v0.2 : NVRHI remplace la RHI maison (ADR-0002) et flecs remplace l'ECS maison (ADR-0004). Le jalon « RHI
> mince » disparaît, les phases 1, 2, 3, 5 et 7 sont allégées, un backend Direct3D 12 et le choix du jeu sont
> ajoutés. Total : **77 h → 68 h**.
>
> v0.3 : passage à Rust (ADR-0010), annulé le jour même.
> v0.4 : **retour au C++23** (ADR-0011), périmètre **Linux d'abord**. **M1.4 (backend Direct3D 12) reste hors
> périmètre** — différé jusqu'à ce qu'une machine Windows soit disponible, et non plus supprimé. **M0.5 (retour
> au C++) ajouté.** Total : **68 h → 68 h** (−1,0 h pour M1.4 différé, +1,0 h pour M0.5).

## Comment lire les estimations

- **Heures Donnovan** : temps de Donnovan (pilotage de la session, réponses aux questions de Claude, relecture,
  questions). C'est la ressource rare, donc l'unité de planification.
- **Sessions Claude Code** : nombre de sessions de travail estimé. Chaque session consomme le quota de
  l'abonnement Claude.
- **Calendrier** : calculé pour **1,5 h par semaine**, à partir du lundi 21/09/2026. Les dates servent d'échéances
  aux milestones GitHub.
- **Incertitude** : les estimations de départ sont à ±50 %. À la fin de chaque phase, on compare l'estimé au
  réel, puis on recalcule la suite avec le ratio observé (voir « Recalibrage »).

## Synthèse

| Phase | Contenu | Heures Donnovan | Sessions | Fin visée (1,5 h/sem.) |
|---|---|---:|---:|---|
| 0 | Fondations | 5,0 (réel) | 4 | fini le 20/09/2026 |
| 1 | Fenêtre et premier triangle | 3,0 (réel, 4,5 estimées) | 3 | fini le 21/09/2026 |
| 2 | 3D de base | 4,25 (réel, 3,75 estimées) | 4 | fini le 22/09/2026 |
| 3 | Scène et ECS | 5,25 (réel, 4,5 estimées) | 5 | fini le 23/09/2026 |
| 4 | Assets, puis la cible web (M4.6) | 6,0 (réel, 8,25 estimées) + 4,5 (réel, 3,5 estimées) | 14 | M4.6 fini le 27/09/2026 |
| 5 | Rendu PBR et monde | 11,0 | 13 | 31/01/2027 |
| 6 | Physique et traversée | 7,15 | 6 | 28/02/2027 |
| 7 | Éditeur | 7,5 | 8 | 04/04/2027 |
| 8 | Audio et le jeu | 8,25 | 8 | 16/05/2027 |
| **Total** | | **61,9** | **65** | |

Les sessions Claude Code ne sont pas recalibrées : le ratio mesure le temps de Donnovan, pas le quota.

Durée restante après M4.6 (33,9 h) selon le rythme : **2 h/sem. → 17 semaines** (fin janvier 2027) ·
**1,5 h/sem. → 23 semaines** (mi-mars 2027) · **1 h/sem. → 34 semaines** (fin mai 2027).

Jalons visibles : **premier triangle** atteint le 21/09/2026 (prévu le 01/11/2026) · **choix du jeu** le
23/09/2026 (prévu le 01/11/2026) · **le jeu jouable** le 09/05/2027.

### Pourquoi 68 h et pas 60

> Section historique : depuis le recalibrage de la phase 1 (v0.5), le total est de 47 h.

L'estimation annoncée à l'oral (« autour de 60 h ») ne tenait pas compte de deux ajouts : le backend Direct3D 12
(1 h) et le choix du jeu (0,5 h). Surtout, ce que NVRHI et flecs ne touchent pas (fondations, assets, input,
physique, gizmos, audio, le jeu et son bilan) pèse à lui seul 34,5 h.

---

## Phase 0 — Fondations

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M0.1 Dépôt, suivi et specs | 1,0 | 0 | 27/09/2026 |
| M0.2 Squelette de build et CI | 1,5 | 2 | 04/10/2026 |
| M0.5 Retour au C++ | 1,0 | 1 | 27/09/2026 |
| M0.3 Core minimal | 1,5 | 1 | 11/10/2026 |

**M0.1 — Dépôt, suivi et specs.** Specs, roadmap et ADR validés ; dépôt GitHub public créé ; board, labels et
milestones en place (`tools/github-bootstrap.sh`) ; machine de référence renseignée.
*Critère* : toutes les issues des phases 0 et 1 existent sur le board avec une estimation.

**M0.2 — Squelette de build et CI.** Arborescence, `CMakePresets.json`, `vcpkg.json` (avec nvrhi et flecs),
exécutable `sandbox` qui affiche une ligne de log, doctest, clang-format, clang-tidy, workflow GitHub Actions
Windows + Linux, cache vcpkg, protection de `main`.
*Critères* : build depuis un clone propre avec deux commandes sur chaque OS ; CI verte sur les deux ; temps de
CI à froid et à chaud mesurés et notés.

**M0.5 — Retour au C++.** Le projet est passé à Rust puis revenu au C++ le 20/09/2026 (ADR-0010 puis
ADR-0011). Le retour apporte trois changements : périmètre **Linux d'abord**, presets et CI Windows retirés, et
la **forme du code** fixée par l'ADR-0011 — fonctions libres, dépendances dans la signature, pièges nommés.
*Critères* : build et tests verts sur Linux ; presets et CI Windows retirés ; ADR-0011 accepté.

**M0.3 — Core minimal.** Logs, assertions, politique de gestion d'erreurs (ADR-0008), horloge haute
résolution, lecture de fichiers, allocateurs linéaire (par frame) et pool, intégration de Tracy.
*Critères* : tests unitaires des allocateurs verts ; benchmark allocateurs vs `malloc` chiffré ; zones visibles
dans une capture Tracy.

**Étude E0 — Comment démarre un moteur** : `FEngineLoop` d'Unreal (PreInit, Init, Tick), PlayerLoop d'Unity,
`Main::setup` et `Main::iteration` de Godot.

## Phase 1 — Fenêtre et premier triangle

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M1.1 Fenêtre et boucle | 1,5 | 1 | 18/10/2026 |
| M1.2 Device NVRHI (Vulkan) et swapchain | 1,5 | 1 | 25/10/2026 |
| M1.3 Premier triangle | 1,5 | 1 | 01/11/2026 |
| ~~M1.4 Backend Direct3D 12~~ | — | — | **différé** (ADR-0011) |

**M1.1 — Fenêtre et boucle.** Fenêtre SDL3, boucle principale, événements, redimensionnement, mesure du
frame time ; ASan et UBSan en CI Linux.
*Critères* : redimensionnement et minimisation sans plantage ; frame time affiché ; zéro fuite signalée par
les sanitizers.

**M1.2 — Device NVRHI (Vulkan) et swapchain.** `DeviceManager` Vulkan inspiré de celui de Donut et adapté à
SDL3 : instance, device, queues, swapchain recréée au redimensionnement ; `nvrhi::vulkan::createDevice` ;
couche de validation NVRHI et validation layers Vulkan ; écran effacé à une couleur.
*Critères* : zéro erreur de validation sur 5 minutes avec redimensionnements ; temps de démarrage mesuré.

**M1.3 — Premier triangle.** Shaders Slang compilés au build en SPIR-V et DXIL, pipeline graphique, command
list NVRHI ; test de fumée headless sous lavapipe en CI Linux.
*Critères* : triangle affiché sous Linux ; zéro erreur de validation ; frame time CPU < 1 ms ; image du test de
fumée identique à la référence.

**M1.4 — différé (ADR-0011).** Le projet cible Linux d'abord : pas de backend Direct3D 12 tant qu'aucune
machine Windows n'est disponible. Le texte reste ici pour quand ce sera le cas.

*Contenu différé :* **Backend Direct3D 12.** `DeviceManager` D3D12 ; choix du backend au lancement (`--api vulkan|d3d12`) ;
test de fumée sous WARP (D3D12 logiciel) en CI Windows.
*Critères* : le même triangle sous les deux backends ; CI Windows verte ; binaire Windows de la CI lancé sous
Proton sur la machine de référence en `--api d3d12` (voir SPECS §10, « Vérification sous Windows »).

**Étude E1 — Les couches RHI** : déjà écrite ([E1-rhi.md](etudes/E1-rhi.md)), à relire pendant la phase.

## Phase 2 — 3D de base

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M2.1 Caméra, meshes et binding sets | 1,75 | 2 | 04/10/2026 |
| M2.2 Textures | 1,0 | 1 | 04/10/2026 |
| M2.3 Hot-reload des shaders | 1,0 | 1 | 11/10/2026 |

**M2.1 — Caméra, meshes et binding sets.** Caméra 3D, depth buffer, meshes indexés, constantes par frame
(volatile constant buffers de NVRHI), instancing, timestamps GPU ; stratégie de binding : **binding sets**,
rangés par fréquence de changement ([ADR-0013](adr/0013-binding-sets.md)).
*Critères* : 10 000 cubes instanciés à plus de 60 images/s en 1080p sur la machine de référence ; temps GPU
affiché.

**M2.2 — Textures.** Chargement stb_image, génération des mipmaps, samplers, filtrage anisotrope.
*Critère* : niveaux de mip vérifiés dans une capture RenderDoc.

**M2.3 — Hot-reload des shaders.** Surveillance des fichiers, recompilation à chaud en relançant le build des
shaders ([ADR-0014](adr/0014-hot-reload-des-shaders.md)), recréation des pipelines, repli si la compilation
échoue.
*Critères* : modification visible en moins d'1 s sans redémarrer ; une erreur de compilation ne fait pas
planter (message dans le log).

**Étude E2 — Ressources GPU et shaders** : binding sets et bindless dans NVRHI, ShaderCompileWorker d'Unreal,
variantes de shaders d'Unity.

## Phase 3 — Scène et ECS

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M3.1 Intégration de flecs et explorer | 1,0 | 1 | 18/10/2026 |
| M3.2 Transforms et hiérarchie | 0,75 | 1 | 18/10/2026 |
| M3.3 Boucle à pas fixe | 1,0 | 1 | 25/10/2026 |
| M3.4 Input par actions et caméra libre | 1,0 | 1 | 25/10/2026 |
| M3.5 Choix du jeu | 0,25 | 0 | 01/11/2026 |
| M3.6 Dépôt du jeu | 0,5 | 1 | 08/11/2026 |

**M3.1 — Intégration de flecs et explorer.** Monde flecs, composants de base, systèmes rangés par phases,
modules flecs ; le renderer dessine ce que contient le monde ; explorer web activé en Debug.
*Critères* : les entités de la démo sont visibles et modifiables dans l'explorer (flecs.dev/explorer) ; mise à
jour de 100 000 entités (Transform + Velocity) en moins d'1 ms.

**M3.2 — Transforms et hiérarchie.** Hiérarchie par le composant `flecs::Parent`, matrices monde calculées par
un système rangé par profondeur ([ADR-0015](adr/0015-stockage-de-la-hierarchie.md)).
*Critère* : 100 000 entités sur 10 niveaux de profondeur recalculées en moins de 2 ms.

**M3.3 — Boucle à pas fixe.** Pipeline flecs dédié à la simulation, exécuté N fois par frame par un
accumulateur ; interpolation du rendu ; garde-fou contre la « spirale de la mort »
([ADR-0016](adr/0016-boucle-a-pas-fixe.md)).
*Critère* : test automatique montrant un état de simulation identique au bit près après N ticks, que le rendu
tourne à 30, 60 ou 144 images/s.

**M3.4 — Input par actions et caméra libre.** Actions et axes (clavier, souris, manette), configuration dans un
fichier, caméra libre.
*Critères* : une même action pilotée au clavier et à la manette ; changement de touches sans recompiler.

**M3.5 — Choix du jeu.** Une page de game design : genre, boucle de jeu, contenu minimal, ce que le moteur doit
savoir faire. Les phases 4 à 8 sont ensuite ajustées pour servir ce jeu.
*Critère* : la page est validée et la roadmap mise à jour en conséquence. → [JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md)

**M3.6 — Dépôt du jeu.** Un ADR fixe la frontière entre moteur, plugins moteur (level design) et plugins
gameplay, et comment un plugin se lie (à la compilation en v1). Le dépôt du jeu, public, récupère le moteur par
`FetchContent` à un commit figé, surchargeable par un clone local ; il affiche une scène, avec sa CI.
*Critère* : la CI du jeu compile contre un commit figé du moteur, et une modification locale du moteur se voit
dans le jeu sans rien publier.

**Étude E3 — Modèles objets** : archetypes (flecs, Unity DOTS, Unreal Mass, Bevy) contre sparse sets (EnTT),
Actors/Components d'Unreal, GameObject d'Unity, Nodes de Godot.

## Phase 4 — Assets

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M4.1 Import glTF | 1,5 | 2 | 15/11/2026 |
| M4.2 Base d'assets | 1,75 | 2 | 22/11/2026 |
| M4.3 Cuisson des assets | 2,0 | 2 | 29/11/2026 |
| M4.4 Hot-reload des assets | 1,0 | 1 | 06/12/2026 |
| M4.5 Animation squelettique | 2,0 | 2 | 13/12/2026 |
| M4.6 Cible web (ajouté après la clôture de la phase) | 4,5 (réel, 3,5 estimées) | 5 | fini le 27/09/2026 |

**M4.1 — Import glTF.** fastgltf : meshes, matériaux, textures et hiérarchie convertis en entités flecs. En
préalable, le sandbox capture sa dernière image en PNG (`--capture`), pour que Donnovan voie les rendus à
distance (ajouté à sa demande le 24/09, +0,25 h, échéances inchangées).
*Critères* : la scène Sponza (Khronos glTF Sample Assets) s'affiche ; temps de chargement mesuré.

**M4.2 — Base d'assets.** GUID dans des fichiers `.meta`, rattachés par le hash après un renommage, registre,
références comptées par le monde ([ADR-0019](adr/0019-identifiants-d-assets.md)).
*Critères* : renommer ou déplacer un fichier ne casse aucune référence ; aucun chemin absolu dans les scènes.

**M4.3 — Cuisson des assets.** Outil hors ligne qui convertit vers un format binaire, textures KTX2 en UASTC
et leur cache BC7 par plateforme ([ADR-0020](adr/0020-cuisson-des-assets.md)).
*Critères* : Sponza cuite charge au moins 5 fois plus vite que le glTF brut ; mémoire vidéo des textures mesurée
avant et après.

**M4.4 — Hot-reload des assets.** Une texture modifiée est relue depuis sa source, sans recuisson
([ADR-0021](adr/0021-hot-reload-des-textures.md)) ; les modèles glTF attendront l'éditeur.
*Critère* : une texture modifiée dans un logiciel externe est visible en moins de 2 s.

**M4.5 — Animation squelettique.** Skinning des meshes glTF, lecture de clips, fondus entre clips, machine à
états simple (repos, marche, course, saut, chute, nage, vol plané). ozz-animation derrière une passerelle glTF,
skinning en compute ([ADR-0022](adr/0022-animation-squelettique.md)).
*Critère* : le personnage Quaternius passe du repos à la course selon sa vitesse, sans saut visible.

**M4.6 — Cible web.** Un backend WebGPU pour NVRHI, écrit dans le dépôt ; le moteur compilé en WebAssembly par
Emscripten ; les shaders compilés aussi en WGSL ; le sandbox publié sur GitHub Pages. En natif, le backend tourne
sur Dawn, pour le développer et le tester en CI ([ADR-0023](adr/0023-cible-web-webgpu.md)).
*Critères* : le sandbox (cubes, sol, modèle glTF, renard animé) tourne dans Firefox et dans un navigateur
Chromium, depuis GitHub Pages ; la CI compile et teste le backend.

**Étude E4 — Pipelines d'assets** : `.uasset` et Derived Data Cache d'Unreal, `.meta` et `Library/` d'Unity,
`.import` et UID de Godot.

## Phase 5 — Rendu PBR et monde

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M5.1 PBR direct | 1,27 (réel, 1,75 estimées) | 3 | fini le 02/10/2026 |
| M5.2 HDR et tonemapping | 0,73 (réel, 1,0 estimée) | 1 | fini le 02/10/2026 |
| M5.3 Ombres en cascades | 1,0 (réel, 1,75 estimées) | 2 | fini le 02/10/2026 |
| M5.4 Éclairage d'environnement (IBL) | 2,0 (réel, 2,25 estimées) | 2 | fini le 03/10/2026 |
| M5.5 Culling et statistiques | 1,0 (réel, 1,25 estimées) | 1 | fini le 03/10/2026 |
| M5.6 Terrain | 2,0 (réel, 1,25 estimées) | 2 | fini le 03/10/2026 |
| M5.7 Eau et herbe | 1,75 | 2 | 31/01/2027 |

**M5.1 — PBR direct.** Modèle metallic-roughness (Cook-Torrance), lumières directionnelle et ponctuelles,
en forward+ en clusters ([ADR-0024](adr/0024-forward-plus-en-clusters.md)) : un compute range les lumières
ponctuelles dans une grille 3D du volume de la caméra. Les passes de Donut servent de référence.
*Critère* : les matériaux glTF de Sponza s'affichent en PBR, normal maps comprises, sans erreur de validation,
et le même rendu sur Vulkan et sur WebGPU. La comparaison avec la visionneuse Khronos passe en M5.4 : la
visionneuse éclaire par IBL.

**M5.2 — HDR et tonemapping.** Cible de rendu RGBA16F, exposition, tonemapping (AgX ou ACES).
*Critère* : captures comparatives avant et après.

**M5.3 — Ombres en cascades.** Shadow maps directionnelles, 4 cascades, filtrage PCF.
*Critère* : temps GPU de la passe d'ombres mesuré sur Sponza.

**M5.4 — IBL.** Irradiance, spéculaire préfiltré, table BRDF, calculés en compute shaders.
*Critère* : MetalRoughSpheres (Khronos glTF Sample Assets) comparé à la visionneuse de référence, l'écart mesuré
par une commande versionnée (#125, reporté de M5.1 le 02/10/2026, +0,5 h).

**M5.5 — Culling et statistiques.** Frustum culling CPU, temps GPU par passe, compteurs de draw calls et de
triangles.
*Critère* : Sponza en PBR avec ombres et IBL à plus de 60 images/s en 1080p sur la machine de référence.

**M5.6 — Terrain.** Plugin moteur : terrain par heightmap, niveaux de détail, mélange de textures par une
carte de poids.
*Critère* : une vallée de 500 m à 1 m de résolution, rendue en moins de 2 ms GPU.

**M5.7 — Eau et herbe.** Plugin moteur : un lac calme (surface animée par normal maps, Fresnel, couleur selon la
profondeur) ; de l'herbe dense instanciée sur GPU, répartie par une carte de densité, animée par le vent.
*Critère* : la vallée avec son lac et son herbe à plus de 60 images/s en 1080p sur la machine de référence.

**Étude E5 — Forward, deferred, forward+** (0,4 h, dans M5.7) : les choix d'Unreal (deferred), d'Unity (URP et HDRP) et de Godot
(Forward+, Mobile, Compatibility).

## Phase 6 — Physique et traversée

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M6.1 Intégration Jolt | 1,75 | 2 | 07/02/2027 |
| M6.2 Colliders, requêtes, debug draw | 1,5 | 1 | 14/02/2027 |
| M6.3 Character controller | 1,65 | 1 | 21/02/2027 |
| M6.4 Caméra à la troisième personne | 1,0 | 1 | 28/02/2027 |
| M6.5 Nage, planeur et endurance | 1,25 | 1 | 28/02/2027 |

**M6.1 — Intégration Jolt.** Monde physique, corps statiques et dynamiques, synchronisation flecs ↔ Jolt au pas
fixe.
*Critère* : 1 000 caisses en chute libre, pas de simulation sous 4 ms.

**M6.2 — Colliders, requêtes, debug draw.** Boîtes, sphères, capsules, meshes ; raycasts ; couches de
collision ; affichage de debug ; collision du terrain (heightfield Jolt) ; volumes déclencheurs.
*Critère* : sélection d'un objet à la souris par raycast.

**M6.3 — Character controller.** `CharacterVirtual` de Jolt : pentes, marches.
*Critère* : se déplacer dans Sponza, escaliers compris.

**M6.4 — Caméra à la troisième personne.** Plugin gameplay, dans le dépôt du jeu : orbite autour du joueur,
collision avec le décor par sphere cast (le « spring arm » d'Unreal), recentrage automatique derrière lui,
cadrage propre au vol plané.
*Critère* : la caméra ne traverse jamais la roche en longeant une paroi de la vallée.

**M6.5 — Nage, planeur et endurance.** Plugin gameplay, dans le dépôt du jeu : états du joueur au-dessus du
character controller, volume d'eau, jauge d'endurance.
*Critère* : descendre du promontoire en planant, traverser le lac à la nage, et se noyer si l'endurance
s'épuise.

**Étude E6 — La physique dans les moteurs** (0,4 h, dans M6.3) : Chaos (Unreal), PhysX (Unity), Jolt (Godot 4.4 et plus) ;
pourquoi c'est presque toujours une bibliothèque.

## Phase 7 — Éditeur

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M7.1 ImGui et panneaux de debug | 1,0 | 1 | 07/03/2027 |
| M7.2 Réflexion et inspecteur | 1,25 | 1 | 14/03/2027 |
| M7.3 Sérialisation et undo/redo | 1,25 | 2 | 21/03/2027 |
| M7.4 Gizmos et picking | 1,75 | 2 | 28/03/2027 |
| M7.5 Play/Stop dans l'éditeur | 1,0 | 1 | 04/04/2027 |
| M7.6 Outils de terrain | 1,25 | 1 | 04/04/2027 |

**M7.1 — ImGui et panneaux de debug.** Renderer ImGui pour NVRHI (adapté de Donut), backend SDL3, panneaux de
statistiques et de profiling.
*Critère* : coût de l'UI inférieur à 0,5 ms par frame.

**M7.2 — Réflexion et inspecteur.** Réflexion des composants via l'addon meta de flecs (**ADR à écrire**), panneau de
hiérarchie, inspecteur de composants.
*Critère* : un nouveau composant devient éditable en une seule déclaration.

**M7.3 — Sérialisation et undo/redo.** Scènes en JSON via le sérialiseur de flecs, pattern Command.
*Critères* : sauvegarde puis chargement donnent une scène identique (test) ; 100 niveaux d'annulation.

**M7.4 — Gizmos et picking.** ImGuizmo, sélection à la souris par buffer d'identifiants.
*Critère* : sélection en une frame.

**M7.5 — Play/Stop.** Sauvegarde de la scène, simulation, restauration.
*Critère* : après Stop, la scène est identique à l'état d'avant Play (test).

**M7.6 — Outils de terrain.** Plugin moteur : sculpt de la heightmap, peinture des textures, placement au
pinceau des arbres, des rochers et de la densité d'herbe.
*Critère* : la vallée du jeu est faite entièrement dans l'éditeur, et survit à une sauvegarde puis un
chargement.

**Étude E7 — Réflexion et éditeurs** (0,4 h, dans M7.6) : Unreal Header Tool, sérialisation d'Unity, `ClassDB` de Godot, addon meta
de flecs.

## Phase 8 — Audio et le jeu

| Milestone | Heures D. | Sessions | Échéance |
|---|---:|---:|---|
| M8.1 Audio | 1,0 | 1 | 11/04/2027 |
| M8.2 Le jeu (vertical slice) | 6,0 | 6 | 09/05/2027 |
| M8.3 Bilan v1 | 1,25 | 1 | 16/05/2027 |

**M8.1 — Audio.** miniaudio, composants AudioSource et AudioListener, spatialisation 3D.
*Critère* : 32 sons 3D simultanés sans coupure.

**M8.2 — Le jeu (vertical slice).** Le jeu choisi en M3.5 ([JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md)), dans son propre dépôt, fait
uniquement avec le moteur et l'éditeur. Il comprend le gameplay de santé (cœurs, pièges, pommes, points de
contrôle) et les énigmes câblées par composants.
*Critères* : 5 à 10 minutes de jeu ; binaires Windows et Linux produits par la CI et publiés en Release.

**M8.3 — Bilan v1.** Mesures finales, rétrospective estimé vs réel, roadmap v2.

**Étude E8 — Post-mortem du moteur** (0,4 h, dans M8.3).

---

## Candidats v2

Render graph (frame graph de Frostbite, RDG d'Unreal) · job system multithread · GPU-driven rendering (draw
indirect, mesh shaders, que NVRHI prend en charge) · ray tracing (aussi pris en charge par NVRHI) · scripting
(Lua, C# ou WebAssembly) · réseau · streaming de monde · chargement dynamique des plugins · moteur installé comme
paquet (`find_package`) · **rendu WebGPU, pour tester dans le navigateur** (demandé par Donnovan le 24/09 ;
NVRHI n'a pas de backend WebGPU : un backend à écrire, ou un portage à évaluer, par un ADR).

Écartés du jeu pendant son choix ([JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md)) : escalade · ennemis et combat · cycle jour/nuit et ciel
procédural · rivière · réflexions sur l'eau · inventaire et cuisine · vraie UI de jeu.

## ADR à venir

| ADR | Sujet | Milestone |
|---|---|---|
| 0008 | Gestion d'erreurs (exceptions ou codes de retour) | M0.3 |
| 0009 | Stratégie de binding (binding sets ou bindless) | M2.1 |
| 0010 | Boucle à pas fixe et interpolation | M3.3 |
| 0011 | Identifiants d'assets et format `.meta` | M4.2 |
| 0024 | Forward ou forward+ (forward+ en clusters) | M5.1 |
| 0025 | Des passes de rendu venues d'un plugin (registre par étape, éclairage partagé) | M5.6 |
| 0013 | Réflexion des composants (addon meta de flecs) | M7.2 |

## Numérotation des ADR

**La roadmap ne pré-attribue plus de numéros d'ADR.** La v0.1 en annonçait cinq à l'avance ; l'aller-retour par
Rust en a consommé deux au passage (0010 et 0011), et toute la suite a glissé. Un numéro se prend **au moment
d'écrire l'ADR**, en suivant le dernier existant dans `docs/adr/`.

## Recalibrage

À la clôture de chaque phase, Claude calcule le ratio **heures passées / heures estimées** de la phase, l'inscrit
dans le journal, puis :

- si le ratio est entre 0,8 et 1,25 : rien à changer ;
- sinon : les estimations des phases restantes sont multipliées par ce ratio, et les échéances des milestones
  GitHub sont décalées en conséquence (dans une PR `docs(roadmap): recalibrage phase N`).

**Le ratio se calcule sur le temps total de Donnovan**, pas sur sa seule relecture : pilotage, questions et
décisions en font partie (voir la définition des « Heures Donnovan » plus haut). La phase 0 l'a appris à ses
dépens — mesurée d'abord à 3,0 h en ne comptant que les relectures, contre **4,9 h réelles**. Le ratio erroné de
0,50 aurait amputé la roadmap de 30 % sans raison.

### Phase 4 — ratio 0,73, aucun recalibrage (décision de Donnovan)

| Milestone | Estimé | Passé |
|---|---:|---:|
| M4.1 Import glTF | 1,5 h | 1,25 h |
| M4.2 Base d'assets | 1,75 h | 0,75 h |
| M4.3 Cuisson des assets | 2,0 h | 2,0 h |
| M4.4 Hot-reload des assets | 1,0 h | 1,0 h |
| M4.5 Animation squelettique | 2,0 h | 1,0 h |
| **Phase 4** | **8,25 h** | **6,0 h** — ratio **0,73** |

Hors de la fourchette 0,8–1,25 : la règle demandait de multiplier les phases 5 à 8 par 0,73. Donnovan a choisi
de ne pas le faire, sur sondage. Le ratio cumulé des phases 0 à 4 vaut **0,87** (23,5 h pour 27,0), dans la
fourchette ; et après le recalibrage ×0,67 de la phase 1, les phases 2 et 3 étaient remontées à 1,13 et 1,17 :
la roadmap avait été trop réduite. La phase 4 a été rapide parce que son travail se découpait en PR courtes,
déjà vérifiées par des mesures, et relues vite ; la phase 5 (PBR, ombres, terrain, eau) est plus neuve. Le point
se refait à sa clôture.

La même clôture corrige les **études** : 0,15 h estimées, 0,33 à 0,5 h passées (E2, E3, E4). Elles passent à
0,4 h ; E6, oubliée des estimations, entre dans M6.3.

### Phase 3 — ratio 1,17, aucun recalibrage

| Milestone | Estimé | Passé |
|---|---:|---:|
| M3.1 Intégration de flecs et explorer | 1,0 h | 0,50 h |
| M3.2 Transforms et hiérarchie | 0,75 h | 0,42 h |
| M3.3 Boucle à pas fixe | 1,0 h | 1,33 h |
| M3.4 Input par actions et caméra libre | 1,0 h | 1,00 h |
| M3.5 Choix du jeu | 0,25 h | 1,50 h |
| M3.6 Dépôt du jeu | 0,5 h | 0,50 h |
| **Phase 3** | **4,5 h** | **5,25 h** — ratio **1,17** |

Dans la fourchette 0,8–1,25 : les estimations des phases 4 à 8 sont conservées. Sans M3.5, le ratio serait de
0,88 : le dépassement vient du choix du jeu, qui a cadré toute la v1 au lieu de choisir un genre, et non d'une
sous-estimation du travail de moteur. Le ratio cumulé des phases 0 à 3 est de **0,93** (17,5 h pour 18,75).

### Phase 2 — ratio 1,13, aucun recalibrage

| Milestone | Estimé | Passé |
|---|---:|---:|
| M2.1 Caméra, meshes et binding sets | 1,75 h | 2,0 h |
| M2.2 Textures | 1,0 h | 1,0 h |
| M2.3 Hot-reload des shaders | 1,0 h | 1,25 h |
| **Phase 2** | **3,75 h** | **4,25 h** — ratio **1,13** |

Dans la fourchette 0,8–1,25 : les estimations des phases 3 à 8 sont conservées. Le ratio remonte au-dessus de
1, comme l'annonçait la réserve du recalibrage de la phase 1 ; le ratio cumulé des phases 0 à 2 est de **0,86**.

### Phase 1 — ratio 0,67, recalibrage (v0.5)

| Milestone | Estimé | Passé |
|---|---:|---:|
| M1.1 Fenêtre et boucle | 1,5 h | 1,25 h |
| M1.2 Device NVRHI et swapchain | 1,5 h | 0,72 h |
| M1.3 Premier triangle | 1,5 h | 1,03 h |
| **Phase 1** | **4,5 h** | **3,0 h** — ratio **0,67** |

Hors de la fourchette : les estimations des phases 2 à 8 sont multipliées par 0,67, arrondies au quart d'heure,
et les échéances recalculées à 1,5 h par semaine à partir du 21/09/2026. Le temps vient du total déclaré par
Donnovan pour la journée ; ce n'est pas une erreur de mesure comme en phase 0.

**Deux réserves.** L'échantillon tient en une journée intense ; et la phase 0 donnait 0,83. Le ratio cumulé des
deux phases, (5,0 + 3,0) / (6,0 + 4,5) = **0,76**, est plus robuste et sortirait lui aussi de la fourchette. Le
prochain point de contrôle est la clôture de la phase 2 : un ratio remonté vers 1 y corrigerait l'excès.

### Phase 0 — ratio 0,83, aucun recalibrage

| | Estimé | Passé | Ratio |
|---|---:|---:|---:|
| Phase 0 (14 issues, 5 milestones) | 6,0 h | **5,0 h** | **0,83** |

Dans la fourchette 0,8–1,25 : les estimations des phases 1 à 8 sont conservées telles quelles. À réexaminer à
la clôture de la phase 1, qui sera le premier échantillon de vrai code de rendu — la phase 0 était faite de
specs, d'ADR et de configuration, et n'en dit pas grand-chose.
