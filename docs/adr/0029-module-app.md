# ADR-0029 — Le module `app` : la boucle et les modèles sortent du sandbox

- **Statut** : accepté le 2026-10-06, sur la réponse de Donnovan au sondage du jour (« Un module app dans le
  moteur »)
- **Date** : 2026-10-06
- **Milestone** : M6.4

## Contexte

M6.4 doit faire une caméra à la troisième personne, **plugin gameplay de *Rando*** (ADR-0018). Or *Rando* n'affiche
qu'un ciel bleu : il suit Levain à la clôture de M4.6, et tout ce qu'il faudrait pour y promener le renard vit
dans `sandbox/src/main.cpp`, 2 900 lignes :

- la fenêtre, le device, la boucle à pas fixe, la boucle du navigateur (ADR-0023) et le temps masqué ;
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
| **A. Un module `app` dans le moteur**, dont le sandbox et *Rando* se servent | Une seule boucle, une seule page web ; l'éditeur (M7) et le jeu (M8.2) en auront besoin de toute façon ; le `main` du jeu tient en une page | Environ trois PR de plus, M6.4 passe de 1,7 à 3,5 h |
| B. *Rando* recopie ce qu'il faut du sandbox | +1 h seulement | Un millier de lignes en double, qui divergent dès la PR suivante ; A reste à faire avant l'éditeur |
| C. La caméra en plugin moteur, essayée dans le sandbox | Rien à déplacer | Revient sur le classement de l'ADR-0018 ; la question revient dès M6.5 (nage, planeur), elle aussi dans *Rando* |

## Décision

**Le moteur gagne un module `engine/app` (`levain::app`)**, au-dessus de tous les autres modules et sous
l'éditeur, le sandbox et le jeu (SPECS §7, inchangé). Il ne dépend d'aucun plugin : les plugins s'inscrivent
dans le renderer qu'il crée, comme ils le font déjà dans celui du sandbox (ADR-0025).

### Ce qu'il contient

1. **Le cycle de vie**, en une fonction, que le `main` d'une application appelle :

   ```cpp
   int main(int argc, char** argv)
   {
       return levain::app::runApp({.title = "Rando", .arguments = {argv, argc}}, startRando);
   }

   // Appelée une fois, quand la fenêtre, le device, le renderer et le monde sont prêts : le jeu y
   // importe ses plugins et pose sa scène. Un échec arrête l'application, avec son message.
   levain::core::Result<void> startRando(levain::app::App& app);
   ```

   `runApp` crée la fenêtre et le device (en natif d'un coup, dans le navigateur quand il les donne,
   ADR-0023), appelle la fonction de démarrage, fait tourner la boucle (la sienne en natif,
   `emscripten_set_main_loop` dans le navigateur), puis journalise le bilan et écrit la capture demandée. Il
   rend le code de sortie du processus.

2. **`App`**, ce que la boucle anime et que le jeu lit ou remplit : la fenêtre, le device, le renderer, le
   monde flecs et son pas fixe, le registre d'assets et ses modèles, l'input. Une `struct` aux champs publics,
   comme `DemoScene` aujourd'hui : le jeu y crée ses passes (terrain, eau, herbe) avec `app.renderer`.

3. **Une image**, dans cet ordre, celui du sandbox aujourd'hui :
   - les événements de la fenêtre, puis l'input (ADR-0017) ;
   - le hot-reload des shaders et des textures ;
   - un tour du monde : les pas de simulation, l'interpolation, les matrices monde (ADR-0016) ;
   - la caméra, relue sur son entité ;
   - l'animation des modèles skinnés, puis le rendu ;
   - le déchargement des assets que plus rien n'utilise (ADR-0019) ;
   - le compte rendu : le titre, le calque de la page web, Tracy.

4. **Les modèles** : `loadModel(app, requête)` lit un glTF par son GUID, l'instancie dans le monde (ADR-0019)
   et l'envoie au GPU. Le module dessine **toute entité qui porte un `MeshRef`**, dans les étapes d'ombres et
   d'opaques, sous le nom « modèles ». Un modèle skinné joue son clip en boucle, ou, s'il a une locomotion, suit
   le `CharacterMotion` de sa racine ou du parent de celle-ci : le renard suit le joueur sans glu.

5. **Le ciel** de l'HDRI, et son soleil (`loadSky`, M5.4), ou l'ambiance uniforme sans HDRI.

6. **La caméra du rendu** est l'entité qui porte un composant `CameraLens` (champ vertical, plans proche et
   lointain), lue à sa matrice monde **interpolée**. Une seule : zéro ou deux font échouer le démarrage, avec
   leurs noms (règle n°7). C'est le `CameraComponent` actif d'Unreal, la `Camera3D` *current* de Godot.

7. **L'input du joueur** : le module charge le fichier de liaisons de l'application (`input.cfg`, ADR-0017),
   met à jour l'état à chaque image, et le pose en **singleton du monde**. Les systèmes du jeu le lisent, comme
   ceux d'un plugin gameplay : la caméra de M6.4 tourne avec les axes `look_right` et `look_up`. `scene`
   continue d'ignorer l'input, et `input` le monde (SPECS §7).

