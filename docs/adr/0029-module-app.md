# ADR-0029 — Le module `app` : la boucle et les modèles sortent du sandbox

- **Statut** : accepté le 2026-10-06, sur les réponses de Donnovan aux sondages du jour (« Un module app dans le
  moteur », puis l'exception de taille pour les déplacements) ; relu par un subagent, dont la relecture a
  ajouté les points d'accroche, les réglages et le piège des appuis entre deux pas
- **Date** : 2026-10-06
- **Milestone** : M6.4

## Contexte

M6.4 doit faire une caméra à la troisième personne, **plugin gameplay de *Rando*** (ADR-0018). Or *Rando* n'affiche
qu'un ciel bleu : il suit Levain à la clôture de M4.6, et tout ce qu'il faudrait pour y promener le renard vit
dans `sandbox/src/main.cpp`, 2 900 lignes :

- la fenêtre, le device, la boucle à pas fixe, la boucle du navigateur (ADR-0023), et le temps où la fenêtre est cachée, qui ne doit pas compter
  comme une image ;
- l'envoi des modèles glTF au GPU, leurs matériaux, le hot-reload des textures (ADR-0021), le skinning et
  l'animateur (ADR-0022) ;
- le ciel et son soleil (M5.4) ;
- le compte rendu des images : le titre de la fenêtre, le calque de la page web (#294), la capture PNG.

C'est le socle de **toute** application du moteur, pas de la démo. Le sandbox est aujourd'hui le seul à pouvoir
s'en servir. SPECS §7 prévoyait depuis le début un module `app/`, « boucle principale, cycle de vie », sous
l'éditeur et le sandbox ; il n'a jamais été écrit, faute d'un deuxième client. *Rando* en est un, et l'éditeur
(phase 7) sera le troisième.

Le sondage du jour, mot pour mot : « Un module app dans le moteur (Recommandé) », contre « Rando recopie le
sandbox » et « La caméra dans le moteur, pour l'instant ».

## Options envisagées

| Option | Pour | Contre |
|---|---|---|
| **A. Un module `app` dans le moteur**, dont le sandbox et *Rando* se servent | Une seule boucle, une seule page web ; l'éditeur (M7) et le jeu (M8.2) en auront besoin de toute façon ; le `main` du jeu tient en une page | Trois PR de plus dans Levain ; M6.4 passe de 1,7 à 3,5 h |
| B. *Rando* recopie ce qu'il faut du sandbox | +1 h seulement | Un millier de lignes en double, qui divergent dès la PR suivante ; A reste à faire avant l'éditeur |
| C. La caméra en plugin moteur, essayée dans le sandbox | Rien à déplacer | Revient sur le classement de l'ADR-0018 ; la question revient dès M6.5 (nage, planeur), elle aussi dans *Rando* |

## Décision

**Le moteur gagne un module `engine/app` (`levain::app`)**, au-dessus de tous les autres modules et sous
l'éditeur, le sandbox et le jeu (SPECS §7, inchangé). Il ne dépend d'aucun plugin : les plugins s'inscrivent
dans le renderer qu'il crée, comme ils le font déjà dans celui du sandbox (ADR-0025).

### Ce qu'il contient

1. **Le cycle de vie**, en une fonction, que le `main` d'une application appelle avec ses réglages et sa
   fonction de démarrage :

   ```cpp
   int main(int argc, char** argv)
   {
       return levain::app::runApp(randoSettings({argv, static_cast<std::size_t>(argc)}), startRando);
   }

   // Appelée une fois, tout étant prêt : le jeu y pose sa scène, et rend ses points d'accroche.
   levain::core::Result<levain::app::FrameHooks> startRando(levain::app::App& app);
   ```

   `runApp` crée la fenêtre et le device (en natif d'un coup, dans le navigateur quand il les donne,
   ADR-0023), le renderer et le monde, appelle la fonction de démarrage, fait tourner la boucle (la sienne en
   natif, `emscripten_set_main_loop` dans le navigateur), puis journalise son bilan, appelle `finish` et écrit
   la capture demandée. En natif, il rend le code de sortie du processus ; dans le navigateur, `main` est déjà
   revenu (ADR-0023), et un échec s'écrit dans la console.

2. **Les points d'accroche**, `FrameHooks`, que la fonction de démarrage rend, tous facultatifs :

   | Point | Quand | Ce que le sandbox y met |
   |---|---|---|
   | (le démarrage) | une fois, tout étant prêt | ses vues, ses modèles, ses plugins ; *Rando* sa vallée et son joueur |
   | `frame(App&)` | à chaque image, après l'input, avant les pas | la sélection à la souris, `FpsInput`, `WalkInput` ; *Rando* la capture de la souris |
   | `record(App&, commandList, seconds)` | à chaque image, avant le rendu | les instances des cubes, les lumières de la démo |
   | `finish(App&)` | à la fin de la boucle | `--pick`, la position du renard, ses bilans ; `false` fait échouer le programme |

   Le reste passe par ce que le moteur a déjà : les **systèmes** flecs pour le gameplay, les **étapes** du
   renderer pour les dessins (ADR-0025). Un point d'accroche n'existe que si un programme en a besoin
   aujourd'hui.
   **Le piège de l'ordre de destruction**, et sa parade : l'état du programme (les cubes de la démo, leurs
   meshes, leurs textures) tient des ressources du GPU, qui doivent disparaître **avant le device**. Les points
   d'accroche le gardent dans leurs captures, et `App` les range dans son dernier champ : détruit le premier,
   il emporte cet état avec lui, avant le renderer, avant le device.

3. **`App`**, ce que la boucle anime et que le programme lit ou remplit : la fenêtre, le device, le renderer, le
   monde flecs et son pas fixe, le registre d'assets et ses modèles, l'input, l'éclairage de l'image (soleil,
   lumières, tonemapping) et **ses compteurs** (dessins écartés et soumis, coût du skinning, temps des passes).
   Une `struct` aux champs publics, comme `DemoScene` aujourd'hui : le jeu y crée ses passes (terrain, eau,
   herbe) avec `app.renderer`. Les bilans du sandbox additionnent les dessins d'`app` (les modèles) et les
   siens (les cubes, le sol) : les lignes que lit la CI ne changent pas.

4. **Les réglages**, `AppSettings`, donnés par le programme et non par des définitions de compilation du
   module : le titre et la taille de la fenêtre, les **racines d'assets** (le sandbox : `data/` et
   `assets-cache/` ; *Rando* : les siennes), le fichier de liaisons, le ciel, le filtrage anisotrope, le
   tonemapping et, pour le hot-reload des shaders (ADR-0014), la commande CMake, le dossier de build et les
   sources des shaders. Le renderer se crée avec le ciel, avant `start` : c'est pourquoi le ciel est un
   réglage et non un appel du programme. Le sandbox les remplit avec les chemins que lui donne son
   `CMakeLists.txt`, comme aujourd'hui ; *Rando* avec les siens, où `CMAKE_BINARY_DIR` est son propre build.

