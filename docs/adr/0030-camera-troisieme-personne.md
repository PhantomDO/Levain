# ADR-0030 — La caméra à la troisième personne de *Rando*

- **Statut** : accepté le 2026-10-06, sur les réponses de Donnovan au sondage du jour (collision, recentrage,
  souris)
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
- souris : « Souris capturée (Recommandé) ».

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
| `armLength` | 3,5 m | La longueur du bras sans obstacle, celle de la caméra de M6.3 |
| `minPitchDegrees`, `maxPitchDegrees` | −70°, 40° | On ne passe ni sous le sol ni par-dessus la tête |
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
2. **Le recentrage** : si le regard est resté à zéro pendant `recenterWaitSeconds` et que le joueur avance
   plus vite que `recenterMinSpeed`, le lacet rejoint celui du joueur par le plus court chemin, en
   s'en approchant exponentiellement. Comme le *Recentering* de Cinemachine, avec son `Wait` et son `Time` [3].
3. **Le bras** : un `physics::sphereCast` part du pivot, dans la direction de la caméra, sur `armLength`, contre
   le décor et les corps dynamiques (pas les volumes déclencheurs : le lac ne repousse pas la caméra).
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
  60° et du 16:9, c'est 1,55 fois le plan proche. Une sphère plus petite laisserait les coins de l'image entrer
  dans la roche, c'est exactement ce que le critère interdit. Le rayon se recalcule quand la fenêtre change de
  forme (un téléphone en portrait, ADR-0023).
- **Le plan proche passe de 0,5 à 0,2 m** pour cette caméra : avec 0,5 m, la sphère ferait 0,77 m, et la caméra
  ne s'approcherait jamais à moins de 77 cm d'une paroi. Avec 0,2 m, elle fait 0,31 m. Le prix est la précision
  de la profondeur au loin (un depth buffer non inversé, `D32`) : la PR de la caméra capture la vallée à 0,2 et
  à 0,5 m pour vérifier qu'aucun scintillement n'apparaît au bord du lac ou sur les crêtes. S'il en apparaît, la
  profondeur inversée (*reversed-Z*) sera une issue du moteur.
- **`shortestYawDelta(from, to)`** : recentrer de 350° à 10° fait 20° dans un sens, pas 340° dans l'autre.
- **Un pivot dans la roche** : sous un surplomb bas, le *sphere cast* touche à la distance 0. La caméra se
  pose alors sur le pivot, sans traverser ; c'est l'endroit où le renard lui-même remplit l'image, un défaut
  connu de toutes les caméras de ce type (Unreal masque alors le personnage). Masquer le renard quand la caméra
  est trop près sera une issue de M6.5, si la vallée en montre le besoin.

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

Une **marge au relief**, à chaque image : la hauteur des quatre coins du plan proche et de son centre au-dessus
du terrain (`terrain::heightAt`), à la pose rendue, donc interpolée. Le relief de la vallée n'a ni surplomb ni
grotte : passer sous lui, c'est traverser la roche.

- **Le scénario** : le renard longe le pied d'un versant (`--walk`), pendant que la caméra fait un tour complet
  autour de lui toutes les 4 s (un regard scripté, `--orbit`). Toutes les directions du bras passent donc
  contre la pente.
- **Le contrôle qui mord** (règle n°7) : le même scénario, la collision coupée (`--camera-collision off`), doit
  donner une marge **négative**. Sinon, le scénario ne longe pas assez la paroi pour prouver quoi que ce soit, et
  l'étape échoue.
- **Avec la collision**, la marge minimale doit rester positive. La CI lit la ligne « caméra : marge minimale au
  relief … » du journal.

## Conséquences

- La caméra ne connaît ni le renard ni la vallée : un `target`, un `CameraLens`, des réglages. Le jour où un
  second jeu la réclamera, elle remontera dans un plugin moteur (ADR-0018), sans changer de forme.
- La caméra suit le **pas fixe**, comme la caméra libre : son regard a au plus un pas de retard (16,7 ms) sur la
  souris, plus l'interpolation. Si la latence se sent sur un écran à 144 Hz, l'orbite passera à l'image ; le
  bras et sa collision resteront au pas.
- La direction de marche se tourne selon le **lacet de la caméra** : « avant » est là où elle regarde. La glu
  du jeu qui remplit le `WalkInput` lit ce lacet. Le `walkDirectionOf` du sandbox, figé sur +x, reste au sandbox.
- Le sandbox garde sa caméra qui suit, sans collision : la caméra de *Rando* ne descend pas dans le moteur.

## Ce que font les autres moteurs

- **Unreal**, `USpringArmComponent` : « This component tries to maintain its children at a fixed distance from
  the parent, but will retract the children if there is a collision, and spring back when there is no
  collision » [1]. La collision est un *sphere cast* : `bDoCollisionTest`, « do a collision test using
  ProbeChannel and ProbeSize to prevent camera clipping into level », la sphère de `ProbeSize` (12 unités,
  12 cm, dans son constructeur, d'après le code source). Le lissage est optionnel et symétrique (`bEnableCameraLag`,
  `CameraLagSpeed`) ; le recentrage n'est pas dans le bras, mais dans le `PlayerController`.
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
