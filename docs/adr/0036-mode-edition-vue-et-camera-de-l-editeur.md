# ADR-0036 — L'atelier : le mode Édition, l'input de l'éditeur, la Vue et sa caméra

- **Statut** : proposé, à accepter par sondage avec la ROADMAP v0.16 ; les réponses aux sondages du 10/10 sont les
  choix de Donnovan, les options 1A à 6A et le détail des décisions sont nos propositions
- **Modifie** : les invariants 4 et 7 d'`editor/README.md`, 6, 8 et 9 d'`engine/app/README.md` ; les ADR-0029,
  0032 et 0034 (détail plus bas) ; SPECS §5 (les polices)
- **Date** : 2026-10-10
- **Milestone** : M7.7 (nouveau)

## Contexte

**L'éditeur d'aujourd'hui, c'est le jeu avec deux panneaux de plus** (main 41db6c4, M7.2 close) :

- `withEditor` n'enveloppe que les points d'accroche `ui` et `finish` (editor/src/editor.cpp:88-108), et la
  simulation avance à chaque image, sans arrêt possible (engine/app/src/app.cpp:875-877) ;
- les fenêtres (`hooks.ui`, par `endUiFrame` : app.cpp:891, 603) passent après les pas, les matrices monde et le
  choix de la caméra (app.cpp:875-885) : une valeur tapée dans l'inspecteur ne se voit qu'à l'image suivante ;
- la caméra est celle du jeu, une entité que déplace un système du pas fixe (sandbox/src/main.cpp:641-647 ;
  engine/scene/src/scene.cpp:111-117) : simulation arrêtée, elle ne bougerait plus ;
- le clavier va au jeu dès qu'aucun widget ne le prend (app.cpp:851-852) : ZQSD déplace la caméra pendant qu'on
  édite, et sur l'AZERTY de Donnovan, Ctrl+Z appuie aussi sur « avancer » ;
- la scène couvre la fenêtre, derrière un docking qui la laisse voir au centre (engine/app/src/panels.cpp:151-152),
  mais sa projection est centrée sur la fenêtre entière (engine/render/src/renderer.cpp:139) ; panneaux ouverts,
  la souris n'est jamais capturée (engine/app/include/levain/app/ui_layer.hpp:93-96) ;
- ni menu, ni raccourci, ni console ; fermer la fenêtre arrête la boucle sans rien demander (app.cpp:106-108).

**Le mandat de Donnovan**, le 2026-10-10, mot pour mot : « Tu peux reprendre, je ne regarderais pas donc fait tout
les test nécessaire pour que ça fonctionne, tu peux finir tout les 7.*, fait en sorte que l'editor soit aussi
agréable à utilisé que celui d'Unity ou Unreal. Par contre n'hésite pas à me posé des questions via des sondages la
session est en remote donc je peux y répondre. » Ses réponses aux sondages du jour, mot pour mot :

- **Périmètre** : « M7.7 « L'atelier » d'abord, puis Play/Stop (Recommandé) » ;
- **Modèle** (quel éditeur imiter quand Unity et Unreal diffèrent) : « Unreal et il faudra faire un menu ou on peut
  régler les raccourcis pour ceux qui veulent les changers. » ;
