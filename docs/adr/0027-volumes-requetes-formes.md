# ADR-0027 — Volumes déclencheurs, requêtes et grandes formes

- **Statut** : accepté le 2026-10-05 (proposé par l'agent dans la nuit ; relu par un subagent, qui en a mesuré
  la première décision ; confirmé par Donnovan au sondage du matin, qui a demandé l'API côté volume et des
  capteurs rendus cinématiques par le moteur)
- **Date** : 2026-10-05
- **Milestone** : M6.2

## Contexte

M6.1 a posé des corps qui tombent et se heurtent (ADR-0026). M6.2 doit donner au jeu de quoi **interroger** la
physique et **réagir** à elle, et au monde ses vraies formes :

1. **les volumes déclencheurs** (couche `Sensor`) : les pièges et points de contrôle de *Rando*, l'eau de la
   nage (M6.5). Comment le gameplay apprend-il qu'un corps entre ou sort ?
2. **les requêtes** : un rayon (la sélection à la souris, critère de M6.2), une sphère lancée (la caméra de
   M6.4) ;
3. **les grandes formes** : le terrain de 513 × 513 hauteurs (le plugin terrain, ADR-0018) et les maillages
   (le décor importé en glTF). L'ADR-0026 a noté qu'elles ne tiennent pas dans un composant de valeurs ;
4. **l'affichage de debug** : voir les formes de collision par-dessus l'image.

Deux contraintes viennent de Jolt (Architecture.md, « Sensors ») : ses rappels de contact arrivent sur ses
threads de travail, dans un ordre qui varie ; et un capteur **statique** perd le contact d'un corps qui
s'endort, ce qui fabriquerait une sortie fantôme.

## Options envisagées

**1. Livrer l'entrée et la sortie d'un volume**

Les chiffres viennent d'une mesure ponctuelle de la relecture (un programme jetable, non versionné, contre flecs
4.1.6, avec 20 requêtes en cache et 1 000 corps à cinq composants) : des ordres de grandeur, qui ne se citent
pas comme des mesures du moteur (règle n°6). La décision tient sans eux, par la fragmentation seule :

| Option | Pour | Contre |
|---|---|---|
| **A. Une relation flecs `(InsideOf, volume)` sur le corps** : posée à l'entrée, retirée à la sortie | Ce qui est dedans est une donnée : `player.has<InsideOf>(eau)` répond à la nage, une requête `with<InsideOf>(eau)` à « qui est dans l'eau ? » (le `get_overlapping_bodies` de Godot) ; l'entrée et la sortie sont les `OnAdd` et `OnRemove` de la paire, observés comme des signaux. **0,18 µs par changement**, et un nombre de tables borné (239 → 268 sur 100 000 changements). Supprimer le volume émet un `OnRemove` par occupant | Un corps dans deux volumes porte deux paires : le nombre de tables croît avec les combinaisons réellement visitées, pas avec le temps |
| A'. La même relation sur le volume, `(Overlaps, corps)` | La première version de cet ADR | **Écartée à la mesure** : chaque ensemble d'occupants est une nouvelle table, jamais libérée (237 → 20 216 tables sur 20 000 changements, 141 µs par changement). Le trait `DontFragment` de flecs 4.1.6 ne la sauve pas : une requête `(Overlaps, *)` y perd des paires, et la suppression de la cible n'émet pas d'`OnRemove` |
| B. Des événements flecs émis sur le volume (`TriggerEntered`, `TriggerExited`) | Le plus proche d'`OnTriggerEnter` d'Unity et des événements de chevauchement d'Unreal | « Qui est dedans ? » demande au gameplay de tenir sa propre liste |
| C. Une file d'événements en singleton, vidée par les systèmes du gameplay | La plus simple | Le gameplay doit filtrer la file lui-même ; rien n'empêche deux systèmes de la consommer différemment |

**2. Le capteur qui perd un corps endormi**

| Option | Pour | Contre |
|---|---|---|
| **A. Des capteurs cinématiques**, gardés éveillés | Jolt l'indique : un capteur cinématique actif détecte aussi les corps endormis (le test « un corps qui s'endort dans un volume y reste » le vérifie, avec une caisse) | Plus coûteux qu'un capteur statique ; acceptable pour les quelques dizaines de volumes d'un niveau |
| B. Des capteurs statiques, et une sortie ignorée si le corps s'endort | Le moins coûteux | Une sortie réelle d'un corps qui s'endort juste après serait perdue ; vérifié : « ajouté, endormi, retiré » pour une boule qui n'a pas bougé |

