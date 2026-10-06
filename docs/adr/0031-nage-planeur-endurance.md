# ADR-0031 — La nage, le planeur et l'endurance de *Rando*

- **Statut** : accepté le 2026-10-07, sur les réponses de Donnovan au sondage du jour (le planeur, la jauge,
  la noyade, les animations)
- **Date** : 2026-10-07
- **Milestone** : M6.5

## Contexte

Le critère de M6.5 : **descendre du promontoire en planant, traverser le lac à la nage, et se noyer si
l'endurance s'épuise** (ROADMAP). Les règles viennent de la page de game design de *Rando* (`docs/JEU.md`) :

- le planeur se déploie en l'air : chute lente, poussée vers l'avant ; il consomme de l'endurance et se replie
  à vide ;
- la nage, dans l'eau du lac, consomme de l'endurance ; à vide, c'est la noyade ;
- la course consomme de l'endurance ; la jauge se recharge au sol, à l'arrêt ou en marchant.

Ce sont des **états du joueur au-dessus du character controller** (M6.3) : un plugin gameplay, dans le dépôt
du jeu (ADR-0018). La marche (`plugins/character` du moteur, ADR-0028) reste ce qu'elle est.

Ce qui manque encore, et que M6.5 contourne sans le construire :

- les cœurs et les points de contrôle (M8.2) ;
- l'interface (ImGui, M7.1) ;
- des animations de nage et de vol : le renard n'a que repos, marche et course.

La vallée, mesurée sur sa heightmap (graine 2026) :

- le lac, à −1,5 m, fait **90 m** d'ouest en est sur la ligne z = 280 (de x = 268 à 358), et **130 m** du nord
  au sud sur x = 330 (de z = 209 à 339) ; 7,5 m de fond au centre ;
- la crête ouest culmine vers 77 m, à x = 60 ; le fond de la vallée est à 0 m, de x = 180 jusqu'au lac.

Le promontoire de M6.5 est donc **la crête ouest, en (70 ; 280)**, face au lac : le sanctuaire et le vrai
promontoire viendront avec l'éditeur (M7.6, M8.2).

Les réponses de Donnovan au sondage, mot pour mot :

- le planeur : « Re-sauter en l'air (Recommandé) » ;
- l'endurance avant ImGui : « Une jauge à côté du renard (Recommandé) » ;
- la noyade : « Retour sur la rive d'où il est parti (Recommandé) » ;
- les animations : « Garder le renard, en attendant (Recommandé) ».

## Options envisagées

**1. Comment les états se partagent le personnage**

| Option | Pour | Contre |
|---|---|---|
| **A. Un seul système, qui choisit selon l'état : marcher (`stepWalk` du moteur), planer ou nager** | Un seul écrivain de la vitesse ; les transitions au même endroit ; les *movement modes* d'Unreal | Le jeu n'importe plus le `WalkModule` : le plugin appelle la marche lui-même |
| B. Un système par état, la marche coupée en retirant son `Walker` | Chaque état isolé | Un changement de structure à chaque transition ; le réglage de la marche à garder ailleurs pour le remettre |
| C. La marche tourne toujours, le planeur et la nage écrasent sa vitesse | Rien à changer à la marche | Deux écrivains ; la marche consomme le saut, que le planeur doit lire avant elle |

**2. Où est l'eau**

| Option | Pour | Contre |
|---|---|---|
| **A. Le lac lui-même : un disque et un niveau (`water::Lake`)** | Une fonction libre, testée sans physique ; la profondeur sous la surface en sort directement | Ne connaît que les lacs plats |
| B. Un volume déclencheur de Jolt (M6.2) | Le « volume d'eau » d'Unreal | Dit « dedans », pas « à quelle profondeur » : il faudrait le niveau quand même |

**3. Le planeur** : re-sauter en l'air (**choisi**) ; maintenir saut ; une touche à part.

**4. L'endurance avant ImGui** : une jauge à côté du renard (**choisie**) ; dans le titre de la fenêtre ; rien.

**5. La noyade** : retour sur la rive d'où il est parti (**choisi**) ; au départ ; sur la rive la plus proche.

**6. Les animations** : garder le renard (**choisi**) ; changer de personnage maintenant.

## Décision

### La forme

Un plugin gameplay de *Rando*, **`plugins/traversal`** (« les moyens de traverser », JEU.md), au-dessus de la
marche du moteur. Le joueur porte :

- `Traversal` : son état (`Walk`, `Glide`, `Swim`), le dernier point sec, et s'il est épuisé ;
- `Stamina` : l'endurance, de 0 à 1 ;
- `TraversalRules` : la marche (`character::Walker`), le planeur, la nage et l'endurance, des réglages ;
- le `WalkInput` de la marche, inchangé : direction, course et saut.

