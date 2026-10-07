# ADR-0031 — La nage, le planeur et l'endurance de *Rando*

- **Statut** : accepté le 2026-10-07, sur les réponses de Donnovan aux sondages du jour (le planeur, la jauge,
  la noyade, les animations, puis la noyade après un vol) ; relu par un subagent, dont la relecture a corrigé la
  largeur du lac, la recharge au sol, la caméra dans l'eau et le retour à la rive après un vol ; précisé le
  2026-10-07, à la clôture de M6.5 (la caméra qui coupe, son premier bras, `--glide`)
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

- le lac, à −1,5 m, fait **90 m** d'ouest en est sur la ligne z = 280 (de x = 268 à 358), **106 m** du nord au
  sud sur x = 330 (de z = 215 à 321), et 125 m au plus long, sur x ≈ 305 ; 7,5 m de fond au centre ;
- la crête ouest culmine vers 77 m, à x = 60 ; le fond de la vallée est à 0 m, de x = 180 jusqu'au lac.

Le promontoire de M6.5 est donc **la crête ouest, en (70 ; 280)**, à 73 m, face au lac : le sanctuaire et le
vrai promontoire viendront avec l'éditeur (M7.6, M8.2).

Les réponses de Donnovan au sondage, mot pour mot :

- le planeur : « Re-sauter en l'air (Recommandé) » ;
- l'endurance avant ImGui : « Une jauge à côté du renard (Recommandé) » ;
- la noyade : « Retour sur la rive d'où il est parti (Recommandé) » ;
- les animations : « Garder le renard, en attendant (Recommandé) » ;
- puis, la relecture ayant montré qu'un vol qui finit dans le lac ramenait au promontoire : « La rive la plus
  proche de l'amerrissage (Recommandé) ».

Les options 1 et 2 ci-dessous sont des choix de l'agent, sans sondage.

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
Entré dans l'eau par les airs : la rive la plus proche de l'amerrissage (**choisie**) ; le dernier point où il
avait pied, le promontoire après un vol.

**6. Les animations** : garder le renard (**choisi**) ; changer de personnage maintenant.

## Décision

### La forme

Un plugin gameplay de *Rando*, **`plugins/traversal`** (« les moyens de traverser », JEU.md), au-dessus de la
marche du moteur. Le joueur porte :

- `TraversalRules` : la marche (`character::Walker`), le planeur, la nage et l'endurance, des réglages ;
- `Traversal` : son état (`Walk`, `Glide`, `Swim`), le point où le ramène la noyade, le nombre de noyades ;
- `Stamina` : l'endurance, de 0 à 1, et s'il est épuisé ;
- le `WalkInput` de la marche, inchangé : direction, course et saut.