**3. Les grandes formes**

| Option | Pour | Contre |
|---|---|---|
| **A. Une donnée partagée et immuable** (`std::shared_ptr<const TriangleMesh>`, `<const HeightField>`) dans le variant de formes ; le module garde la forme Jolt construite pour chaque donnée | Le composant reste une valeur copiable ; cent rochers identiques partagent une forme | Un pointeur dans un composant de données ; la donnée ne doit plus changer une fois partagée (le sculpt de M7.6 en créera une nouvelle) |
| B. Des assets identifiés par GUID (ADR-0019), résolus par le module | Sérialisable tel quel | Le terrain est généré par le code, pas un asset ; une étape de cuisson en plus |

**4. L'affichage de debug**

| Option | Pour | Contre |
|---|---|---|
| **A. Des lignes de debug dans `render`**, que la physique alimente par une fonction libre (les arêtes de chaque forme) | Utile au-delà de la physique (la caméra, l'IA, les volumes de l'éditeur), comme `DrawDebugLine` d'Unreal ou `Debug.DrawLine` d'Unity ; aucune dépendance nouvelle | Le dessin des formes simples est à écrire ; pour les autres, `Shape::GetTrianglesStart` de Jolt existe sans la feature `debugrenderer` |
| B. Le `DebugRenderer` de Jolt, branché sur NVRHI | Toutes les formes, dessinées par Jolt | La feature vcpkg `debugrenderer` change les définitions `JPH_*` des deux côtés ; un renderer à écrire quand même ; seulement pour la physique |

## Décision

1. **Les volumes posent une relation flecs `(physics::InsideOf, volume)` sur chaque corps qu'ils
   contiennent**, et le gameplay s'en sert **du point de vue du volume** (choix de Donnovan) :

   ```cpp
   // Un piège réagit à ce qui entre en lui, et à ce qui en sort.
   physics::onEnter(world, trap, [](flecs::entity body) { hurt(body); });
   physics::onExit(world, trap, [](flecs::entity body) { heal(body); });

   // Le lac demande qui est en lui.
   for (const flecs::entity body : physics::occupantsOf(world, lake)) { … }

   // Un système de gameplay, parallélisable : ce sont des données. Il vise « un volume qui est un
   // lac » par une variable, et non ce lac-ci : flecs refuse de supprimer une entité qu'une requête
   // vise encore.
   world.system<Stamina>()
       .with<physics::InsideOf>("$volume").with<Lake>().src("$volume")
       .kind<scene::Simulation>().multi_threaded()…
   ```

   C'est bien le volume qui demande ; la réponse est rangée sur le corps parce qu'un ECS à archetypes range
   une entité selon ses composants, et que la liste changeante des occupants d'un volume en ferait une table
   par ensemble d'occupants (option A'). flecs indexe une paire par sa cible : il trouve directement les tables qui
   portent `(InsideOf, lac)`, sans parcourir les autres. Le corps peut aussi demander `has<InsideOf>(lac)` (la nage). Les
   fonctions de l'API sont de la glu d'une ligne, documentées avec ces exemples dans le README du module. La
   collecte :
   - un `ContactListener` reçoit, sur les threads de Jolt, les contacts ajoutés et retirés. Jolt les donne
     **par paire de sous-formes**, les deux corps rangés par `BodyID` et non capteur en premier, et
     `OnContactRemoved` ne donne que des `BodyID`, parfois au pas qui suit la destruction du corps ;
   - les contacts sans capteur sont écartés **avant** de prendre le mutex, par une table des `BodyID` de
     capteurs tenue par le module : les 1 000 caisses en produisent beaucoup ;
   - un compteur par paire de `BodyID`, qui garde les deux entités relevées à l'ajout (les *user data* ne
     sont lisibles qu'à ce moment-là) ;
   - après le pas, dans la phase `PostPhysics`, une **différence au niveau des entités** entre l'état d'avant
     et celui d'après, triée pour le déterminisme, pose et retire les paires. Un corps reconstruit au même
     pas (nouveau `BodyID`, même entité) ne fait donc ni sortie ni entrée fantômes. Le rappel ne touche jamais
     au monde flecs.
