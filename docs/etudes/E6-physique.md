# E6 — La physique dans les moteurs

> Écrite le 05/10/2026, en M6.3. Lecture : 10 minutes.
> Convention : ce qui est **documenté** renvoie à une source ; ce qui est **déduit** est signalé.

## La question

Un moteur physique fait quatre choses à chaque pas :

- la **broad phase** trouve les paires d'objets qui pourraient se toucher, grâce à un arbre de boîtes ;
- la **narrow phase** calcule les vrais contacts de ces paires : points, normales, profondeurs ;
- le **solveur** trouve les vitesses qui respectent tous les contacts et toutes les articulations à la fois,
  puis avance les positions ;
- les **requêtes** répondent au jeu entre deux pas : rayons, formes lancées, chevauchements.

Chaque étape est un domaine de recherche, et la robustesse coûte cher : une pile qui ne tremble pas, un objet
rapide qui ne traverse pas un mur, un monde qu'on charge et décharge pendant la partie. Les moteurs du marché
écrivent leur rendu, mais presque jamais leur physique. Pourquoi ? Et quand en écrivent-ils une ?

## Ce que fait notre moteur (et pourquoi)

Levain intègre **Jolt** (ADR-0026), sous licence MIT, derrière ses propres types : `Collider`, `RigidBody`, des
fonctions libres. Aucun en-tête de Jolt ne sort de `engine/physics/src`. Le pas est fixe, à 60 Hz, dans la
phase `Physics` du pipeline flecs. Jolt fait autorité sur la pose des corps dynamiques. Les couches de collision
forment une table fixe.

Au-dessus, le moteur ajoute ce que Jolt ne sait pas d'un jeu :

- les volumes déclencheurs, posés comme une relation flecs `(InsideOf, volume)` (ADR-0027) ;
- les grandes formes partagées ;
- un personnage qui délègue à `CharacterVirtual`, avec la marche dans un plugin (ADR-0028).

Mesures : 1 000 caisses en 0,52 ms par pas en moyenne (critère de M6.1, journal du 05/10). La forme de collision
de Sponza, 262 000 triangles, coûte environ 118 ms à construire (sonde de l'ADR-0028).

Pourquoi Jolt plutôt que PhysX, plutôt que du code maison : voir l'ADR-0026. Cette étude regarde ce qu'ont fait
les autres.

## Unreal : PhysX, puis Chaos, fait maison

Unreal 4 utilisait PhysX, de NVIDIA. Unreal 5 l'a remplacé : « UE5 uses the Chaos Physics engine for physical
simulation, replacing PhysX as the default » (**documenté** [1]). Le guide de migration prévient que la
simulation « behaves differently from PhysX ». Il annonce aussi un changement d'architecture : la physique peut
tourner **sur son propre thread, à pas fixe**, pour gagner en déterminisme et faciliter le réseau, au prix d'un
délai entre une demande du jeu et la réaction de la physique (**documenté** [1]).

Chaos couvre bien plus que les corps rigides : la destruction (les *Geometry Collections*), les tissus, les
véhicules, la réplication réseau, les cheveux, les fluides, les corps mous (**documenté** [2]). Epic explique
son choix par la volonté de faire évoluer la physique en interne et de construire des systèmes « networked
physics, large world coordinates, vehicles, and destruction » (billet technique [3] ; la page refuse l'outil de
lecture, la phrase vient de son extrait indexé).

Ce qu'on en déduit : Epic a écrit sa physique parce que ses besoins **débordaient** d'une bibliothèque
généraliste. La destruction de Fortnite, la réplication, les coordonnées 64 bits des grands mondes touchent
toutes au cœur du solveur. Un studio de cette taille peut payer une équipe physique permanente (**déduit**).

## Unity : PhysX intégré, puis deux moteurs pour les données

Unity a deux mondes (**documenté** [4]) :

- **les GameObjects** utilisent la physique intégrée : « Nvidia PhysX engine integration » en 3D, « Box2D engine
  integration » en 2D ;
- **les projets DOTS**, orientés données, utilisent le paquet **Unity Physics**, écrit par Unity, ou **Havok
  Physics for Unity**, qui le remplace sans changer les données.

Les deux moteurs DOTS diffèrent par un point de conception. Unity Physics est **sans état** : chaque pas repart
des seules données du monde. Havok est « deterministic but stateful », et une copie du monde ne simule à
l'identique que si ses caches sont copiés aussi (**documenté** [5]). Sans état, le moteur se prête au réseau par
rollback et aux mondes dupliqués. Avec état, il garde le sommeil des corps et les contacts du pas précédent,
donc il va plus vite sur une grande scène (**déduit**).

Le `CharacterController` de Unity est une capsule cinématique : `stepOffset` règle les marches, `slopeLimit` la
pente, `skinWidth` la marge (**documenté** [6]). Ce sont les réglages du contrôleur de PhysX (**déduit** de la
correspondance avec [7]).

## Godot : maison, Bullet, maison, puis Jolt

Godot a changé trois fois :

1. **Un moteur maison** au départ : peu de moteurs existaient, et l'API de Godot (`Area`, `KinematicBody`,
   rayons) demandait trop de modifications à ceux qui existaient (**documenté** [8]).
2. **Bullet en 3.0**, parce que « maintaining a physics engine and keeping it up to date with the new techniques
   and algorithms is time consuming » (**documenté** [8]).
3. **Retour au moteur maison en 4.0**, Godot Physics, avec Bullet relégué en plugin officiel (**documenté**
   [9]).