Les trois derniers, avec `animation::CharacterMotion`, viennent avec les réglages (le trait `With` de flecs,
comme la caméra) : un composant oublié ferait une requête qui ne correspond à rien, sans un mot (règle n°7).
Le lac est un singleton du monde, `water::Lake`, posé par le jeu (le disque et le niveau qu'il dessine déjà) ;
le module en pose un vide d'ici là, pour la même raison.

```cpp
// Dans le démarrage de Rando : la marche du moteur n'est plus importée.
world.import<rando::traversal::TraversalModule>();
world.set(lake);
player.set(FoxController).set(rando::traversal::TraversalRules{.walker = FoxWalker});
```

### Un pas

Dans la phase `Simulation`, avant le pas de physique, là où marchait le `WalkModule`, un système qui tient en
une ligne appelle `stepTraversal`. Chaque étape est une fonction libre (ADR-0011) :

1. **L'état** (`nextMode`), d'après le sol du pas précédent et la profondeur des pieds sous la surface
   (`waterDepthAt`), dans cet ordre :
   - d'abord l'eau : les pieds à plus de 0,45 m sous la surface, il **nage**, d'où qu'il vienne ;
   - de la **nage** à la marche : **au sol** (`isWalking`, pas sur une pente trop raide), les pieds à moins de
     0,3 m sous la surface ;
   - du **planeur** à la **marche** : au **premier contact** (tout sol sauf `InAir`, une paroi trop raide
     comprise), sur un nouvel appui de saut, ou épuisé ;
   - de la **marche** au **planeur** : **en l'air** (`InAir` : pas en glissant sur une pente), sur un appui de
     saut, s'il n'est pas épuisé.
2. **La vitesse**, selon l'état :
   - **marche** : `stepWalk`, la marche du moteur, telle quelle ;
   - **planeur** (`glideVelocity`) : il avance à 6 m/s dans la direction où il regarde, que l'input tourne à
     90°/s, et ne descend qu'à 2 m/s ;
   - **nage** (`swimVelocity`) : 1,8 m/s dans la direction demandée ; il flotte, les pieds à 0,4 m sous la
     surface, rejoints en 0,3 s.
3. **L'endurance** (`staminaAfter`) : le planeur et la nage la vident, la course aussi, mais seulement au sol
   et en mouvement ; elle se recharge **au sol**, en marchant ou à l'arrêt. En l'air, rien ne change : sinon,
   replier le planeur une seconde le rechargerait de 9 s de vol, et le vol ne finirait jamais.
4. **La noyade** : à zéro dans l'eau, le renard revient sur la rive, endurance pleine (choix de Donnovan) :
   - entré dans l'eau **en marchant**, au dernier point où il avait pied ;
   - entré **par les airs** (en planant, en tombant), sur le sol sec le plus proche de l'amerrissage
     (`nearestShore`) : des cercles de plus en plus larges autour du point d'entrée, et des rayons verticaux
     qui cherchent le sol au-dessus de l'eau.

   `stepTraversal` rend la pose ; la glu la pose (`set`), et le moteur téléporte le personnage (ADR-0028). Une
   écriture par référence ne téléporterait pas, et Jolt l'écraserait. L'état repasse à la marche, la jauge est
   pleine, l'épuisement levé. C'est là que M8.2 retirera un cœur.
5. **L'animation** (`animation::CharacterMotion`, M4.5) : ses champs `swimming` et `gliding`, déjà prévus.
   Sans clip de nage ni de vol, l'animateur garde la locomotion au sol : à la nage, la marche, à la vitesse de
   la nage ; en vol, une vitesse nulle, et le renard prend la pose de repos (choix de Donnovan). Le jour où un
   clip arrive, rien ne change ici.

### Les réglages de départ

À régler en jouant (M8.2), comme ceux de la caméra :

| Réglage | Départ | Ce qu'il donne dans la vallée |
|---|---:|---|
| `glide.speed`, `glide.sinkSpeed` | 6 m/s, 2 m/s | Une finesse de 3 : du promontoire (73 m), environ 225 m en 38 s, jusqu'au-dessus du lac |
| `glide.turnDegreesPerSecond` | 90°/s | Un virage large, pas le demi-tour en un quart de seconde de la marche |
| `swim.speed` | 1,8 m/s | 50 s pour traverser le lac d'ouest en est |
| `stamina.glidePerSecond` | 1/45 | 45 s de vol : la descente entière du promontoire (84 % de la jauge), mais pas de quoi nager ensuite |
| `stamina.swimPerSecond` | 1/60 | 60 s, 108 m : la traversée ouest-est (88 m de nage) en prend 81 % ; nager sur place noie en une minute |
| `stamina.runPerSecond` | 1/15 | 15 s de course |
| `stamina.refillPerSecond` | 1/5 | 5 s pour remplir la jauge |

### Les pièges, et leur nom

- **`brakeToGlide`** : ouvrir le planeur en pleine chute. La vitesse verticale freine de 40 m/s² (4 g) vers
  −2 m/s, moins d'une demi-seconde depuis 20 m/s, au lieu d'y passer d'un pas à l'autre : sans ça, la chute
  s'arrête net, comme contre un mur. L'élan vers l'avant, lui, passe de la marche aux 6 m/s du planeur à
  8 m/s².
- **`brakeInWater`** : le même mur à l'entrée dans l'eau. Une chute de 15 m/s ne devient pas d'un coup la
  remontée vers la surface : l'eau la freine de 30 m/s², et le renard s'enfonce un peu avant de remonter.
- **L'appui qui saute ne plane pas** : le saut est une impulsion, consommée au pas où il est lu (ADR-0028).
  Celui qui fait sauter ne peut pas, au pas suivant, ouvrir le planeur ; il faut un second appui. C'est le
  geste de *Breath of the Wild*.
- **Un appui, un seul pas** : `stepTraversal` consomme le saut dans **tous** les états, pas seulement dans la
  marche (dont `stepWalk` le fait déjà). Le jeu écrit le `WalkInput` une fois par image : dans une image qui
  joue deux pas (la page web à 30 images/s), le planeur s'ouvrirait et se replierait sur le même appui.
- **`enterSwimDepth` et `leaveSwimDepth`** (0,45 et 0,3 m) : deux seuils, pas un. Avec un seul, le renard qui
  flotte au ras d'une rive en pente passerait de la marche à la nage à chaque pas.
- **`lastDryFeet`** : le dernier point sec ne s'écrit qu'**au sol** (`isWalking`) et **hors de l'eau**. Un saut
  au-dessus du lac ou un vol ne le déplacent pas ; une pente trop raide non plus : on n'y reviendrait que pour
  glisser. Une entrée par les airs le remplace par `nearestShore`.
- **`exhausted`** : à zéro, le renard est épuisé jusqu'à ce que la jauge soit pleine. Il ne court plus et ne
  plane plus. Sans ce verrou, la jauge oscillerait autour de zéro, et la course reprendrait à chaque pas.
- **La caméra sous l'eau** : en nageant, le pivot est à 0,2 m au-dessus de la surface, et le bras qui descend
  vers l'arrière passerait dessous. Le moteur ne dessine rien sous l'eau, et l'image montrerait le dessous du
  plan d'eau. La caméra reçoit donc un plancher, `floorHeight` : le niveau du lac, plus la sphère de son plan
  proche (ADR-0030). Le plancher la **relève** à la verticale, et elle **vise alors le pivot** : relevée sans
  viser, à 30° de tangage, elle laisserait le renard sortir par le bas de l'image. Hors du lac, le terrain
  reste au-dessus du plancher (de 10 cm au plus bas, à −1,04 m), et la caméra au-dessus du terrain. Le jeu
  pose le plancher, comme la forme de l'image : la caméra ne connaît pas le lac.

### La caméra en vol

Le bras passe de 3,5 à **6 m** en vol, pour voir où l'on va atterrir, et revient à 3,5 m à l'atterrissage. Le
jeu pose cette longueur selon l'état, comme la forme de l'image : la caméra ne dépend pas du plugin
`traversal`. Le bras y va par le retour amorti qu'il a déjà (ADR-0030), **dans les deux sens** :
`armLengthAfter` rejoignait d'un coup une longueur voulue plus courte que le bras actuel, et il ne rentre plus
d'un coup que contre un obstacle. Le pivot et le tangage ne changent pas : c'est le « jeu de réglages par
état » de l'ADR-0030, réduit à ce qui sert.

### La jauge

Un **arc** près de la tête du renard, tourné vers la caméra. Il est dessiné avec les lignes de debug du moteur
(ADR-0027), par-dessus l'herbe (`DebugDepth::OnTop`, son étape inscrite après celle de l'herbe, qui le
couvrirait sinon), à la pose interpolée du renard. Il ne se montre que si la jauge n'est pas pleine ; vert,
rouge quand le renard est épuisé. Il marche aussi dans la page web, comme les lignes de debug. ImGui le
remplacera en M7.1, si on le souhaite.