5. **Une image**, dans cet ordre, celui du sandbox aujourd'hui :
   - les événements de la fenêtre, puis l'input (ADR-0017), puis `frame` ;
   - le hot-reload des shaders et des textures ;
   - un pas du monde : les pas de simulation que l'image a mérités, l'interpolation, les matrices monde
     (ADR-0016) ;
   - la caméra, relue sur son entité ;
   - l'animation des modèles skinnés, `record`, puis le rendu ;
   - le déchargement des assets que plus rien n'utilise (ADR-0019) ;
   - le compte rendu : le titre, le calque de la page web, Tracy.

6. **Les modèles** (déjà dans `app` depuis la PR 2) : `loadModel(app, requête)` lit un glTF par son GUID,
   l'instancie dans le monde (ADR-0019) et l'envoie au GPU. Le module dessine **toute entité qui porte un
   `MeshRef`**, dans les étapes d'ombres et d'opaques, sous le nom « modèles ». Un modèle skinné joue son clip
   en boucle ; s'il a une locomotion, il joue le mouvement que lui donne le programme (`MotionOf`) : le
   joueur pour le renard, une vitesse de démonstration pour le modèle de `--locomotion`. Les vitesses de marche
   et de course de sa locomotion sont dans la requête : `app` ne lit pas les réglages du plugin `character`.
   **Limite** : l'animation est rangée par asset. Un modèle **skinné** ne s'instancie qu'une fois, et une
   seconde requête du même échoue (comme aujourd'hui) ; une animation par entité viendra avec un second
   personnage. Un modèle statique, lui, s'instancie autant de fois qu'on le charge, sur les mêmes données GPU.

7. **Le ciel** de l'HDRI, et son soleil (`loadSky`, M5.4), ou l'ambiance uniforme sans HDRI.

8. **La caméra du rendu** est l'entité qui porte un composant `CameraLens` (champ vertical, plans proche et
   lointain), lue à sa matrice monde **interpolée**. Une seule, **vérifiée à chaque image** : zéro ou deux
   arrêtent la boucle avec leurs noms (règle n°7). C'est le `CameraComponent` actif d'Unreal, la `Camera3D`
   *current* de Godot.