4. **Jolt en 4.4**, d'abord comme alternative à Godot Physics (**documenté** [10]), puis **moteur par défaut
   des nouveaux projets 3D en 4.6** (**documenté** [11]). Le changement s'est fait par l'interface de serveur de
   Godot (`PhysicsServer3D`) : le moteur derrière change, les nœuds du jeu restent (**déduit**).

La doc de l'intégration liste ce qui change avec Jolt : des articulations dont les limites souples ne se
traduisent pas, des marges de collision qui rétrécissent la forme avant d'y ajouter une coque, une stabilisation
appliquée à la position seulement (**documenté** [10]). C'est le prix d'un changement de moteur : le même jeu ne
tombe plus tout à fait pareil.

## Autres : Decima et Jolt

Pour *Horizon Forbidden West*, Guerrilla a quitté « a commercial physics engine » pour Jolt. Leur ancienne
physique « caused bottlenecks while streaming in data and while interacting with the multi-threaded game object
update ». Avec Jolt, ils ont gagné de la mémoire et de la taille d'exécutable, et doublé la fréquence de
simulation pour moins de temps CPU (**documenté** [12]). Jolt est né d'un projet personnel de Jorrit Rouwe,
qui l'a écrit pour ce besoin et l'a publié sous licence MIT (**documenté** [12], [13]). Ses objectifs déclarés
sont ceux d'un moteur de monde ouvert : requêtes en parallèle de la simulation, chargement en arrière-plan sans
réveiller les corps, déterminisme (**documenté** [13]).

## Ce qu'on en retient

1. **La physique est une bibliothèque, parce qu'elle coûte une équipe pour toujours.** Godot l'a écrite, l'a
   abandonnée pour la maintenance, l'a reprise, puis a adopté Jolt. Ceux qui écrivent la leur ont un besoin
   qu'aucune bibliothèque ne couvre : la destruction en réseau d'Epic, le streaming de Guerrilla. Même alors, le
   résultat redevient une bibliothèque (Jolt, publié ; Chaos, réutilisé par tous les jeux Unreal).
2. **Les bibliothèques sont devenues ouvertes** : PhysX sous BSD-3 depuis sa version 4.0, en 2018 (**documenté**
   [14]), Jolt sous MIT. Havok reste commercial : son code C++ est réservé aux clients d'une licence complète
   (**documenté** [5]). Levain a pu choisir sur la technique seule.
3. **Tous les moteurs isolent la physique derrière leurs types.** C'est ce qui a permis les migrations : Unreal de
   PhysX à Chaos, Godot de Bullet à Jolt. Elles ont tout de même changé le comportement des jeux existants : le
   guide d'Unreal et la doc de Godot le disent. Levain fait de même (ADR-0026). Ce qui fuirait de Jolt jusqu'au
   jeu (un réglage, une couche, un comportement de marge) se paierait au prochain changement.
4. **Le personnage est toujours du code du moteur, au-dessus de la bibliothèque.** Le contrôleur de PhysX est
   « an external component built on top of the PhysX SDK ». Il recommande de laisser au jeu la poussée des objets
   dynamiques : « the CCT module should best be coupled to specific game code » (**documenté** [7]). Le
   `CharacterVirtual` de Jolt pousse lui-même, selon sa masse. Levain le garde (choix de Donnovan, ADR-0028), en
   sachant que ce réglage est un choix de gameplay.
5. **Ce qui reste à surveiller** : le pas sur un thread à part, comme Chaos, quand le jeu sera plus lourd ;
   et le déterminisme, que Jolt offre et que le réseau voudrait. *Rando* est solo : ni l'un ni l'autre ne
   presse.

## Sources

1. Epic Games, *Unreal Engine 5 Migration Guide*, « Physics » —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-migration-guide
2. Epic Games, *Physics in Unreal Engine* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-in-unreal-engine
3. Epic Games, *Chaos Scene Queries and Rigid Body Engine in UE5* (billet technique) —
   https://www.unrealengine.com/en-US/tech-blog/chaos-scene-queries-and-rigid-body-engine-in-ue5
4. Unity, *Physics* (manuel 6000.1) — https://docs.unity3d.com/6000.1/Documentation/Manual/PhysicsSection.html
5. Unity, *Havok Physics for Unity*, FAQ — https://docs.unity3d.com/Packages/com.havok.physics@1.4/manual/faq.html
6. Unity, *Character Controller component reference* — https://docs.unity3d.com/Manual/class-CharacterController.html
7. NVIDIA, *PhysX 5.4.1, Character Controllers* —
   https://nvidia-omniverse.github.io/PhysX/physx/5.4.1/docs/CharacterControllers.html
8. Godot, *Godot 3.0 switches to Bullet for physics* — https://godotengine.org/article/godot-30-switches-bullet-3-physics/
9. Godot, *Physics progress report #1* (19/05/2021) — https://godotengine.org/article/physics-progress-report-1/
10. Godot, *Using Jolt Physics* — https://docs.godotengine.org/en/latest/tutorials/physics/using_jolt_physics.html
11. Godot, *Godot 4.6 Release* — https://godotengine.org/releases/4.6/
12. GDC Vault, *Architecting Jolt Physics for « Horizon Forbidden West »* (Jorrit Rouwe, 2022) —
    https://gdcvault.com/play/1027560/Architecting-Jolt-Physics-for-Horizon
13. Jolt Physics, README — https://github.com/jrouwe/JoltPhysics
14. NVIDIA, *Announcing PhysX SDK 4.0, an Open-Source Physics Engine* —
    https://developer.nvidia.com/blog/announcing-physx-sdk-4-0-an-open-source-physics-engine