Le lac est un singleton du monde, `water::Lake`, posé par le jeu (le disque et le niveau qu'il dessine déjà).

```cpp
// Dans le démarrage de Rando, une ligne par entité.
world.import<rando::traversal::TraversalModule>(); // à la place du WalkModule
world.set(lake);
player.set(rando::traversal::TraversalRules{.walker = FoxWalker}).add<rando::traversal::Traversal>();
```

### Un pas

Dans la phase `Simulation`, avant le pas de physique, là où marchait le `WalkModule`, un système qui tient en
une ligne appelle `stepTraversal`. Chaque étape est une fonction libre (ADR-0011) :

1. **L'état** (`nextMode`) :
   - de la **marche** au **planeur** : en l'air, sur un appui de saut, si l'endurance le permet ;
   - du **planeur** à la **marche** : au sol, sur un nouvel appui de saut, ou l'endurance à zéro ;
   - de la marche ou du planeur à la **nage** : les pieds à plus de 0,45 m sous la surface ;
   - de la **nage** à la marche : au sol, les pieds à moins de 0,3 m sous la surface.
2. **La vitesse**, selon l'état :
   - **marche** : `stepWalk`, la marche du moteur, telle quelle ;
   - **planeur** (`glideVelocity`) : il avance à 6 m/s dans la direction où il regarde, que l'input tourne à
     90°/s, et ne descend qu'à 2 m/s ;
   - **nage** (`swimVelocity`) : 1,8 m/s dans la direction demandée ; il flotte, les pieds à 0,4 m sous la
     surface, rejoints en 0,3 s.
3. **L'endurance** (`staminaAfter`) : la course, le planeur et la nage la vident ; elle se recharge en
   marchant ou à l'arrêt.
4. **La noyade** : à zéro dans l'eau, le renard revient au **dernier point sec**, endurance pleine (choix de
   Donnovan). Un `Transform` posé : le moteur téléporte le personnage (ADR-0028). C'est là que M8.2 retirera
   un cœur.
5. **L'animation** (`animation::CharacterMotion`) : à la nage, sa vitesse, et l'animation de marche suit ; en
   vol, zéro, et le renard prend la pose de repos (choix de Donnovan).

### Les réglages de départ

À régler en jouant (M8.2), comme ceux de la caméra :

| Réglage | Départ | Ce qu'il donne dans la vallée |
|---|---:|---|
| `glide.speed`, `glide.sinkSpeed` | 6 m/s, 2 m/s | Une finesse de 3 : du promontoire (75 m), environ 220 m, jusqu'au-dessus du lac |
| `glide.turnDegreesPerSecond` | 90°/s | Un virage large, pas le demi-tour en un quart de seconde de la marche |
| `swim.speed` | 1,8 m/s | 50 s pour traverser le lac d'ouest en est |
| `stamina.glidePerSecond` | 1/45 | 45 s de vol : la descente entière du promontoire, mais pas de quoi nager ensuite |
| `stamina.swimPerSecond` | 1/60 | 108 m : la traversée ouest-est (90 m) passe, la nord-sud (130 m) noie |
| `stamina.runPerSecond` | 1/15 | 15 s de course |
| `stamina.refillPerSecond` | 1/5 | 5 s pour remplir la jauge |

### Les pièges, et leur nom

- **`brakeToGlide`** : ouvrir le planeur en pleine chute. La vitesse verticale rejoint −2 m/s en une demi-seconde,
  au lieu d'y passer d'un pas à l'autre : sans ça, une chute de 20 m/s s'arrête net, comme contre un mur.
- **L'appui qui saute ne plane pas** : le saut est une impulsion, consommée au pas où il est lu (ADR-0028).
  Celui qui fait sauter ne peut pas, au pas suivant, ouvrir le planeur ; il faut un second appui. C'est le
  geste de *Breath of the Wild*.
- **`enterSwimDepth` et `leaveSwimDepth`** (0,45 et 0,3 m) : deux seuils, pas un. Avec un seul, le renard qui
  flotte au ras d'une rive en pente passerait de la marche à la nage à chaque pas.
- **`lastDryFeet`** : le dernier point sec ne s'écrit qu'**au sol** et **hors de l'eau**. Un saut au-dessus du
  lac ou un vol ne le déplacent pas : on revient là où on avait pied.
- **`exhausted`** : à zéro, le renard est épuisé jusqu'à ce que la jauge soit pleine. Il ne court plus et ne
  plane plus. Sans ce verrou, la jauge oscillerait autour de zéro, et la course reprendrait à chaque pas.