### Le critère, vérifié en CI

Trois tests de *Rando*, sur la vraie vallée, sa collision et la vraie marche, sans GPU. Comme le renard de M6.3
(`levain_tests -tc='*renard de la démo*'`), ils jouent des milliers de pas sans rien dessiner. Le jeu complet,
lui, ne rend que quelques centaines d'images dans le temps de la CI (lavapipe).

Chacun vérifie aussi qu'il a vraiment fait ce qu'il prétend (règle n°7) : un renard qui ne nagerait jamais
marcherait au fond du lac, ne dépenserait rien, et ressortirait sur l'autre rive.

- **Le planeur** : parti du promontoire, le renard marche vers le lac et saute ; le premier appui ne l'ouvre
  pas, le second si. Il se pose (ou amerrit) plus de 150 m plus loin, après plus de 25 s de vol ; de 0,5 s
  après l'ouverture jusqu'au contact, il ne descend jamais plus vite que 2,05 m/s, mesuré sur ses pieds.
- **La traversée** : il entre dans le lac par l'ouest, sur z = 280, et ressort à l'est, au sol, sans s'être
  noyé ; il a nagé plus de 40 s, les pieds jamais à plus de 0,6 m sous la surface, et sa jauge est descendue
  sous 0,3.
- **La noyade** : il entre dans le lac par l'ouest, s'arrête au large et nage sur place. Il se noie en moins
  d'une minute de nage, et revient à moins d'un mètre du dernier point où **le test** l'a vu au sol, hors de
  l'eau : sur la rive ouest, jauge pleine.
- **La noyade après un vol** : jeté au-dessus du lac, il y tombe, se noie, et revient sur le sol sec le plus
  proche du point d'entrée, et non sur le point de départ.