2. **Un `Collider` de la couche `Sensor` est rendu cinématique par le module**, et ne s'endort jamais
   (`mAllowSleeping = false`, par sûreté : Jolt n'endort déjà pas un capteur actif) : il voit aussi les corps
   endormis, et le gameplay n'a rien à y penser. Un
   capteur avec un `RigidBody` dynamique est refusé bruyamment, comme un maillage ou une grille en capteur,
   qui n'ont ni intérieur ni extérieur et ne verraient rien de ce qu'ils contiennent.
3. **Les requêtes sont des fonctions libres** sur `PhysicsWorld` : `raycast` (le premier corps touché :
   entité, point, normale, distance) et `sphereCast`, filtrées par un masque de couches. Par défaut, elles
   traversent les capteurs. Elles voient chaque corps à **la pose du dernier pas** (ou à celle de sa
   création) : un cinématique déplacé depuis n'y est pas encore, et la sélection à la souris vise la pose du
   pas, non la pose interpolée qu'on voit. Un rayon ne voit que les surfaces qu'il traverse en entrant, comme
   celui d'Unity : rien du corps d'où il part, rien du dessous d'un terrain. Un *sphere cast* qui part dans
   un mur rend une distance nulle s'il avance vers lui, rien s'il s'en éloigne : la caméra peut s'en dégager.
   La caméra de M6.4 demandera un filtre qui ignore le joueur (`BodyFilter`).
4. **Les grandes formes sont des données partagées et immuables**, `TriangleMesh` et `HeightField`, ajoutées au
   variant de formes ; le module garde la forme Jolt de chaque donnée tant que la donnée vit (un `weak_ptr`
   vérifié, pour qu'une donnée réallouée à la même adresse ne reprenne pas une forme périmée). Elles sont
   refusées bruyamment là où Jolt les ignorerait en silence : un maillage dynamique (Jolt ne calcule pas sa
   masse, ni sa collision contre un autre maillage ou une grille), une grille de hauteurs mobile ou de moins
   de 3 × 3 échantillons, un maillage ou une grille en capteur, un sommet ou une hauteur qui n'est pas fini.
   Un maillage cinématique reçoit du module la masse que Jolt exige et ne sait pas calculer. Le plugin
   terrain, qui dépend désormais de `physics` (ADR-0018), fournit le `Collider` de sa heightmap
   (`colliderOf`) ; l'application le pose.
5. **L'affichage de debug passe par des lignes de `render`** (`DebugLinesPass`), que la physique alimente par
   des fonctions pures (`appendOutline`) et que l'application relie, comme `scene` et `render`. Le volume est
   borné : une grille de hauteurs n'est dessinée que par son bord (ses 263 000 hauteurs feraient près de
   790 000 lignes, arêtes et diagonales), et le sandbox ne dessine que le contour de ce qu'on a sélectionné.
   La passe dessine **en un seul appel par command list** (WebGPU écrit ses sommets avant toute la command
   list) : plusieurs producteurs réunissent leurs lignes en une liste, ce que fait aujourd'hui l'application,
   faute de collecteur. Elle en dessine au plus `MaxDebugLines` (65 536) et le dit une fois au journal ; un
   mode `OnTop` dessine par-dessus ce qui est déjà à l'écran, pour le contour d'une sélection.

## Conséquences

- **Une entrée ou une sortie est posée dans `PostPhysics`, au pas où elle arrive.** Un observateur de la paire
  y réagit tout de suite, à la fusion des commandes ; un système de gameplay la voit au pas suivant, dans la
  phase `Simulation`.
- **La suppression d'un corps est une sortie** : flecs retire sa paire et émet son `OnRemove` avant de
  détruire l'entité ; `onExit` la voit, le corps encore vivant avec tous ses composants, et ne peut pas la
  distinguer d'une sortie (testé). **Supprimer un volume** retire les paires de ce qu'il contenait, et ses
  propres `onEnter` et `onExit` partent avec lui : flecs refuse de supprimer une entité qu'un observateur ou
  une requête vise encore (une assertion, en Debug seulement : en Release, la suppression passe et laisse une
  requête qui vise une entité morte), d'où des observateurs enfants du volume (vu à l'implémentation), et des
  systèmes qui visent le volume par une variable. Ces observateurs **peuvent voir ou non** la dernière sortie
  de ce que le volume contenait : flecs traite les tables dans leur ordre de création. Le gameplay ne s'y fie
  pas.
