# ADR-0030 — La caméra à la troisième personne de *Rando*

- **Statut** : accepté le 2026-10-06, sur les réponses de Donnovan aux sondages du jour (collision, recentrage,
  souris, puis le regard vers le haut) ; relu par un subagent, dont la relecture a ajouté la marge de la
  sphère, son masque, et la vérification entre deux pas ; précisé le 2026-10-06, à la clôture de M6.4 (le
  scénario de la CI, le cône du recentrage et le plan proche)
- **Date** : 2026-10-06
- **Milestone** : M6.4

## Contexte

Le critère de M6.4 : **la caméra ne traverse jamais la roche en longeant une paroi de la vallée**. Le joueur
doit aussi pouvoir la tourner autour du renard, et elle doit se replacer derrière lui (ROADMAP, JEU.md). La
caméra est un **plugin gameplay de *Rando*** (ADR-0018) : ses réglages sont du game feel. Elle tourne au-dessus
du module `app` (ADR-0029), qui la rend par son `CameraLens`.

Aujourd'hui, la vue `hike` du sandbox place la caméra à distance fixe derrière le joueur, sans collision
(`followCamera`, M6.3), et elle entre dans le versant dès que le renard s'en approche de dos.

Les réponses de Donnovan au sondage, mot pour mot :

- quand un rocher coupe le bras : « Sphère, rentre vite, ressort doucement (Recommandé) » ;
- recentrage : « En marchant, après 1,5 s sans regard (Recommandé) » ;
- souris : « Souris capturée (Recommandé) » ;
- en levant les yeux, la caméra touche vite le sol derrière le renard : « Elle glisse sur le sol
  (Recommandé) », plutôt qu'un regard borné à l'horizontale.

## Options envisagées

**1. La collision**

| Option | Pour | Contre |
|---|---|---|
| **A. Un *sphere cast* du pivot vers la caméra ; elle rentre aussitôt, ressort en douceur** | Le plan proche ne touche jamais la roche ; pas de saut d'avant en arrière derrière une arête | Deux réglages de plus (le rayon de la sphère, le temps du retour) |
| B. Le même *sphere cast*, sans lissage | Simple | La caméra saute d'avant en arrière derrière les arbres et les arêtes |
| C. Un rayon seul | Moins cher | Le plan proche entre dans la roche le long d'une arête : le critère serait fragile |

**2. Le recentrage**