- **Clavier** (les touches d'outil sur son AZERTY) : « Par position : Z, E, R (Recommandé) » ;
- **Langue** : « Français mais il faudrait faire une passe de traduction à la fin pour qu'on puisse changer la
  langue du moteur pour l'anglais pour le reste des gens. » ;
- **Fenêtres** (les tests sur le portable Windows) : « Oui, quand tu veux (Recommandé) » ;
- **Police** : « Accepter l'OFL pour les polices (Recommandé) ».

Unity et Unreal ont deux états : **l'édition**, où rien ne tourne et où une caméra d'éditeur regarde la scène, et
**le jeu** (Play, PIE). M7.5 (Play/Stop) a besoin du premier ; M7.7 le pose, avec le confort quotidien. Tout cela
touche `app`, dont l'éditeur promettait de ne pas changer la boucle : d'où cet ADR (règle n°3).

## Options envisagées

| 1. Arrêter la simulation en Édition | Pour | Contre |
|---|---|---|
| **A. Un drapeau nommé d'`App`, testé avant `advanceWorld`** | Se lit et se teste ; l'interpolation et l'accumulateur ont chacun leur règle | Un champ et une branche de plus dans la boucle |
| B. `FixedStep::maxStepsPerFrame = 0` | Rien à ajouter : `planSteps` ne joue plus aucun pas | Un détournement du garde-fou ; `alpha` resterait figé à sa dernière valeur : l'image montrerait un mélange de l'ancien et du nouveau `Transform` |
| C. Couper la phase `Simulation` | flecs sait désactiver un pipeline | L'accumulateur et `RenderAlpha` continuent : les corps tremblent entre deux états |

| 2. Quand une écriture de l'éditeur se voit | Pour | Contre |
|---|---|---|
| **A. Une recomposition nommée après `hooks.ui`, à la demande de l'éditeur, puis la caméra** | Dans l'image même : l'inspecteur, les gizmos (M7.4) et la caméra de l'éditeur sans retard | Les matrices monde composées deux fois dans une image où l'éditeur écrit (coût à mesurer, morceau 4) |
| B. Garder une image de retard | Rien à faire | Le modèle suit le gizmo avec une image de retard ; une caméra imposée aussi |
| C. `hooks.ui` avant les pas | Une seule composition | Le HUD de *Rando* et les panneaux liraient l'état d'avant les pas : le retard passe au jeu |

| 3. Qui reçoit l'input | Pour | Contre |
|---|---|---|
| **A. `App::inputRoute` (éditeur, jeu ou UI), posé par l'éditeur pour l'image suivante** | Explicite, testé au niveau de l'App ; en jeu, un clic dans la Vue arrive au jeu | Une image de retard quand la route change : le clic qui donne le focus à la Vue n'arrive pas au jeu |
| B. `SetNextFrameWantCaptureKeyboard` et `…Mouse` | L'API d'ImGui | Agit aussi à l'image suivante (imgui.h:1103, 1157) ; la Vue est une fenêtre ImGui : la survoler met `WantCaptureMouse` à vrai, et en jeu, le jeu ne verrait plus un clic |
| C. Ne plus appeler `hooks.frame` en Édition, rien d'autre | Une ligne | `PlayerInput` cumule les appuis jusqu'au prochain pas (app.cpp:854-856) : ils partiraient tous au premier pas de Play |

| 4. La caméra de l'éditeur | Pour | Contre |
|---|---|---|
| **A. Un état de l'éditeur, imposé à `App` (`App::cameraOverride`) après `hooks.ui`** | Hors du pas fixe, jamais sauvegardée ; une scène sans caméra reste ouvrable | Un champ d'`App` de plus |
| B. Une seconde entité `CameraLens` | L'inspecteur la montre | Deux `CameraLens` arrêtent la boucle (engine/app/src/camera.cpp:53-71) ; elle serait sauvegardée avec la scène, et ne bougerait qu'au pas fixe |
| C. Écrire `app.camera` dans `hooks.ui` | Possible aujourd'hui, sans API | Un contrat implicite ; zéro `CameraLens` arrête encore la boucle |

| 5. Où se voit la scène | Pour | Contre |
|---|---|---|
| **A. La Vue : la scène rendue dans une texture, montrée par une fenêtre ImGui** | La projection suit la fenêtre ; on la déplace, l'agrandit, la ferme comme les autres ; le picking et les gizmos (M7.4) ont son rectangle | Une cible de rendu de plus, recréée quand la taille change ; `ui` doit montrer une texture qu'il ne possède pas |
| B. Le centre transparent d'aujourd'hui, la projection recentrée sur le trou | Aucune texture | Les pixels sous les panneaux rendus pour rien ; une projection décentrée à écrire ; pas d'onglet « Jeu » possible plus tard |

| 6. Les chaînes de l'éditeur | Pour | Contre |
|---|---|---|
| **A. Un catalogue dès M7.7, le texte français pour clé (`tr("Fichier")`), comme gettext [8]** | Le code reste lisible ; une traduction manquante s'affiche en français, jamais vide ; M7.8 n'ajoute qu'un fichier | Corriger un texte français change sa clé (le contrôle de M7.8 le voit) ; un homonyme (un français, deux anglais) demande un contexte, le `msgctxt` de gettext ; pluriels et valeurs ont leurs règles (décision 14) |
| B. Des identifiants (`str::FileMenu`) | Une clé stable | Chaque texte écrit deux fois ; le code ne se lit plus |
| C. Rien avant M7.8 | Rien maintenant | Toutes les fenêtres de la phase 7 à reprendre à la fin, et leurs identifiants ImGui avec |

## Décision

**1A, 2A, 3A, 4A, 5A, 6A**, et les choix des sondages. En bref : l'éditeur s'ouvre en Édition, la simulation à
l'arrêt et le jeu sans input, la scène dans une fenêtre « Vue » que parcourt une caméra d'éditeur pilotée comme
celle d'Unreal. Autour : les raccourcis d'Unreal, que chacun peut changer ; chaque texte par un catalogue, pour
l'anglais de M7.8 ; des polices sous OFL, une console, une disposition gardée. Seize décisions :

1. **La simulation à l'arrêt.** Un drapeau d'`App` (`simulationPaused`), testé avant `advanceWorld`. À l'arrêt, aucun
   pas ; `RenderAlpha` vaut 1, l'image montre donc le `Transform` tapé ; l'accumulateur ne reçoit pas le temps de
   l'image (engine/scene/include/levain/scene/fixed_step.hpp:30-36), sinon le retour au jeu jouerait une rafale de
   pas ; `progress` tourne encore. `--steps N` compte des images (app.cpp:807-809) : à l'arrêt, il ne joue rien.
2. **L'ordre de l'image change.** Après `hooks.ui` et `ImGui::Render`, si l'éditeur le demande (`App::recomposeAfterUi`,
   remis à faux à chaque image), `scene::composeWorldTransforms` recompose les matrices monde (le système
   `ComputeWorldTransforms`, nommé : scene.cpp:176-181) ; puis la caméra est relue, puis le rendu. Une écriture de
   l'éditeur se voit dans son image. `app.camera` reste posée avant `ui` aussi : un HUD y lit la caméra de l'image.