9. **L'input du joueur** : le module charge le fichier de liaisons du programme (`input.cfg`, ADR-0017), met à
   jour l'état à chaque image, et le pose en **singleton du monde**. Les systèmes du jeu le lisent : la caméra
   de M6.4 tourne avec les axes `look_right` et `look_up`. `scene` continue d'ignorer l'input, et `input` le
   monde (SPECS §7).
   **Le piège des appuis entre deux pas**, `pressesUntilNextStep` : un appui (`actionPressed`) ne dure qu'une
   image. À 144 images/s, la plupart des images ne jouent aucun pas de simulation, et un système du pas fixe ne
   le verrait jamais ; une image qui joue deux pas le verrait deux fois. Le singleton **cumule** donc les appuis
   jusqu'au prochain pas joué, et les oublie à la fin de ce pas, dans une phase de simulation de plus,
   `EndOfStep` (après le pas suivant de tous les pas de l'image, une image à deux pas le ferait voir deux fois) ;
   les axes et les actions tenues, eux, sont l'état de l'image. Le sandbox le faisait à la main pour le saut (`walk.jump = walk.jump || …`).

10. **Les options communes** de la ligne de commande, celles de la CI et des captures : `--seconds`,
    `--steps`, `--capture`, `--time`, `--gpu`, `--sky`, `--exposure`, `--tonemap`, `--anisotropy`. Le
    programme lit les siennes (`--view` pour le sandbox, `--walk` pour les deux), et passe le reste.

### Ce qui reste au sandbox

Les vues de démonstration et ce qu'elles posent : la grille de cubes, le sol en damier, les lumières de couleur,
les caisses, Sponza et son escalier, la vue du glTF Sample Viewer, la sélection à la souris, et les bilans
propres à chaque critère (culling, skinning, physique). Le sandbox garde aussi sa vue `hike` : c'est la vitrine
des plugins moteur, et la page web publique la montre. La mise en place de la vallée (sa grille de hauteurs, ses
passes, son lac) sera donc **en double** entre le sandbox et *Rando*, une quarantaine de lignes : elle
descendra dans le plugin `terrain` si un troisième programme en a besoin.

### Ce qui va dans *Rando*

La vallée, le lac, l'herbe, le renard, et la caméra de M6.4 : un `main` d'une page, au-dessus de `levain::app`.
*Rando* suit d'abord Levain jusqu'à ce module (son manifeste vcpkg, ses ports, son commit figé), puis reçoit la
caméra. **Ses assets** : le renard et les textures de la vallée sont des assets de test, non versionnés ;
*Rando* les télécharge par le `tools/fetch-assets.sh` de Levain (dans la source que `FetchContent` a récupérée),
qui gagne un dossier de destination, et en fait sa seconde racine d'assets.

### Le découpage en PR

| PR | Dépôt | Contenu |
|---|---|---|
| 1 | Levain | Cet ADR, celui de la caméra (ADR-0030), la roadmap |
| 2 | Levain | `app`, première partie : les modèles sur le GPU, leurs matériaux, le skinning, le hot-reload des textures. Un déplacement, le sandbox s'en sert |
| 3 | Levain | `app`, deuxième partie : `runApp`, ses réglages et ses points d'accroche, la boucle native et web, le compte rendu, le hot-reload des shaders. Le sandbox devient un client |
| 4 | Levain | `app`, troisième partie : la caméra par `CameraLens`, l'input en singleton et ses appuis cumulés, `loadModel` et le dessin des modèles |
| 5 | *Rando* | Le jeu suit Levain et montre la vallée, le renard qu'on dirige et la caméra qui le suit, au-dessus de `app` |
| 6 | *Rando* | La caméra à la troisième personne (ADR-0030) |

Les PR 2 à 4 déplacent surtout du code. **Exception décidée par Donnovan** (sondage du 2026-10-06) : une PR qui
déplace du code sans le changer peut dépasser 400 lignes, si l'écart est signalé et que le guide de lecture dit
de la lire avec `git diff --color-moved`, qui ne fait ressortir que les lignes changées en route (règle n°2
d'`AGENTS.md`).

## Conséquences

- **Le `main` d'une application du moteur tient en une page.** Celui de *Rando* importe ses plugins, pose la
  vallée et le joueur, et rend la main.
- **Le sandbox maigrit d'environ 1 500 lignes**, et ses tests de CI ne changent pas : ce sont la preuve que le
  déplacement n'a rien cassé (mêmes positions du renard, mêmes caisses dans le lac, mêmes images).
- **Le module ne lie pas la physique** : rien de ce qui y monte ne s'en sert. Un jeu sans physique ne la paie
  pas.
- **La page web reste à chaque application** (son `shell.html`, son `stats.js`, ses fichiers préchargés) : le
  module ne fournit que la boucle du navigateur et l'appel à `Module.onFrameReport`. *Rando* dans le navigateur
  viendra plus tard ; rien ici ne l'empêche.
- **L'éditeur (phase 7)** sera une application de plus au-dessus de `app` : son Play/Stop (M7.5) jouera sur le
  monde d'`App`.
- Des fonctions en points d'accroche plutôt qu'une classe à dériver : c'est la règle de forme de
  l'ADR-0011. Il n'y a rien à surcharger, seulement une scène à poser et quelques lignes de glu par image.

## Ce que font les autres moteurs

- **Unreal** : le moteur fournit la boucle (`FEngineLoop`, module `Launch`, d'après le code source), et le jeu
  s'y branche comme un module : « At least one module in your game must be registered using
  `IMPLEMENT_PRIMARY_GAME_MODULE` » [1]. Son contenu passe par les classes du *Gameplay Framework* (GameMode,
  PlayerController, Pawn).
- **Unity** : le jeu n'est que des scripts et des scènes, joués par la boucle du moteur. Cette boucle se lit et
  se modifie par la classe `PlayerLoop`, qui « provides static methods for retrieving and modifying the Unity
  Player loop » [2].
- **Godot** : `MainLoop` « is the abstract base class for a Godot project's game loop. It is inherited by
  SceneTree, which is the default game loop implementation used in Godot projects, though it is also possible
  to write and use one's own MainLoop subclass instead of the scene tree » [3].
- **Bevy** (un moteur Rust tout en ECS, le plus proche de notre usage de flecs) : « `App` is the primary API
  for writing user applications. It automates the setup of a standard lifecycle and provides interface glue for
  plugins » [4], et un programme s'écrit `App::new().add_systems(Update, …).run()`. Nos `runApp` et
  `FrameHooks` en sont l'équivalent explicite.

Les quatre font la même chose : **la boucle appartient au moteur, le jeu y branche son contenu**. Nous gardons
un `main` dans le jeu, parce qu'il se lit d'un coup d'œil et qu'il n'y a ni code généré ni chargement
dynamique (ADR-0018).

## Sources

1. Epic Games, *Gameplay Modules* —
   https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-modules-in-unreal-engine
2. Unity, *PlayerLoop* — https://docs.unity3d.com/ScriptReference/LowLevel.PlayerLoop.html
3. Godot, *MainLoop* — https://docs.godotengine.org/en/stable/classes/class_mainloop.html
4. Bevy, *App* — https://docs.rs/bevy/latest/bevy/app/struct.App.html