- **La caméra sous l'eau** : en nageant, le bras qui descend vers l'arrière passerait sous la surface. Le
  moteur ne dessine rien sous l'eau, et l'image montrerait le dessous du plan d'eau. La caméra reçoit donc un
  plancher, `floorHeight` : le niveau du lac, plus la sphère de son plan proche (ADR-0030). Hors du lac, le
  terrain est toujours au-dessus de ce niveau, et la caméra au-dessus du terrain : le plancher ne gêne
  jamais.

### La caméra en vol

Le bras passe de 3,5 à **6 m** en vol, pour voir où l'on va atterrir. Il y va par le retour amorti qu'il a déjà
(ADR-0030), sans rien d'autre. Le pivot et le tangage ne changent pas : c'est le « jeu de réglages par état »
de l'ADR-0030, réduit à ce qui sert.

### La jauge

Un **arc** près de la tête du renard, tourné vers la caméra. Il est dessiné avec les lignes de debug du moteur
(ADR-0027), par-dessus l'herbe (`DebugDepth::OnTop`). Il ne se montre que si la jauge n'est pas pleine ; vert,
rouge quand le renard est épuisé. Il marche aussi dans la page web, comme les lignes de debug. ImGui le
remplacera en M7.1, si on le souhaite.

### Le critère, vérifié en CI

Trois tests de *Rando*, sur la vraie vallée, sa collision et la vraie marche, sans GPU. Comme le renard de M6.3
(`levain_tests -tc='*renard de la démo*'`), ils jouent des milliers de pas sans rien dessiner. Le jeu complet,
lui, ne rend que quelques centaines d'images dans le temps de la CI (lavapipe).

- **Le planeur** : parti du promontoire, le renard marche vers le lac, saute, ouvre le planeur, et se pose (ou
  amerrit) plus de 150 m plus loin, après plus de 25 s en l'air, sans jamais descendre plus vite que 2 m/s
  une fois le freinage fini.
- **La traversée** : il entre dans le lac par l'ouest et ressort à l'est, au sol, sans s'être noyé.
- **La noyade** : il entre dans le lac par le sud et nage vers le nord. Il se noie avant l'autre rive, et
  revient à moins d'un mètre du dernier point sec, endurance pleine.

Le jeu, en CI, joue en plus la descente pendant quelques centaines de pas : le planeur ouvert, la jauge
dessinée, le journal le dit.

## Conséquences

- Le jeu importe le `TraversalModule` **au lieu** du `WalkModule` : un seul système déplace le joueur. Le
  sandbox garde le `WalkModule`.
- Les chutes ne font pas de dégâts avant les cœurs (M8.2) : un vol trop ambitieux se replie, et le renard
  tombe sans conséquence.
- La nage reste en surface : ni plongée, ni nage rapide.
- Le jour où un second lac viendra, le singleton deviendra une liste : `waterDepthAt` prend déjà le lac en
  paramètre.

## Ce que font les autres moteurs

- **Unreal** : le `UCharacterMovementComponent` a des **modes de déplacement**, un seul actif à la fois, que
  l'option A reprend : `MOVE_Walking`, « Walking on a surface » ; `MOVE_Falling`, « Falling under the effects
  of gravity, such as after jumping or walking off the edge of a surface » ; `MOVE_Swimming`, « Swimming
  through a fluid volume, under the effects of gravity and buoyancy » ; `MOVE_Custom` pour le reste [1]. Le
  planeur d'un jeu Unreal est un `MOVE_Custom`, ou un `MOVE_Falling` à la gravité réduite. L'eau est un
  `APhysicsVolume` dont `bWaterVolume` est vrai : « True if this volume contains a fluid like water » [2].
- **Unity** : le `CharacterController` ne connaît que le déplacement ; les états sont au script du joueur. Le
  plus souvent, c'est une machine à états écrite à la main, et l'eau un *trigger* (**déduit** : rien
  d'intégré).
- **Godot** : même chose avec le `CharacterBody3D` et son script. Un `Area3D` détecte l'eau, et peut même
  changer la gravité de ce qui y entre (`gravity_space_override`) [3].

Le cœur est partout le même : **un état à la fois, qui décide de la vitesse**. Ce qui distingue un jeu d'un
autre, ce sont les transitions et leurs réglages.

## Sources

1. Epic Games, *EMovementMode* — https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/EMovementMode
2. Epic Games, *APhysicsVolume* — https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/APhysicsVolume
3. Godot, *Area3D* — https://docs.godotengine.org/en/stable/classes/class_area3d.html