8. **Les options communes** de la ligne de commande, celles de la CI : `--seconds`, `--steps`, `--capture`,
   `--time`, `--gpu`. L'application lit les siennes avant (`--view` pour le sandbox), et passe le reste.

### Ce qui reste au sandbox

Les vues de démonstration et ce qu'elles posent : la grille de cubes, le sol en damier, les lumières de couleur,
les caisses, Sponza et son escalier, la vue du glTF Sample Viewer, la sélection à la souris, et les bilans
propres à chaque critère (culling, skinning, physique). Le sandbox garde aussi sa vue `hike` : c'est la vitrine
des plugins moteur, et la page web publique la montre.

### Ce qui va dans *Rando*

La vallée, le lac, l'herbe, le renard, et la caméra de M6.4 : un `main` d'une page, au-dessus de `levain::app`.
*Rando* suit d'abord Levain jusqu'à ce module (son manifeste vcpkg, ses ports, son commit figé), puis reçoit la
caméra.

### Le découpage en PR

| PR | Dépôt | Contenu |
|---|---|---|
| 1 | Levain | Cet ADR, celui de la caméra (ADR-0030), la roadmap |
| 2 | Levain | `app`, première moitié : les modèles sur le GPU, leurs matériaux, le skinning, le hot-reload des textures. Un déplacement, le sandbox s'en sert |
| 3 | Levain | `app`, seconde moitié : `runApp`, la boucle native et web, la caméra par `CameraLens`, l'input en singleton, le compte rendu. Le sandbox devient un client |
| 4 | *Rando* | Le jeu suit Levain et montre la vallée, le renard qu'on dirige et la caméra qui le suit, au-dessus de `app` |
| 5 | *Rando* | La caméra à la troisième personne (ADR-0030) |

Les PR 2 et 3 déplacent surtout du code : leur relecture se fait au guide de lecture, qui dit ce qui a changé en
route. Elles dépasseront peut-être 400 lignes ; l'écart sera signalé (règle n°2).

## Conséquences

- **Le `main` d'une application du moteur tient en une page.** Celui de *Rando* importe ses plugins, pose la
  vallée et le joueur, et rend la main.
- **Le sandbox maigrit d'environ 1 500 lignes**, et ses tests de CI ne changent pas : ce sont la preuve que le
  déplacement n'a rien cassé (mêmes positions du renard, mêmes caisses dans le lac, mêmes images).
- **Le module dépend de tout le moteur** : un jeu sans physique la lie quand même. C'est le prix d'un seul
  socle ; le jour où un jeu s'en plaindra, la physique deviendra optionnelle dans `app`.
- **La page web reste à chaque application** (son `shell.html`, son `stats.js`, ses fichiers préchargés) : le
  module ne fournit que la boucle du navigateur et l'appel à `Module.onFrameReport`. *Rando* dans le navigateur
  viendra plus tard ; rien ici ne l'empêche.
- **L'éditeur (phase 7)** sera une application de plus au-dessus de `app` : son Play/Stop (M7.5) jouera sur le
  monde d'`App`.
- Une fonction de démarrage plutôt qu'une classe à dériver : c'est la règle de forme de l'ADR-0011. Il n'y a
  rien à surcharger, seulement une scène à poser.

## Ce que font les autres moteurs

- **Unreal** : le moteur fournit la boucle (`FEngineLoop`, module `Launch`, d'après le code source), et le jeu
  est un module qu'elle charge (`IMPLEMENT_PRIMARY_GAME_MODULE`). Le jeu n'écrit jamais de `main` ; il règle des
  classes du *Gameplay Framework* (GameMode, PlayerController, Pawn).
- **Unity** : le *Player* est un exécutable précompilé ; le jeu n'est que des scripts et des scènes. Sa boucle
  se lit et se modifie par la classe `PlayerLoop`, qui « provides static methods for retrieving and modifying
  the Unity Player loop » [1].
- **Godot** : `MainLoop` « is the abstract base class for a Godot project's game loop. It is inherited by
  SceneTree, which is the default game loop implementation used in Godot projects, though it is also possible
  to write and use one's own MainLoop subclass instead of the scene tree » [2].
- **Bevy** (un moteur Rust tout en ECS, le plus proche de notre usage de flecs) : `App::new()
  .add_plugins(DefaultPlugins).run()` ; l'`App` « is how you define the structure of all the things that make
  up your project: plugins, systems […] » [3]. Notre `runApp` et sa fonction de démarrage en sont l'équivalent
  explicite.

Les quatre font la même chose : **la boucle appartient au moteur, le jeu y branche son contenu**. Nous gardons
un `main` dans le jeu, parce qu'il se lit d'un coup d'œil et qu'il n'y a ni code généré ni chargement
dynamique (ADR-0018).

## Sources

1. Unity, *PlayerLoop* — https://docs.unity3d.com/ScriptReference/LowLevel.PlayerLoop.html
2. Godot, *MainLoop* — https://docs.godotengine.org/en/stable/classes/class_mainloop.html
3. *Unofficial Bevy Cheat Book*, « The App » — https://bevy-cheatbook.github.io/programming/app-builder.html