3. **Les modes vivent dans `editor::Editor`**, et `withEditor` enveloppe aussi `hooks.frame` : en Édition, le
   `frame` du programme n'est jamais appelé (`steerDemo` l'est aujourd'hui à chaque image : main.cpp:1209-1231). Le
   mode « Jouer (sans retour) » garde le jeu d'aujourd'hui sous un bandeau, jusqu'à Play et Stop (M7.5) ; sans
   Enregistrer, rien ne peut s'y perdre. `mouseCaptureWanted` (main.cpp:1215) est remis à faux à chaque changement.
4. **Le routage de l'input**, `App::inputRoute`. *UI* : le filtre d'aujourd'hui, par défaut (le sandbox, *Rando*).
   *Jeu* : la souris au jeu dans le rectangle de la Vue de l'image d'avant, ou capturée ; le clavier au jeu, ImGui
   n'en recevant que les relâchements, F1 et les accords du contexte *jeu* : Ctrl+S ne part pas en jouant.
   *Éditeur* : rien au jeu, ce qui était tenu relâché. L'éditeur la décide à la fin de l'image N pour l'image N+1
   (survol et focus ne se savent que dans `hooks.ui`), et `gameInputOf` la lit. Édition : *éditeur* ; « Jouer (sans
   retour) » : *jeu* quand la Vue a le focus, *UI* sinon.
5. **L'éditeur lit l'input brut**, scancodes compris. `platform::keyAtPosition(scancode)` rend la touche (keycode)
   qu'une disposition met à une position, et `ui::imguiKeyOf` (ui/input.hpp:16-21) la traduit : ImGui ne descend
   pas sous `ui` (`deps.imgui-visibility`, tests/CMakeLists.txt:380-382). Le changement de disposition devient un
   événement de `platform`, que `translateEvent` ignore aujourd'hui (engine/platform/src/window.cpp:38-80). Les
   touches d'ImGui restent traduites par keycode : Ctrl+Z reste la touche marquée Z.
6. **La caméra de l'éditeur** n'est **pas** une entité : un état d'`Editor` (position, lacet, tangage, distance du
   pivot, vitesse), imposé par `App::cameraOverride`. Avec elle, `renderCameraOf` n'est pas consulté, pas même par
   `startApp` avant la première image (app.cpp:1044-1050) ; sans elle, zéro ou deux `CameraLens` arrêtent la boucle
   comme avant. Elle part de la caméra du jeu s'il y en a une. Ses plans proche et lointain suivent le cadrage, comme
   le *Dynamic Clipping* d'Unity [4] : le lointain par défaut est de 100 (render/camera.hpp:16), pour une vallée de
   512 m. Sa logique, en fonctions libres (ADR-0011) : `flyCamera`, `orbitCamera`, `panCamera`, `dollyCamera`,
   `framingOf`, `clipPlanesFor`, `clampEditorPitch` (±89°, `lookAtRH` dégénérant à ±90° : render/src/camera.cpp:10).
   Les gestes d'Unreal [1], sur la Vue : clic droit tenu et les touches de vol, la molette en vol pour la vitesse ;
   Alt+clic gauche, l'orbite ; bouton du milieu, le pan ; molette, le zoom ; Alt+clic droit, le dolly ; F ou le
   double-clic dans la hiérarchie, cadrer.
