# ADR-0028 — Le personnage : `CharacterVirtual` dans le moteur, la marche dans un plugin

- **Statut** : accepté le 2026-10-05, sur les réponses de Donnovan à deux sondages du jour (le second a
  précisé la masse qui règle la poussée) ; relu deux fois par un subagent, dont la première relecture a fait
  mesurer le coût du personnage contre Sponza et changer la collision du décor
- **Date** : 2026-10-05
- **Milestone** : M6.3

## Contexte

M6.1 et M6.2 ont posé des corps, des volumes et des requêtes (ADR-0026, ADR-0027). M6.3 doit faire marcher
un personnage : **se déplacer dans Sponza, escaliers compris** (critère du milestone), et que le renard de M4.5
marche et coure selon la vitesse du contrôleur (#177). Viendront ensuite la caméra à la troisième personne
(M6.4), puis la nage et le planeur (M6.5), dans le dépôt du jeu.

Un personnage n'est pas un corps rigide : il ne doit ni basculer, ni rebondir, ni glisser sur une pente douce.
Il doit monter une marche, rester collé au sol en descendant, et s'arrêter net. Jolt fournit pour cela
`CharacterVirtual` (Architecture.md, « Character Controllers ») : une forme que l'on **déplace soi-même**, à la
vitesse qu'on lui donne, et qui glisse le long de ce qu'elle touche. Ce n'est pas un corps du `PhysicsSystem` :
on l'avance à chaque pas, et il ne voit les corps que par des requêtes. `ExtendedUpdate` y ajoute deux choses :

- la montée des marches (`mWalkStairsStepUp`, 0,4 m par défaut) ;
- le collage au sol (`mStickToFloorStepDown`, 0,5 m).

La gravité reste à la charge de l'appelant : « it's your own responsibility to apply gravity » (en-tête de
`CharacterVirtual::Update`).

Les réponses de Donnovan au sondage, mot pour mot :

- qui décide de la vitesse ? « Je n'ai pas de préférences, on peut en faire un plugin si c'est quelque chose
  qu'on doit ou veut réutiliser » ;
- d'où vient la collision du décor ? « Le plus performant est toujours le mieux ici » ;
- le personnage pousse-t-il les caisses ? « Oui, selon sa masse » ;
- la démo ? « Le renard, caméra qui suit ».

Les mesures de cet ADR viennent d'une sonde jetable (non versionnée : des ordres de grandeur, règle n°6). Les PR
qui suivent versionnent les leurs. Elles ont été prises en Release, sur la machine de référence, avec le
personnage du code d'essai : une capsule de 1,6 m qui tourne en rond dans Sponza pendant 600 pas.

| Collision de Sponza | Triangles | Construction de la forme Jolt | `moveCharacter`, médiane | 99e centile |
|---|---:|---:|---:|---:|
| Le maillage affiché, tel quel | 262 267 | 118 ms | 850 µs | 5,6 ms |
| Sans le feuillage (matériau en `MASK`) | 230 831 | 101 ms | 440 µs | 3,4 ms |
| Sans feuillage, simplifié à **2 cm** près (meshoptimizer) | 34 016 | 15 ms | **47 µs** | **0,17 ms** |
| Sans feuillage, simplifié à 5 cm près | 13 186 | 7 ms | 25 µs | 0,09 ms |
| Un sol plat (une boîte) | 12 | — | 0,7 µs | 4 µs |

Le coût vient des **détails contre lesquels le personnage s'appuie**. Sur le sol dégagé de Sponza, un pas
coûte 2 µs (la dernière ligne du tableau est un autre sol, une simple boîte) ; contre le vase orné de la cour
(4 800 triangles dans un cube de 1,6 m) et ses feuilles, il coûte 2 à 6 ms. Pour l'ordre de grandeur, le critère
de M6.1 demande qu'un pas de 1 000 caisses tienne en 4 ms. La simplification elle-même coûte environ 60 ms :
c'est un travail de cuisson.

La même sonde a établi que **Sponza n'a pas d'escalier**. La cour centrale (à −0,02 m) et les galeries (à
−0,92 m) sont séparées par un **rebord vertical de 0,9 m**, sans marche. Le critère « escaliers compris »
demande donc d'en poser un.

## Options envisagées

**1. Qui décide de la vitesse**

| Option | Pour | Contre |
|---|---|---|
| A. **Le moteur, comme Unreal** : gravité, saut, accélération, frottement au sol, modes de déplacement | Clé en main | La nage et le planeur de *Rando* (M6.5) deviennent des modes du moteur ; toute façon de bouger nouvelle passe par lui |
| B. **Le jeu, comme Godot et Unity** : le gameplay donne la vitesse voulue, chute comprise ; le moteur glisse, monte les marches, colle au sol | Le moteur reste générique ; chaque état du joueur choisit sa vitesse verticale | Chaque jeu réécrit la marche, la gravité et le saut |
| **C. B dans le moteur, et la marche dans un plugin réutilisable** | Le moteur ne fait que déplacer (B) ; le plugin `character` écrit une fois la marche ; *Rando* ajoute la nage et le planeur au-dessus | Un plugin de plus, et une frontière à tenir : rien de propre à la marche ne descend dans le moteur |

**2. La collision du décor**

| Option | Pour | Contre |
|---|---|---|
| A. **Des formes simplifiées faites à la main** (boîtes, maillages réduits ; le préfixe `UCX_` d'Unreal) | Le moins de triangles | Sponza n'en a pas : à faire et à tenir à jour avec le décor |
| B. **Le maillage affiché, tel quel** | Rien à préparer | 0,85 ms par pas en médiane, 5,6 ms contre un vase : le budget du pas de physique y passe |
| **C. Le maillage affiché, simplifié et cuit par le cuiseur** | Rien à faire à la main ; 47 µs par pas, 0,17 ms au 99e centile ; le chargement relit le résultat | Une dépendance (meshoptimizer) ; une tolérance à choisir ; un format cuit de plus |

**3. Ce que le cuiseur garde**

| Option | Pour | Contre |
|---|---|---|
| **A. Les triangles simplifiés, dans notre format** ; la forme Jolt est construite au chargement | Aucun lien avec la version de Jolt ; le format suit l'ADR-0020 | 15 ms pour construire la forme de Sponza |
| B. La forme Jolt sérialisée (`SaveBinaryState`, Architecture.md, « Saving Shapes ») | Rien à construire au chargement | Jolt prévient que ce format ne sera pas relisible par ses versions suivantes (`Shape.h`) ; sa clé de version (`JPH_VERSION_ID`) change entre Debug et Release, qui se recuiraient l'un l'autre |

**4. L'ordre dans le pas**

| Option | Pour | Contre |
|---|---|---|
| **A. Le personnage avance avant le pas de Jolt** | C'est ce que font les exemples de Jolt (`CharacterVirtualTest::PrePhysicsUpdate`, après `MoveKinematic`) : les impulsions données aux caisses entrent dans le pas, et la vitesse d'une plateforme est déjà posée | Il voit les corps à leur pose du pas précédent |
| B. Après le pas | Il voit les corps à leur nouvelle pose | Les impulsions attendent un pas de plus |

## Décision

1. **Le moteur déplace, le jeu décide (option 1C).** Donnovan n'avait pas de préférence, et acceptait un plugin
   si la marche était réutilisée. Elle l'est : le sandbox du moteur et *Rando* s'en servent. Le module
   `physics` gagne un personnage à la façon du `CharacterController` d'Unity :

   ```cpp
   // Le contrôleur : sa forme et ses limites.
   player.set<physics::CharacterController>({
       .shape = physics::Capsule{.halfHeight = 0.5f, .radius = 0.3f},
       .maxSlopeDegrees = 50.0f, // au-delà, il ne monte plus
       .stepHeight = 0.3f,       // les marches qu'il monte sans sauter
       .mass = 70.0f,            // ce qu'il pèse sur ce qui le porte
       .maxPushForce = 100.0f,   // la force avec laquelle il pousse, en newtons
   });
   // La vitesse voulue, chute comprise : le gameplay applique la gravité. Elle **persiste** d'un pas
   // à l'autre, comme le `velocity` de Godot : non reposée, il continue à la même vitesse.
   player.set<physics::CharacterVelocity>({velocity});
   // Ce que le moteur a trouvé, posé par le module, en lecture seule.
   const auto& state = player.get<physics::CharacterState>();
   ```

   - **Le personnage est un `CharacterVirtual`**, avancé par `ExtendedUpdate` (marches et collage au sol), dans
     la phase `Physics`, **après les cinématiques et avant le pas de Jolt** (option 4A).
   - **Ses pieds sont son origine**, comme celle d'un modèle : la capsule est décalée vers le haut dans sa forme
     (`RotatedTranslatedShape`, comme dans les exemples de Jolt).
   - **`CharacterState`** donne :
     - l'état du sol, avec les quatre états de Jolt : sur un sol, sur un sol trop raide, touché mais sans
       appui, en l'air ;
     - sa normale, l'entité qui le porte et la vitesse de ce sol ;
     - la **vitesse effective**, son déplacement divisé par la durée du pas. C'est celle qu'il faut animer :
       la vitesse que Jolt garde n'est pas corrigée par les collisions, et le renard courrait sur place
       contre un mur.
   - **L'état du sol** est connu dès que le personnage s'est déplacé. La vitesse du sol est relue après le pas
     (`UpdateGroundVelocity`), pour une plateforme que le pas vient de déplacer.
   - **La position appartient à Jolt, la rotation au gameplay**, autour de Y seulement (un piège nommé dans le
     module). Le module recopie la position dans le `Transform`, jamais la rotation. Il lit la rotation du
     `Transform` avant chaque déplacement : le gameplay tourne son personnage en écrivant le `Transform` par
     référence, sans `set`. Un `set<Transform>` le **téléporte**, comme un corps (ADR-0026) : sa vitesse
     dans Jolt **et son `CharacterVelocity`** remises à zéro (sans quoi le composant la reposerait au pas
     suivant), contacts relus, `PreviousTransform` remis à la nouvelle pose pour que le rendu n'interpole pas la
     traversée.
   - **Le contrôleur est une entité racine, sans échelle**, comme un corps (ADR-0026) ; le modèle affiché est
     un **enfant**, avec son échelle et son décalage (le renard est à l'échelle 0,05).
   - **Il est interpolé par le rendu**, comme un corps : le trait `With` attache `PreviousTransform` au
     `CharacterController`.
   - **Le `PhysicsWorld` possède les personnages.** Leur destruction suit l'ordre de fermeture de l'ADR-0026
     (`ecs_is_fini`), car un `CharacterVirtual` qui meurt retire son corps intérieur du système.

2. **Son corps intérieur** est un corps cinématique, créé par Jolt, que Jolt téléporte à chaque déplacement du
   personnage (`SetPositionAndRotation`). Il porte l'entité dans ses *user data* : les volumes déclencheurs le
   voient entrer (la nage, M6.5) et les rayons le touchent (ADR-0027). Il est sur la couche `Character`, que
   notre matrice fait déjà toucher le décor, les corps dynamiques et les capteurs, mais pas elle-même : ni
   le personnage ni ses semblables ne se cognent à lui. Le personnage, de son côté, ignore les capteurs (un
   piège nommé), puisque son corps intérieur s'en charge.

   **Il fait 90 % de la capsule**, comme dans les exemples de Jolt (`cInnerShapeFraction`). C'est un piège
   nommé dans le module : de la taille du personnage, il pousserait les caisses par sa seule pénétration,
   avec une masse infinie, sans la limite de la force de poussée.

3. **La marche vit dans un plugin moteur, `plugins/character`.** Il lit une intention (`WalkInput` : direction,
   course, saut). Il applique la gravité, l'accélération et le saut, et ajoute la vitesse du sol quand le
   personnage marche : sans elle, il glisserait d'une plateforme. Il tourne l'entité vers sa marche et écrit
   `CharacterVelocity`. Il remplit enfin `animation::CharacterMotion` (M4.5) avec la vitesse effective. La nage
   et le planeur de *Rando* seront des états au-dessus de lui.

   **Cet ADR amende l'ADR-0018**, dont les plugins moteur étaient jusqu'ici du level design : un plugin moteur
   peut aussi être une brique de gameplay que plusieurs jeux reprennent. Son tableau gagne une ligne, et
   SPECS §7 suit.

4. **La poussée, selon la masse des caisses.** Donnovan a répondu « selon sa masse », puis précisé, à un
   second sondage, qu'il s'agissait de celle de la caisse. C'est le comportement de Jolt, décrit exactement :
   - **le personnage pousse.** Contre un corps dynamique, Jolt lui donne l'impulsion qui l'amène à la vitesse
     du personnage, compte tenu de **la masse du corps**, mais sans dépasser `maxPushForce`
     (`mMaxStrength`, 100 N par défaut). C'est **un seuil** : une caisse ne bouge que si cette force dépasse
     son frottement, m < F / (μ·g). Avec 100 N et le frottement de 0,5 de nos corps, la limite est vers
     **20 kg** : en dessous, la caisse accélère, d'autant plus lentement qu'elle pèse, puis suit au pas ; au
     dessus, elle ne bouge pas. Contre une caisse plus haute qu'une marche, `ExtendedUpdate` déplace le
     personnage deux fois par pas (son déplacement, puis l'essai de monter la marche) : la caisse reçoit deux
     poussées, et le seuil double, vers **40 kg** avec une hauteur de marche (mesuré à la relecture du code,
     encadré par un test). Les caisses de la démo pèsent 10 kg, et une de 50 kg montre la différence ;
   - **la masse du personnage** (`mMass`) ne sert qu'à peser sur ce qui le porte ;
   - **le personnage est poussé**, par la vitesse du point de contact : une plateforme ou un rocher qui roule
     l'emmène (`mCanPushCharacter`, gardé à son défaut). Il n'y a pas d'échange de quantité de mouvement : une
     caisse lancée contre lui ne le fait pas reculer davantage parce qu'elle est lourde.

5. **La collision du décor est le maillage affiché, simplifié, puis cuit (options 2C et 3A).** « Le plus
   performant est toujours le mieux ici » : les mesures disent que c'est à l'exécution que se joue la
   performance, pas au chargement.
   - **`assets` en tire un maillage de collision** :
     - les triangles du modèle sont mis dans son repère (les nœuds composés) ;
     - les matériaux à transparence découpée (`alphaMode` `MASK`), le feuillage, en sont écartés : on ne se
       cogne pas à des feuilles (un piège nommé). Pas ceux en `BLEND` : une vitre reste solide ;
     - les sommets sont soudés par position, puis **simplifiés à 2 cm près** par meshoptimizer
       (`meshopt_simplify` avec `meshopt_SimplifyErrorAbsolute`, sans quoi l'erreur serait relative à la
       taille du modèle ; licence MIT, port vcpkg), visible dans `assets/src` seulement. Les 2 cm sont
       mesurés **dans le monde** : l'échelle du placement est appliquée aux triangles avant, puisqu'un corps
       n'a pas d'échelle (ADR-0026).

     Le résultat : des sommets et des indices. ni `physics` ni `assets` ne dépendent l'un de l'autre
     (SPECS §7). La glu d'une ligne vit dans l'application.
   - **Le cuiseur l'écrit à côté du modèle cuit**, avec l'en-tête de l'ADR-0020 : signature, version du format,
     version du cuiseur, hash de la source. Un fichier périmé retombe sur la simplification au chargement,
     avec un avertissement, comme un modèle cuit retombe sur le glTF.
   - **La forme Jolt est construite au chargement** depuis ces triangles : 15 ms pour Sponza. Sérialiser la
     forme Jolt gagnerait ces 15 ms au prix d'un format lié à la version et à la configuration de Jolt : ce
     n'est pas la peine tant que le chargement ne le réclame pas.
   - **2 cm**, c'est du même ordre que la marge que le personnage garde à la géométrie (`mCharacterPadding`).
     L'erreur de meshoptimizer est une estimation, pas une borne : le contrôle est une vérification par
     rayons, comme celle de la sonde (les mêmes hauteurs de sol tous les 10 cm, avant et après), qu'un test
     de la PR reprend. La tolérance deviendra un réglage d'import dans le `.meta` quand un modèle en voudra
     une autre ; elle entrera alors dans l'en-tête du fichier cuit, pour qu'en changer le périme.
   - Une convention de collision faite à la main (des nœuds `UCX_` ou `-col`) viendra quand le jeu aura des
     modèles qui la portent ; elle remplacera alors la simplification pour ces modèles.

6. **La démo** (choix de Donnovan) : le renard dans Sponza, mené au clavier, caméra qui le suit à distance fixe,
   sans collision (la vraie caméra est M6.4). Contre le rebord de la cour, la démo pose :
   - un **escalier** de boîtes, six marches de 0,15 m ;
   - une rampe de 30°, qu'il monte, et une de 55°, qu'il ne gravit pas ;
   - quelques caisses à pousser.

   **Le critère se vérifie à deux endroits** :
   - **un test sans fenêtre**, sur une géométrie construite, que `ctest` lance dans tous les presets avant le
     téléchargement des assets. Il vérifie : monter l'escalier, ne pas le monter sans hauteur de marche (la
     preuve que c'est bien la montée qui opère), monter la rampe douce, ne pas gravir la raide, s'arrêter
     contre le rebord de 0,9 m ;
   - **le sandbox dans Sponza**, après les assets. `--walk <direction> --steps N` avance exactement N pas fixes,
     un par image, quelle que soit la durée réelle de l'image. Il écrit la position des pieds au journal ; la CI
     vérifie la hauteur atteinte en haut de l'escalier.

## Conséquences

- **Le moteur ne connaît ni la gravité du personnage, ni le saut, ni les modes.** Un jeu qui ne veut que la
  marche prend le plugin ; un jeu qui veut autre chose écrit sa vitesse lui-même.
- **Une dépendance de plus**, meshoptimizer, visible dans `assets/src` seulement (contrôle
  `deps.asset-libraries-visibility` étendu). Elle servira aussi aux LOD des modèles, si on en fait.
- **Un modèle dont la simplification enlève l'essentiel** (un objet fin, une grille) perd sa collision : une
  erreur de 2 cm efface un barreau de 1 cm. Le cas se règlera par la convention faite à la main.
- **Le cache des assets cuits de la CI** : sa clé couvre déjà le code qui simplifie (`engine/assets`). La CI web
  ne cuit pas : le navigateur simplifie au chargement, sur un seul thread. À mesurer dans le navigateur.
- ***Rando* recopie la dépendance** dans son manifeste vcpkg, comme ktx (contrôle de l'ADR-0018).
- **Le personnage ne se cogne pas aux autres personnages** (`CharacterVsCharacterCollision` de Jolt) : un seul
  joueur dans *Rando* ; à reprendre avec des PNJ. Le numéro que Jolt donne à chaque personnage vient d'un
  compteur global : le test de déterminisme de l'ADR-0026 devra en tenir compte s'il inclut un personnage.
- **Une caisse qui tombe sur le personnage** se pose sur son corps intérieur, 8 cm à l'intérieur de sa capsule :
  Jolt la repousse vers le haut, et sa récupération de pénétration peut déplacer le personnage. Un test vérifie
  que la caisse se stabilise, et que le personnage ne s'enfonce ni ne dérive.

## Ce que font les autres moteurs

- **Unity** : `CharacterController.Move` déplace en glissant le long des obstacles et « does not use gravity »
  (**documenté** [1]). `slopeLimit` et `stepOffset` règlent pentes et marches (**documenté** [2]). Le contrôleur
  ne pousse pas les `Rigidbody` : l'exemple de la documentation le fait à la main, dans
  `OnControllerColliderHit` (**documenté** [3]).
- **Godot** : `CharacterBody3D.move_and_slide` déplace selon `velocity`, que le script calcule, gravité comprise ;
  `floor_max_angle` règle la pente (**documenté** [4]). La référence de la classe n'a aucune propriété de
  hauteur de marche (`floor_snap_length` colle au sol, il ne fait pas monter) : un escalier demande du code ou
  des rampes invisibles (**déduit** de son absence).
- **Unreal** : `UCharacterMovementComponent` porte tout : la gravité (`GravityScale`), le saut (`JumpZVelocity`),
  les marches (`MaxStepHeight`), la pente (`WalkableFloorAngle`), les modes (`MOVE_Walking`, `MOVE_Falling`,
  `MOVE_Swimming`, `MOVE_Flying`, `MOVE_Custom`), et la poussée des objets physiques
  (`bEnablePhysicsInteraction`, `PushForceFactor`) (**documenté** [5]). C'est notre option 1A, que Levain
  répartit entre le moteur et le plugin.
- **La collision du décor** : Unreal importe des formes simplifiées nommées `UCX_` à côté du maillage
  (**documenté** [6]). Il distingue la collision simple de la complexe, et « Use Complex Collision As Simple »
  fait du maillage affiché la collision (**documenté** [7]) : c'est notre option 2B. Godot crée une collision
  depuis le maillage avec les suffixes `-col` et `-colonly` à l'import (**documenté** [8]). Sa
  `ConcavePolygonShape3D` garde des triangles bruts (`set_faces`), et sa structure de recherche se construit
  au chargement, comme notre option 3A (**déduit** de son API).

## Sources

1. Unity, `CharacterController.Move` — https://docs.unity3d.com/ScriptReference/CharacterController.Move.html
2. Unity, `CharacterController` — https://docs.unity3d.com/ScriptReference/CharacterController.html
3. Unity, `CharacterController.OnControllerColliderHit` —
   https://docs.unity3d.com/ScriptReference/CharacterController.OnControllerColliderHit.html
4. Godot, `CharacterBody3D` — https://docs.godotengine.org/en/stable/classes/class_characterbody3d.html
5. Epic Games, `UCharacterMovementComponent` —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/UCharacterMovementComponent
6. Epic Games, *FBX Static Mesh Pipeline*, « Collision » —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-static-mesh-pipeline-in-unreal-engine
7. Epic Games, *Simple versus Complex Collision* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/simple-versus-complex-collision-in-unreal-engine
8. Godot, *Node type customization using name suffixes* —
   https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_3d_scenes/node_type_customization.html
9. Jolt Physics, *Architecture*, « Character Controllers » et « Saving Shapes » — https://jrouwe.github.io/JoltPhysics/ ;
   en-têtes `Jolt/Physics/Character/CharacterVirtual.h` et `Jolt/Physics/Collision/Shape/Shape.h` (5.6.0),
   exemples `Samples/Tests/Character/`.
10. meshoptimizer, « Simplification » — https://github.com/zeux/meshoptimizer