| Option | Pour | Contre |
|---|---|---|
| **A. En marchant, après 1,5 s sans regard** | On ne lutte jamais contre la caméra ; à l'arrêt, elle reste où on l'a mise | Deux réglages (l'attente, la durée) |
| B. Jamais, une touche recentre | Le joueur décide toujours | Une main de plus sur la caméra en marchant |
| C. Toujours, avec du retard | La plus simple à piloter | Le regard libre ne dure qu'un instant |

**3. La souris**

| Option | Pour | Contre |
|---|---|---|
| **A. Capturée : elle tourne la caméra sans bouton** | Le geste de tous les jeux à la troisième personne sur PC | Échap pour la libérer ; dans le navigateur, un clic pour la reprendre (*pointer lock*) |
| B. Bouton droit enfoncé | La caméra libre d'aujourd'hui | Une main crispée sur le bouton pendant toute la marche |

## Décision

### La forme

Un **bras** part d'un **pivot** au-dessus des pieds du joueur ; la caméra est au bout, et regarde le pivot.

```cpp
// Le joueur et sa caméra, dans le démarrage de Rando : la glu tient en une ligne par entité.
world.import<rando::camera::ThirdPersonCameraModule>();
world.entity("camera")
    .set(levain::app::CameraLens{.verticalFovDegrees = 60.0f, .nearPlane = 0.2f, .farPlane = 1000.0f})
    .set(rando::camera::ThirdPersonCamera{.target = player});
```

Les réglages, dans `ThirdPersonCamera`, avec leurs valeurs de départ (à régler en jouant) :

| Réglage | Départ | Rôle |
|---|---:|---|
| `pivotHeight` | 0,6 m | Le pivot au-dessus des pieds : le dos d'un renard de 0,79 m |
| `armLength` | 3,5 m | La longueur du bras sans obstacle ; la caméra de M6.3 était à 3,7 m de sa cible |
| `minPitchDegrees`, `maxPitchDegrees` | −70°, 30° | Vers le bas, pas par-dessus la tête ; vers le haut, la caméra glisse sur le sol (au-delà d'environ 5° sur sol plat, le bras touche le sol et rentre) et lève les yeux vers les crêtes |
| `lookDegreesPerUnit` | 1 | Comme la caméra libre (ADR-0017) |
| `returnSeconds` | 0,4 s | La constante de temps du retour, une fois l'obstacle dégagé |
| `recenterWaitSeconds` | 1,5 s | Sans regard, le temps avant le recentrage |
| `recenterSeconds` | 1,0 s | La constante de temps du recentrage |
| `recenterMinSpeed` | 0,5 m/s | En dessous, le joueur est à l'arrêt : pas de recentrage |

L'orientation de la caméra se garde en **lacet et tangage**, la source, comme la caméra libre (`FpsController`) :
la rotation de son `Transform` en est calculée à chaque pas.

### Un pas de caméra

Dans la phase `PostPhysics` (ADR-0026), après que le joueur a bougé, chaque pas :

1. **Le regard** : les axes `look_right` et `look_up` de l'input du joueur, posé par `app` en singleton
   (ADR-0029), tournent le lacet et le tangage, bornés (`clampPitch`, déjà dans `scene`).
2. **Le recentrage** : si le regard est resté à zéro pendant `recenterWaitSeconds`, que le joueur avance plus
   vite que `recenterMinSpeed` **et qu'il s'éloigne de la caméra** (sa marche à moins de 45° de son regard), le
   lacet rejoint celui du joueur par le plus court chemin, en s'en approchant exponentiellement. Comme le
   *Recentering* de Cinemachine, avec son `Wait` et son `Time` [3].
3. **Le bras** : un `physics::sphereCast` part du pivot, dans la direction de la caméra, sur `armLength`, avec
   le masque `maskOf({Layer::Static, Layer::Dynamic})` : le décor et les corps dynamiques. Ni les volumes
   déclencheurs (le lac ne repousse pas la caméra), ni la couche `Character`, que le masque par défaut
   (`SolidLayers`) contient : le pivot est **dans** la capsule du renard (0,6 m, pour un sommet à 0,8 m), et
   une sphère qui part de l'intérieur d'un corps et avance vers lui le touche à la distance 0 (queries.hpp) ;
   la caméra s'écraserait sur le pivot dès qu'on lève les yeux. La documentation de Godot donne le même
   conseil pour son `SpringArm3D` (exclure le corps du joueur).
   - **Touché à une distance plus courte que le bras actuel** : le bras prend cette distance **tout de suite**.
   - **Sinon** : il revient vers `armLength`, exponentiellement, avec la constante `returnSeconds`. Le retour
     ne dépasse jamais la distance libre que le *sphere cast* vient de trouver.
4. **La pose** : la caméra au bout du bras, regardant le pivot. Elle porte un `PreviousTransform` : le rendu
   l'interpole entre deux pas (ADR-0016), comme celle d'aujourd'hui.

Chacune de ces étapes est une fonction libre, testée seule (ADR-0011), et le système qui les enchaîne tient en
une ligne.

### Les pièges, et leur nom

- **`nearPlaneRadius(lens, aspect)`** : la sphère doit contenir le **plan proche** de la caméra, pas seulement
  son centre. Le coin de ce plan est à `near × √(1 + tan²(fov/2) × (1 + aspect²))` du centre optique : avec
  60° et du 16:9, c'est 1,54 fois le plan proche. Une sphère plus petite laisserait les coins de l'image entrer
  dans la roche, c'est exactement ce que le critère interdit. Le rayon se recalcule quand la fenêtre change de
  forme (un téléphone en portrait, ADR-0023).
- **Une marge de 5 cm** s'ajoute à ce rayon : la collision (les triangles du heightfield de Jolt), l'image (ceux
  du GPU) et la mesure de la CI (`heightAt`, bilinéaire) ne voient pas exactement le même relief. Sans marge,
  les coins de l'image toucheraient la roche au contact, et la CI réussirait ou échouerait au hasard.
- **Le plan proche passe de 0,5 à 0,2 m** pour cette caméra : avec 0,5 m, la sphère ferait 0,82 m, et la caméra
  ne s'approcherait jamais à moins de 82 cm d'une paroi. Avec 0,2 m, elle fait 0,36 m. Le prix : la précision de
  la profondeur au loin, et deux réglages qui partent du plan proche, les **cascades d'ombres**
  (`cascadeSplitsOf`, en répartition logarithmique) et les **tranches des clusters** de lumières (ADR-0024). La
  PR de la caméra capture la vallée à 0,2 et à 0,5 m et compare le bord du lac, les crêtes et les ombres. Si un
  écart se voit, deux issues du moteur : la profondeur inversée (*reversed-Z*), et des cascades réglées par
  leur propre distance plutôt que par le plan proche.
- **`shortestYawDelta(from, to)`** : recentrer de 350° à 10° fait 20° dans un sens, pas 340° dans l'autre. Le
  `turnTowards` du plugin `character` fait déjà ce calcul en privé : la fonction descend dans `scene`, à côté
  de `clampPitch`, et les deux s'en servent.
- **Le recentrage qui tourne en rond** : tenir « droite » fait marcher le renard vers la droite de la caméra ;
  recentrée derrière lui, la caméra tourne, la droite tourne avec elle, et le renard décrit un cercle. D'où la
  condition « s'éloigne de la caméra » : de profil ou face à elle, il n'y a pas de recentrage.
- **Un pivot dans la roche** : sous un surplomb bas, le *sphere cast* touche à la distance 0. La caméra se
  pose alors sur le pivot, sans traverser ; c'est l'endroit où le renard lui-même remplit l'image. Le masquer
  quand la caméra est trop près sera une issue de M6.5, si la vallée en montre le besoin.

### La souris

*Rando* capture la souris au démarrage : elle tourne la caméra sans bouton. **Échap la libère, un clic dans la
fenêtre la reprend**, deux actions de son `input.cfg` (`free_mouse`, `capture_mouse`). Dans le navigateur, le
*pointer lock* ne s'obtient que dans un geste de l'utilisateur : le clic qui reprend la souris est aussi celui
qui la capture la première fois. À la manette, le stick droit suffit.

### Le cadrage du vol plané

Il attend le planeur (M6.5) : la caméra aura alors un **jeu de réglages par état du joueur** (le bras plus long,
le pivot plus haut, le tangage abaissé en vol), et passera de l'un à l'autre en douceur. Rien n'est écrit pour
lui en M6.4 ; la forme ci-dessus le permet sans rien casser.

### Le critère, vérifié en CI

Une **marge au relief** : la hauteur des quatre coins du plan proche et de son centre au-dessus du terrain
(`terrain::heightAt`). Le relief de la vallée n'a ni surplomb ni grotte : passer sous lui, c'est traverser la
roche.

- **Entre deux pas** : la CI joue un pas par image (`--steps`), et le rendu n'y voit donc jamais une pose
  interpolée. Or c'est là qu'est le risque : le bras rentre d'un coup, et sur une arête convexe, le segment
  entre l'ancienne et la nouvelle pose peut passer sous la roche. La mesure évalue donc, à chaque pas, la pose
  interpolée entre le `PreviousTransform` et le `Transform` aux fractions 0, ¼, ½ et ¾, comme la verrait une
  image tombée entre les deux.
- **Le scénario** : le renard longe le pied d'un versant (`--walk`, une direction **dans le monde**, que la
  caméra ne tourne pas), pendant que la caméra fait un tour complet autour de lui toutes les 4 s et que son
  tangage va d'une borne à l'autre (un regard scripté, `--orbit`). Toutes les directions du bras passent donc
  contre la pente, la caméra basse comprise, le pire cas.
- **Le contrôle qui mord** (règle n°7) : le même scénario, la collision coupée (`--camera-collision off`), doit
  donner une marge **négative**. Sinon, le scénario ne longe pas assez la paroi pour prouver quoi que ce soit, et
  l'étape échoue.
- **Avec la collision**, la marge minimale doit rester d'au moins **2 cm**. La CI lit la ligne « caméra : marge
  minimale au relief … » du journal.

## Conséquences

- La caméra ne connaît ni le renard ni la vallée : un `target`, un `CameraLens`, des réglages. Le jour où un
  second jeu la réclamera, elle remontera dans un plugin moteur (ADR-0018), sans changer de forme.
- La caméra suit le **pas fixe**, comme la caméra libre : son regard a au plus un pas de retard (16,7 ms) sur la
  souris, plus l'interpolation. Si la latence se sent sur un écran à 144 Hz, l'orbite passera à l'image ; le
  bras et sa collision resteront au pas.
- La direction de marche se tourne selon le **lacet de la caméra** : « avant » est là où elle regarde. La glu
  du jeu qui remplit le `WalkInput` lit ce lacet ; `--walk` la remplace par une direction du monde, pour la CI.
  Le `walkDirectionOf` du sandbox, figé sur +x, reste au sandbox.
- Le sandbox garde sa caméra qui suit, sans collision : la caméra de *Rando* ne descend pas dans le moteur.

## Précisions du 2026-10-06 — clôture de M6.4

Ce que l'implémentation (*Rando* #7 et #8) a fixé, sans changer de décision :

- **Le scénario de la CI** : le renard part du pied du versant ouest, à (176 ; 250), et marche vers +z
  (`--start 176,250 --walk 0,1 --steps 300`). La caméra fait un tour **en 2 s** (`--orbit 180`), et non en 4 s :
  plus sévère, puisque le bras change de direction deux fois plus vite. Le tangage fait un aller-retour d'une
  borne à l'autre en 7 s ; les deux périodes (2 et 7 s) ne se calent pas l'une sur l'autre.
- **« S'éloigne de la caméra »** se lit comme une marche à moins de **40°** du regard de la caméra
  (`recenterMaxDegrees`) : la diagonale du clavier, à 45°, ne recentre pas, ce qui évite le cercle décrit plus
  haut.
- **Le plan proche à 0,2 m** ne demande ni profondeur inversée ni cascades réglées autrement : entre 0,2 et
  0,5 m, 179 pixels de la vallée diffèrent de plus de 8 niveaux, aux bords des ombres, sans scintillement. Les
  deux issues prévues ne sont pas ouvertes.

## Ce que font les autres moteurs

- **Unreal**, `USpringArmComponent` : « This component tries to maintain its children at a fixed distance from
  the parent, but will retract the children if there is a collision, and spring back when there is no
  collision » [1]. La collision est un *sphere cast* : `bDoCollisionTest`, « do a collision test using
  ProbeChannel and ProbeSize to prevent camera clipping into level », la sphère de `ProbeSize` (12 unités,
  12 cm, dans son constructeur, d'après le code source). Son *camera lag* (`bEnableCameraLag`, « camera lags
  behind target position to smooth its movement ») lisse le suivi de la cible, pas la collision : d'après le
  code source, le bras prend le point touché aussitôt, dans les deux sens. Unreal ne fournit pas de recentrage :
  un jeu l'écrit lui-même.
- **Unity**, Cinemachine : l'extension *Deoccluder* a les deux amortissements que nous choisissons, `Damping`,
  « How quickly to return the camera to its normal position after an occlusion has gone away », et `Damping When
  Occluded`, « How quickly to move the camera to avoid an obstacle », plus un `Camera Radius`, « Distance to
  maintain from any obstacle » [2]. Le recentrage est celui de l'*Orbital Follow* : « it will wait this many
  seconds after the last user input before beginning the recentering process » [3].
- **Godot**, `SpringArm3D` : « casts a ray or a shape along its Z axis and moves all its direct children to the
  collision point, with an optional margin » [4]. Pas de lissage intégré : il s'écrit dans le script de la
  caméra.

Les trois font la même chose au cœur, **un bras qui rentre contre ce qu'il touche**. Ce qui distingue une
caméra d'une autre, c'est ce qu'on met autour : le lissage, le recentrage, la marge. Nous prenons l'asymétrie de
Cinemachine et la sphère d'Unreal, avec un rayon calculé plutôt que réglé à la main.

## Sources

1. Epic Games, *USpringArmComponent* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USpringArmComponent
2. Unity, *Cinemachine Deoccluder* —
   https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineDeoccluder.html
3. Unity, *Cinemachine Orbital Follow* —
   https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineOrbitalFollow.html
4. Godot, *SpringArm3D* — https://docs.godotengine.org/en/stable/classes/class_springarm3d.html