7. **La Vue.** La scène se rend dans une texture que l'UI montre, au format de la swapchain : la conversion sRGB de
   l'UI reste juste (ADR-0032, `linearOnSrgbTarget`). Sa taille se demande dans `hooks.ui` ; elle n'est recréée
   qu'une fois stable, l'ancienne image étirée en attendant, car l'image HDR et la profondeur se recréent à chaque
   taille (renderer.cpp:124-131). `ui` montre une texture qu'il ne possède pas (`registerUiTexture`) ; une inconnue
   reste une assertion en Debug (ui/src/ui_pass.cpp:390-391), plus une erreur en Release. La scène dans la Vue, la
   swapchain est effacée avant l'UI et le docking perd `PassthruCentralNode` (panels.cpp:151-152) ; une Vue fermée
   ou de taille nulle ne rend pas la scène ; panneaux fermés (F1), la scène reprend la fenêtre, et les gestes s'y
   font partout. `App::sceneRect` publie le rectangle de la scène : le jeu y lit sa taille et le curseur, comme le
   picking de `steerDemo`, qui lit aujourd'hui la fenêtre (main.cpp:1218-1220). L'exécutable éditeur refuse
   `--gpu webgpu`, bruyamment (editor/README.md:28-29).
8. **La souris pendant le vol** est capturée, panneaux ouverts compris (`mouseShouldBeCaptured` gagne ce cas). Le
   regard lit le mouvement brut ; ImGui ne reçoit plus que les relâchements, la règle de `gameInputOf`
   (ui_layer.hpp:81-86) : le clic droit du vol ne reste pas enfoncé pour lui, ses menus contextuels marchent encore.
9. **Le temps en Édition** continue pour l'eau, l'herbe et les matériaux (app.cpp:680-694). Les squelettes, qui
   lisent aujourd'hui le même temps (`animateModels`, engine/app/src/models.cpp:300-302 ; main.cpp:828), suivent
   la simulation : `App` leur passe le temps de la scène moins le temps passé à l'arrêt, que `--time` fige comme
   avant ; le renard ne marche plus sur place. « Temps réel », dans la Vue, décoché, fige tout (Ctrl+R) [1].
10. **La disposition et les préférences.** `DockNodes` gagne `center` et `bottom` (ui_layer.hpp:51-55 ;
    panels.cpp:28-49) : la Hiérarchie à gauche, la Vue au centre, l'Inspecteur à droite, seul sur son nœud pour la
    CI (editor/README.md:134-137), Console, Image, Passes et Scène en bas. Le menu Fenêtre rouvre une fenêtre et
    propose « Réinitialiser la disposition ». L'éditeur seul garde disposition et préférences (`SDL_GetPrefPath`,
    par `platform`), relues sans `buildLayout` (panels.cpp:147-150). **Une exécution scriptée ne les lit ni ne les
    écrit** (`isScriptedRun` : `--seconds`, `--steps`, `--time`, `--capture`, `--input-script`) : les réglages d'un
    développeur fausseraient les clics des tests, et une session changerait la suivante (ADR-0032).
11. **Fermer, et le titre.** `FrameHooks::closeRequested` laisse le programme demander avant de quitter (M7.3 y
    proposera d'enregistrer), jamais sous `isScriptedRun`. Fermer la fenêtre ouvre la modale, une seule par image ;
    un signal (Ctrl+C, SIGTERM : platform/src/window.cpp:40-44) quitte sans demander. Le titre reste en ASCII,
    qu'exige `setWindowTitle` (platform/window.hpp:100-104) et que la boucle réécrit (app.cpp:935) : le nom de la
    scène et son « * » vont dans la barre de menus.
12. **Les menus et les barres** : Fichier, Édition, Entité, Fenêtre, Aide, une action grisée tant que son milestone
    n'est pas là ; une barre d'outils (mode, outil, vitesse de la caméra, temps réel) ; une barre d'état (le dernier
    message, les compteurs d'avertissements et d'erreurs).