- **Ce qu'un volume voit** : la matrice des couches ne lui fait voir ni `Static` ni `Debris` (un rocher de
  décor ne fait pas de remous), ni un autre volume. Un cinématique de `Dynamic` ou de `Character` est vu, par la
  règle « cinématique contre capteur » de Jolt (`Body.inl`) : c'est elle qui rendra le joueur visible.
- **Le personnage n'est vu ni par les capteurs ni par les rayons** : le `CharacterVirtual` de Jolt n'est pas
  un corps (Architecture.md, « Character Controllers »). Son `mInnerBodyShape` crée un corps intérieur,
  détecté par les capteurs et les requêtes : sur la couche `Character`, avec l'entité du joueur dans ses
  *user data*, il passera par le même `ContactListener` (M6.3), et la nage (M6.5) verra le joueur entrer
  dans l'eau.
- **Le terrain gagne une dépendance** : `plugin::terrain` lie `levain::physics`. Un jeu sans physique qui
  voudrait le terrain la paierait ; c'est le classement de l'ADR-0018 (« collision du terrain » dans le plugin).
- **Construire une grande forme coûte** : 8,3 ms pour la grille de 513 × 513, 350 ms pour un maillage de
  524 000 triangles (mesure ponctuelle de la relecture, non versionnée : un ordre de grandeur). Elles sont construites dans le pas qui suit leur `set` : un à-coup
  au chargement, qu'on assume ; si un monde en charge en cours de partie, il faudra les construire à part.
- **Une donnée ne se modifie pas** une fois partagée : le sculpt (M7.6) produira une nouvelle donnée et
  reconstruira le corps. C'est un choix de Levain, pas une limite de Jolt, qui sait modifier une partie d'une
  grille en place (`HeightFieldShape::SetHeights`) ; à reprendre si reconstruire coûte trop.
- **Les capteurs restent dans la broad phase `NonMoving`** (ADR-0026) bien qu'ils soient cinématiques : ils ne
  bougent presque jamais, et un capteur déplacé coûtera une mise à jour de l'arbre du décor.

## Ce que font les autres moteurs

- **Unity** : tout collider d'un objet qui a un `Rigidbody` peut produire des événements `OnTrigger`
  (`OnTriggerEnter`, `OnTriggerStay`, `OnTriggerExit`) avec un collider `isTrigger` (**documenté** [1]). Les
  requêtes sont des fonctions statiques filtrées par un `LayerMask`, `Physics.Raycast` (**documenté** [2]) ;
  `Debug.DrawLine` dessine une ligne de debug (**documenté** [8]).
- **Godot** : un `Area3D` émet `body_entered` et `body_exited`, et `get_overlapping_bodies` liste ce qui est
  dedans (**documenté** [3]) : notre option A réunit les deux. Les rayons passent par
  `PhysicsDirectSpaceState3D.intersect_ray` (**documenté** [4]).
- **Unreal** : un acteur reçoit `ReceiveBeginOverlap` et `ReceiveEndOverlap` quand les deux objets ont
  « Generate Overlap Events » (**documenté** [5]). Les rayons sont des *traces*, par canal de trace, à un ou
  plusieurs résultats (**documenté** [6]) ; `DrawDebugLine` dessine une ligne de debug (**documenté** [9]).

## Sources

1. Unity, `Collider.OnTriggerEnter` — https://docs.unity3d.com/ScriptReference/Collider.OnTriggerEnter.html
2. Unity, `Physics.Raycast` — https://docs.unity3d.com/ScriptReference/Physics.Raycast.html
3. Godot, `Area3D` — https://docs.godotengine.org/en/stable/classes/class_area3d.html
4. Godot, *Ray-casting* — https://docs.godotengine.org/en/stable/tutorials/physics/ray-casting.html
5. Epic Games, *Collision Overview* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/collision-in-unreal-engine---overview
6. Epic Games, *Traces with Raycasts* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/traces-with-raycasts-in-unreal-engine
7. Jolt Physics, *Architecture*, sections « Sensors », « Character Controllers », « Dynamic Mesh Shapes » —
   https://jrouwe.github.io/JoltPhysics/
8. Unity, `Debug.DrawLine` — https://docs.unity3d.com/ScriptReference/Debug.DrawLine.html
9. Epic Games, `DrawDebugLine` —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/DrawDebugLine
