# Plugin `character`

La marche du personnage (M6.3, [ADR-0028](../../docs/adr/0028-personnage.md)), au-dessus du `CharacterVirtual`
du module `physics`. C'est le premier plugin moteur qui n'est pas du level design : une brique de gameplay que
le sandbox et *Rando* reprennent (ADR-0018, amendé). La nage et le planeur de *Rando* (M6.5) seront des états
au-dessus de lui, dans le dépôt du jeu.

## Rôle

Le moteur déplace le personnage à la vitesse qu'on lui donne ; ce plugin décide de cette vitesse pour un
personnage qui marche :

- **la gravité**, ajoutée à chaque pas, même au sol, comme dans l'exemple de Jolt : elle le garde plaqué, et
  le fait glisser sur une pente trop raide ;
- **le saut**, qui ne part que du sol, et pas une seconde fois tant qu'il monte ;
- **l'accélération** vers la vitesse demandée, de marche ou de course, réduite en l'air (`airControl`) ;
- **la vitesse du sol** : sur une plateforme, « immobile » veut dire « à la vitesse de la plateforme » ;
- **l'élan en l'air** : sans rien demander, il garde sa vitesse horizontale, celle d'une plateforme dont il a
  sauté comprise (le `BrakingDecelerationFalling` nul d'Unreal) ; en demandant, il la corrige à 30 % de son
  accélération. Un choix de game feel, à revoir avec *Rando* ;
- **le plafond** : en montant, une arche l'arrête net (`bumpedHead`), au lieu de le garder collé dessous ;
- **la rotation** vers la marche, par le plus court chemin, à `turnDegreesPerSecond` ;
- **ce que lit l'animation** (`animation::CharacterMotion`, M4.5) : la vitesse effective par rapport au sol.

## Invariants

- **Le gameplay donne une direction dans le monde**, sur le plan horizontal, déjà tournée selon la caméra
  (`WalkInput`). Le plugin ne connaît ni la caméra ni les touches.
- **Le saut est une impulsion**, consommée au pas où la marche la lit.
- **La rotation s'écrit par référence** dans le `Transform` : un `set` téléporterait le personnage.
- **Les vitesses de `Walker` sont en m/s**, comme celles de la `Locomotion` de son animation : les pieds ne
  glissent pas si les deux concordent.
- Le plugin dépend d'`animation`, de `physics` et de `scene`, déclarés (`levain_add_plugin`, ADR-0018).
- **Décrits pour l'éditeur** (ADR-0034, `describeWalkComponents`, qu'appelle `WalkModule`, et *Rando* sans
  lui) : `Walker` en donnée d'auteur, `WalkInput`, reposé par le jeu à chaque pas, en lecture seule.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/levain/character/walk.hpp`](include/levain/character/walk.hpp) | `WalkInput`, `Walker`, `walkVelocity`, `turnTowards`, `motionOf`, `stepWalk`, `WalkModule` |

Les tests : `tests/walk_test.cpp`. La démo : `levain_sandbox --view character`, le renard dans Sponza, mené
au clavier (ZQSD ou WASD, espace pour sauter) ; `--walk x,z --steps N` le fait marcher seul pendant N pas,
pour la CI. `--view hike` : le même renard dans la vallée du terrain, la vue par défaut de la page web
(Sponza ne peut pas y être publiée).

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | `UCharacterMovementComponent` | Dans le moteur, avec les modes nage et vol ; Levain sépare le déplacement (moteur) de la marche (plugin) (**documenté**, ADR-0028). |
| **Unity** | `CharacterController` et le script du joueur | Le script calcule la vitesse, gravité comprise ; le *Starter Assets* d'Unity en fournit un, à recopier (**déduit** de sa distribution comme exemple). |
| **Godot** | `CharacterBody3D` et son script | Le modèle de script de Godot fait exactement ce travail : gravité, saut, vitesse (**documenté** : le modèle « CharacterBody3D: Basic Movement » de l'éditeur). |