13. **Les raccourcis, ceux d'Unreal, que chacun peut changer.** Une table, dans un seul fichier : par action, son nom
    (catalogue), au plus deux accords comme chez Unreal [3], un contexte. *Vol* (clic droit tenu) passe avant *Vue*
    (survolée ou en focus), qui passe avant *global* ; *jeu* ne vaut que sous la route *jeu*, seul. **Vol et outils
    vont par position** : sur l'AZERTY, déplacer, tourner, mettre à l'échelle sont Z, E, R, aux places du W, E, R
    d'un QWERTY, et s'affichent ainsi. **Les accords à Ctrl vont par lettre** (`ImGui::Shortcut`, route globale pour
    Fichier, du focus pour les outils) ; un champ de texte actif garde son Ctrl+Z, l'élément actif passant avant ces
    routes (imgui.h:1758-1762). Clic droit tenu, les touches d'outil volent ; avec Ctrl, elles ne prennent pas
    d'outil (`toolKeyWithoutModifiers`). F1 seule, et non Maj+F1, ouvre et ferme les panneaux (app.cpp:586 ne
    regarde aujourd'hui aucun modificateur). **Édition > Préférences > Raccourcis** liste les actions, filtrables ;
    un clic, puis la touche, pour changer, et pendant la saisie aucun raccourci ne part (ni `ImGui::Shortcut`, ni
    les positions, ni F1). Un accord pris est un conflit, nommé (« Ctrl+S : Enregistrer ») : rien ne change sans
    « Remplacer », qui le retire à l'autre action, comme l'*Override* d'Unreal [3]. Conflit dans un même contexte,
    et entre *global* et tout autre ; une position se compare à une lettre par `keyAtPosition`, de nouveau quand la
    disposition change (un conflit né ainsi va à la console). F1 est réservée ; « Réinitialiser » rend les défauts,
    d'une action ou de toutes. La table s'enregistre avec les préférences : une position par son numéro de scancode
    (l'usage USB, le même partout), une lettre par notre nom (« Ctrl+S »), jamais par les noms de SDL ou d'ImGui,
    instables (SDL_keyboard.h:275-282 ; imgui.h:1102). La fenêtre Aide la liste. Les défauts :

    | Action | Défaut | Par | Contexte | Source |
    |---|---|---|---|---|
    | Avancer, reculer, à gauche, à droite | W, S, A, D (Z, S, Q, D en AZERTY) | position | vol | [1] |
    | Monter, descendre | E, Q (E, A en AZERTY) | position | vol | [1] |
    | Cadrer la sélection ; vue de jeu | F ; G | position | Vue | [1] |
    | Temps réel | Ctrl+R | lettre | Vue | [1] |
    | Déplacer, tourner, échelle (M7.4) ; outil suivant | W, E, R (Z, E, R) ; Espace | position | Vue | [6] |
    | Enregistrer ; annuler (M7.3) | Ctrl+S ; Ctrl+Z | lettre | global | supposé |
    | Rétablir (M7.3) | Ctrl+Y, et Ctrl+Maj+Z (en plus, de Godot) | lettre | global | supposé |
    | Jouer ; simuler, si M7.5 le garde | Alt+P ; Alt+S | lettre | global | supposé ; [2] |
    | Arrêter (M7.5) ; rendre la souris | Échap ; Maj+F1 | position | jeu | [2] |

14. **Les chaînes passent par un catalogue** (6A) : tout texte affiché par l'éditeur et les panneaux du moteur passe par
    `ui::tr`, dont la clé est le texte français, comme le `msgid` de gettext [8]. **Un texte traduit n'est jamais un
    format** : `TextUnformatted(tr(…))`, les valeurs par `ui::trf("GPU : {} ms", x)` (`std::vformat`), des `{}` fautifs
    affichant le français ; les `ImGui::Text("… %.3f …")` de panels.cpp:66-134 y passent. **Un libellé n'est pas un
    identifiant** : fenêtres, boutons, menus et nœuds gardent un identifiant stable après `###` (`labelOf`), sans quoi
    traduire perdrait la disposition, et deux clés traduites par un même mot se heurteraient (imgui.h:2594). **Les noms
    de la réflexion** (inspector.cpp:229-396) restent ceux du code, comme chez Unity. `i18n.untranslated` refuse un
    premier argument littéral d'affichage d'ImGui, avec une lettre et sans `##` en tête, hors de `tr` (un identifiant
    seul, comme hierarchy.cpp:127, prend `##`) ; un texte construit à l'exécution lui échappe. Journaux et ligne de la
    CI restent en français, la CI les lit ; les traduire se tranche à l'ouverture de M7.8, par sondage. **M7.8** : le
    catalogue anglais, la langue dans les Préférences, une CI qui exige l'anglais de chaque clé, aux mêmes `{}`.
15. **Les polices et les icônes** (sondage « Police ») : **Inter**, sous SIL OFL 1.1, pour l'interface, Noto Sans
    (même licence) fusionnée derrière pour un glyphe manquant ; **Material Symbols**, sous Apache-2.0, pour les
    icônes. La console garde ProggyForever, la police d'ImGui (MIT, à chasse fixe), Latin-1 comme ProggyClean
    (ADR-0032, l. 93-95) : Inter, fusionnée derrière, y écrit les « nœud » des messages (gltf.cpp:378).
    `tools/fetch-assets.sh` les télécharge avec leur empreinte et leurs licences (`OFL.txt`, `LICENSE`) dans
    `assets-cache/Fonts/`, jamais versionnées, que l'éditeur lit par les racines d'assets (app.hpp:112-114) comme
    le renard ; *Rando* prend ce préfixe, `docs/SETUP.md` cite le script ; une police absente arrête l'éditeur en
    la nommant (règle n°7). Avec elles : X, Y, Z colorés (`ImGuiSliderFlags_ColorMarkers`, imgui.h:2033), une
    grille au sol par son propre `DebugLinesPass`, un appel par command list (render/debug_lines.hpp:63-66).
16. **La console** : un récepteur dans `core::log`, qui n'a aujourd'hui que `logMessage` (core/log.hpp:27-29),
    sous un mutex, car les traces et assertions de Jolt (physics_world.cpp:98-100) peuvent partir de ses threads
    (physics_world.cpp:135-138), dans un anneau borné. La fenêtre : niveaux, compteurs, recherche, Effacer, Réduire
    (le *Collapse* d'Unity). Les refus de la physique (ADR-0034, 5B) y deviennent visibles.

## Ce que l'ADR modifie

- **editor/README.md** : invariants 4, « La boucle ne change pas » (l. 30-31), qui change par les points nommés
  d'`app` décidés ici, et 7 (l. 35-38) : la hiérarchie quitte l'onglet d'« Image ».
- **engine/app/README.md** : invariants 6 (l. 45-46) *sauf caméra imposée*, 8 (l. 50-51) *sauf en vol*, 9 (l. 52-54).
- **ADR-0029** : décisions 2 (l. 66-74, `closeRequested`), 5 (l. 99-107, l'ordre), 8 (l. 121-124, la caméra).
- **ADR-0032** : la police (l. 93-95), en M7.7 et non au HUD de M8.2 ; l'ordre d'une image (l. 117-128) ; la souris
  (l. 146-154) ; `gameInputOf` (l. 158-162), qui lit la route ; `imgui.ini` (l. 178-179), à l'éditeur seul.
- **ADR-0034** : les panneaux, « sans changer la boucle » (l. 200-203) ; ce que le pas réécrit « s'éditera à
  l'arrêt, en M7.5 » (l. 186-187) : l'arrêt vient en M7.7.

## Les contrôles

Donnovan ne regardera pas : les tests prouvent tout, chaque contrôle nouveau avec son contre-test (règle n°7).

- **Le banc d'essai de l'App** (morceau 2) rejoue des `platform::Events`, paires keycode et scancode (un AZERTY se
  simule), par une API de test et `--input-script`, dans la vraie boucle hors écran (gpu/src/device.cpp:106-125)
  sous lavapipe. ImGui sans écran teste les widgets seuls (tests/editor_test.cpp:245), le GPU suit `levain_ui_gpu`
  (tests/CMakeLists.txt:374-376) ; app, render, ui, gpu et platform passent aussi au portable (sondage « Fenêtres »).
- **La ligne de la CI** « éditeur : … » (editor.cpp:18-29 ; tools/ci-programs.sh:282-294) gagne `mode`, `pas` et
  `vue`, sous linux-debug, linux-asan (ci.yml:322-327) et windows-debug (ci.yml:880-884) ; `i18n.untranslated`
  suit le modèle de `deps.imgui-visibility`.
- **Le banc d'essai des gestes** (morceau 1) relie chaque geste quotidien d'Unity et d'Unreal à son scénario :
  chaque clôture dit « N gestes sur M couverts par un test », le substitut mesurable d'« agréable ». Les coûts se
  mesurent par script, leurs chiffres sur la machine de référence seulement (règle n°6).
- **Le critère de M7.7** : en Édition, aucun pas, l'accumulateur immobile, `PlayerInput` vide touches tenues ; une
  écriture faite depuis `ui` dans l'image de la même image ; la Vue à sa taille ; chaque geste de navigation en
  QWERTY et en AZERTY ; un raccourci changé survit à un redémarrage ; un conflit nommé, sans effet avant « Remplacer ».

## Les morceaux

Chacun sous 400 lignes, relu, vérifié par `tools/verify.sh`, fusionné dans la branche `m7.7/atelier` (règle n°1).

| n° | Contenu | Modules | Lignes | Tests et contre-tests |
|---|---|---|---:|---|
| 0 | Cet ADR, ROADMAP v0.16, SPECS §5 ; les milestones GitHub M7.7 et M7.8, les échéances de M7 et M8 ; les issues | docs | 350 | — |
| 1 | Le banc d'essai des gestes : chaque geste d'Unity et d'Unreal, son scénario ; l'« État » du README de l'éditeur | docs | 200 | — |
| 2 | Le banc d'essai de l'App : des `platform::Events` rejoués (API de test, `--input-script`), l'App hors écran | platform, app, tests | 350 | Un appui scripté atteint `PlayerInput` ; une paire AZERTY donne la bonne touche d'ImGui. Contre-test : une touche inconnue dans le script est refusée bruyamment |
| 3 | Le catalogue : `ui::tr`, `ui::trf`, `ui::labelOf` et les `###` existants ; les panneaux du moteur sans format traduit ; `i18n.untranslated` | ui, app, editor, tests | 300 | La disposition retrouve ses fenêtres ; une traduction aux `{}` fautifs affiche le français. Contre-test : un `ImGui::Text("…")` sans `tr` fait rougir le contrôle |
| 4 | L'arrêt (0 pas, alpha 1, accumulateur figé) ; l'horloge des squelettes ; la recomposition sur demande après `ui` ; la caméra imposée, `startApp` compris ; le script de coût | scene, app | 350 | Un `set` fait depuis `ui` est dans l'image rendue (rouge sans la recomposition) ; 300 images à l'arrêt : `FixedStep` inchangé, puis au plus un pas ; la pose du renard ne bouge pas ; sans caméra imposée, zéro `CameraLens` arrête toujours la boucle |
| 5 | Les modes ; `frame` enveloppé ; `inputRoute` ; `mouseCaptureWanted` remis à faux ; la ligne « mode : édition ; pas : 0 » | editor, app, tools | 350 | En Édition, le `frame` du programme n'est jamais appelé et `PlayerInput` reste vide ; en jeu, les touches l'atteignent, pas ImGui, et un clic hors de la Vue n'atteint pas le jeu ; clic droit tenu au changement de mode : la souris est rendue |
| 6 | `registerUiTexture` et `releaseUiTexture`, un binding set comme `createTexture` | ui | 250 | Un `ImGui::Image` gris moyen (0,5 linéaire, 188 en sRGB, à ±2 près) relu : une double conversion se voit (Vulkan, WebGPU ; D3D12 au portable) ; un identifiant libéré quitte la table, et le dessiner fait l'assertion en Debug, l'erreur en Release |
| 7 | La scène dans une texture (format de la swapchain, cible et ressource de shader, valeur d'effacement pour D3D12), recréée une fois la taille stable, avant `ImGui::Image`, l'ancienne libérée après l'image ; la swapchain effacée, sans `PassthruCentralNode` ; F1 ; `--gpu webgpu` refusé. **CI à la main** | app, editor | 300 | Rendu en 640×360, l'aspect suit ; 30 images de glisser : au plus 2 recréations ; Vue fermée : pas de scène, une image effacée ; zéro erreur de validation sous Vulkan et D3D12 |
| 8 | La fenêtre Vue ; `App::sceneRect`, que lit le picking du sandbox ; la disposition ; le menu Fenêtre ; la ligne « vue : L×H » | app, editor, sandbox | 350 | Chaque fenêtre sur son nœud, l'inspecteur visible ; en jeu, un clic dans la Vue choisit le cube visé ; contre-test de la ligne |
| 9 | La caméra en fonctions libres, les plans de découpe, `clampEditorPitch` | editor | 300 | L'orbite garde la distance ; le cadrage et les plans contiennent la boîte ; vitesse et tangage bornés |
| 10 | La table des raccourcis, défauts seuls ; `keyAtPosition` et l'événement de disposition ; F1 sans modificateur ; les relâchements d'ImGui pendant la capture | platform, ui, app, editor | 300 | Une paire AZERTY donne Z à la place du W ; Maj+F1 laisse les panneaux ouverts. Contre-test : sans relâchement donné à ImGui, après un vol, le menu au clic droit ne s'ouvre plus |
| 11 | Les gestes (décision 6), par l'input brut ; la capture pendant le vol | app, editor | 350 | En QWERTY et en AZERTY : clic droit + la touche à la place du W avance ; rien hors de la Vue ; F cadre. Contre-test : pas de capture hors du vol |
| 12 | Les menus, les barres, la fenêtre Aide des raccourcis | editor | 350 | Ctrl+S atteint son action ; une touche d'outil, clic droit tenu, vole ; un champ texte garde son Ctrl+Z |
| 13 | `isScriptedRun` ; `closeRequested` et sa modale ; le signal qui quitte ; le titre en ASCII | platform, app, editor | 250 | Fermer ouvre la modale, jamais sous `--seconds` ni `--input-script` ; SIGTERM quitte ; un nom de scène non ASCII ne touche pas le titre |
| 14 | La console : le récepteur de `core::log`, l'anneau, la fenêtre | core, editor | 300 | Une erreur comptée ; deux threads écrivent ensemble ; l'anneau garde sa taille ; un récepteur retiré ne pend pas (ASan) |
| 15 | Disposition et préférences gardées ; les nœuds retrouvés ; « Réinitialiser la disposition » | platform, app, editor | 300 | Écrire puis relire rend la même disposition ; contre-tests : sous `--seconds`, aucun fichier écrit ; un fichier présent, `--input-script` garde la disposition et les accords par défaut ; au portable, le fichier est sous %APPDATA% |
| 16 | Édition > Préférences > Raccourcis : changer, nommer un conflit, « Remplacer », réinitialiser, enregistrer | editor | 350 | Un accord changé survit à un redémarrage ; un conflit est nommé, rien ne change sans « Remplacer » ; une lettre contre une position, en AZERTY ; pendant la saisie, Ctrl+S n'enregistre pas ; F1 refusée ; « Réinitialiser » rend les défauts |
| 17 | Polices, icônes et leurs licences (`fetch-assets.sh`, SETUP.md), style, X/Y/Z colorés, grille, « Temps réel », menu Afficher | ui, editor, tools, docs | 350 | « œ », « — », « É » et une icône, par `ImFont::IsGlyphInFont`, dans l'interface et dans la console ; police absente : arrêt qui la nomme ; la grille sans erreur de validation |
| fin | Journal, critère, gestes couverts, READMEs, PR vers `main` et sa CI | docs | — | — |

## Conséquences

- **`app` gagne sept points nommés** (l'arrêt et l'horloge des squelettes, la recomposition, la caméra imposée, la
  route, la Vue et `sceneRect`, `closeRequested`, `isScriptedRun`) et une règle de capture. Le sandbox et *Rando*
  n'en posent aucun et gardent leur boucle, sans recomposition ; leurs contrôles de CI le vérifient déjà.
- **M7.5 en hérite** : Play prend la route *jeu* et la caméra du jeu, et retire « Jouer (sans retour) ». **M7.4**
  dessine ses gizmos et lit ses clics dans le rectangle de la Vue, sans image de retard.
- **La langue n'est que celle de l'éditeur** : en anglais (M7.8), la console garde les messages du moteur en
  français tant que le sondage de M7.8 n'en décide pas autrement : « changer la langue du moteur » à moitié.
- **Les risques** : le mode relatif de SDL face à ImGui ; la table des positions, relue quand le clavier change ; la
  CI qui doit toujours voir l'inspecteur et compter plus de 0 champ, sous Linux comme sous Windows.
- **Une phase 7 plus longue** : +2,0 h pour Donnovan (ROADMAP v0.16, proposée avec cet ADR), surtout des sondages ;
  environ 60 morceaux pour l'agent sur la phase. Les polices sous OFL serviront aussi au HUD du jeu (M8.2).

## Ce que font les autres moteurs

- **Unreal** : la vue de niveau a sa caméra et les gestes de la décision 6 (**documenté** [1]), sans être un acteur
  (**supposé**, `FEditorViewportClient`). Échap arrête le jeu, Maj+F1 rend la souris, Alt+S simule (**documenté**
  [2]), Alt+P joue (**supposé**) ; W, E, R prennent l'outil, Espace le fait tourner (**documenté** [6]). Les
  raccourcis : Editor Preferences > Keyboard Shortcuts, deux par commande, un conflit averti par « Override » ([3]).
- **Unity** : la vue Scène a ses réglages de caméra, vitesse bornée et *Dynamic Clipping*, qui « calculate the
  Camera's near and far clipping planes relative to the viewport size of the Scene » (**documenté** [4]) ; la
  fenêtre Shortcuts sert à « view and manage keyboard shortcuts », avec des profils (**documenté** [5]).
- **Godot** : clic droit tenu pour regarder et voler en WASD, E et Q, la molette pour la vitesse, le bouton du
  milieu pour l'orbite (**documenté** [7]) ; ses traductions suivent gettext, où « msgid is the source string » et
  où un contexte sépare deux sens d'un même texte (**documenté** [8]), comme notre catalogue.

## Sources

1. Epic Games, *Viewport Controls* — https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine
2. Epic Games, *Playing and Simulating* — https://dev.epicgames.com/documentation/en-us/unreal-engine/playing-and-simulating-in-unreal-engine
3. Epic Games, *Customizing Keyboard Shortcuts* — https://dev.epicgames.com/documentation/en-us/unreal-engine/customizing-keyboard-shortcuts-in-unreal-engine
4. Unity, *Scene view Camera* — https://docs.unity3d.com/Manual/SceneViewCamera.html
5. Unity, *Shortcuts Manager* — https://docs.unity3d.com/Manual/ShortcutsManager.html
6. Epic Games, *Transforming Actors* — https://dev.epicgames.com/documentation/en-us/unreal-engine/transforming-actors-in-unreal-engine
7. Godot, *Introduction to 3D* — https://docs.godotengine.org/en/stable/tutorials/3d/introduction_to_3d.html
8. Godot, *Localization using gettext* — https://docs.godotengine.org/en/stable/tutorials/i18n/localization_using_gettext.html