Le jeu, en CI, joue en plus la descente pendant quelques centaines de pas (`--glide`, qui scripte le saut et
le second appui) ; l'étape lit dans le journal le nombre de pas en vol et de traits de la jauge, et échoue
s'ils sont nuls.

## Précisions du 2026-10-07 — clôture de M6.5

Ce que l'implémentation (*Rando* #10 à #13) et sa relecture ont fixé, sans changer de décision :

- **La caméra coupe quand le joueur est téléporté** (`targetJumped`) : après une noyade, la cible saute de
  30 m en un pas. Interpolée, la caméra traverserait la vallée en une image ; au-delà de 5 m en un pas, la
  glu remet son état précédent sur sa nouvelle pose.
- **Le premier bras de la caméra se mesure sur la heightmap** (`heightmapArmCast`, dans le jeu) : la physique
  ne crée le relief qu'au premier pas, et un sphere cast lancé au démarrage ne touche rien. Sur le versant du
  promontoire, la caméra partait dans la roche (marge −0,301 m dès la première image). L'ADR-0030 ne le
  prévoyait pas : son bras part du pivot et suppose que le relief existe. Son scénario de CI, au pied d'un
  versant, ne le montrait pas.
- **Sans point sec, la noyade en cherche un** : un joueur posé dans l'eau au départ n'a jamais eu pied ;
  `nearestShore` est appelé comme pour une entrée par les airs, sinon il flotterait sans fin à bout de forces.
- **`--glide N`** compte en pas, un par image : il est refusé sans `--steps`, où une image sans pas effacerait
  l'appui scripté avant qu'un pas le lise.
- La noyade sur place prend **une minute** de nage : 60,0 s mesurées (`rando_tests -tc='*sur place*'`).

## Conséquences

- Le jeu importe le `TraversalModule` **au lieu** du `WalkModule` : un seul système déplace le joueur. Le
  sandbox garde le `WalkModule`.
- Les chutes ne font pas de dégâts avant les cœurs (M8.2) : un vol trop ambitieux se replie, et le renard
  tombe sans conséquence.
- La nage reste en surface : ni plongée, ni nage rapide.
- Le jour où un second lac viendra, le singleton deviendra une liste : `waterDepthAt` prend déjà le lac en
  paramètre.
- `nearestShore` cherche un sol sec, pas un sol où l'on tient : sur une rive très raide, le renard
  réapparaîtrait pour glisser. La vallée n'en a pas au bord du lac (32° au plus raide, à l'est).

## Ce que font les autres moteurs

- **Unreal** : le `UCharacterMovementComponent` a des **modes de déplacement**, un seul actif à la fois, que
  l'option A reprend : `MOVE_Walking`, « Walking on a surface » ; `MOVE_Falling`, « Falling under the effects
  of gravity, such as after jumping or walking off the edge of a surface » ; `MOVE_Swimming`, « Swimming
  through a fluid volume, under the effects of gravity and buoyancy » ; `MOVE_Custom` pour le reste [1]. Le
  planeur d'un jeu Unreal est le plus souvent un `MOVE_Custom`, ou un `MOVE_Falling` à la gravité réduite
  (**déduit** : c'est l'usage, pas la documentation). L'eau est un
  `APhysicsVolume` dont `bWaterVolume` est vrai : « True if this volume contains a fluid like water » [2].
- **Unity** : le `CharacterController` ne connaît que le déplacement ; les états sont au script du joueur. Le
  plus souvent, c'est une machine à états écrite à la main, et l'eau un *trigger* (**déduit** : rien
  d'intégré).
- **Godot** : même chose avec le `CharacterBody3D` et son script. Un `Area3D` détecte l'eau, et peut changer
  la gravité de ce qui y entre (`gravity_space_override`) [3] ; un `CharacterBody3D` ne la reçoit que si son
  script la lit (`get_gravity()`, qui rend « the gravity vector computed from all sources », les `Area3D`
  comprises) [4].

Le cœur est partout le même : **un état à la fois, qui décide de la vitesse**. Ce qui distingue un jeu d'un
autre, ce sont les transitions et leurs réglages.

## Sources

1. Epic Games, *EMovementMode* — https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/EMovementMode
2. Epic Games, *APhysicsVolume* — https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/APhysicsVolume
3. Godot, *Area3D* — https://docs.godotengine.org/en/stable/classes/class_area3d.html
4. Godot, *PhysicsBody3D* — https://docs.godotengine.org/en/stable/classes/class_physicsbody3d.html
