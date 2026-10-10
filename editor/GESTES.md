# Les gestes de l'éditeur

Le banc d'essai des gestes ([ADR-0036](../docs/adr/0036-mode-edition-vue-et-camera-de-l-editeur.md), « Les
contrôles », morceau 1) : ce qu'on fait tous les jours dans Unity et dans Unreal, et que l'éditeur de Levain doit
rendre, chaque geste relié au scénario qui le prouvera. Un éditeur aussi agréable que ceux d'Unity et d'Unreal, le
mandat de Donnovan du 2026-10-10, ne se mesure pas ; un geste couvert par un test, si. Donnovan ne regardera pas : les
scénarios tiennent lieu d'essai en main, et **chaque clôture d'un milestone de la phase 7 dit « N gestes sur M
couverts par un test »**, compté dans ce tableau (§ Compter).

Un fichier à part plutôt qu'une section du [README](README.md) : le README dit ce qu'est le module, ce tableau ce
qu'il doit devenir et sa preuve ; chaque PR qui ajoute un scénario y change une ligne, et la commande de clôture n'a
qu'un tableau à lire.

## Les règles du tableau

- **Le rang** suit la fréquence d'un geste dans une journée d'Unity ou d'Unreal, du plus fréquent au plus rare :
  notre estimation (supposé).
- **Unreal est le modèle** quand les deux diffèrent (sondage « Modèle » du 2026-10-10). Chaque affirmation sur l'un ou
  l'autre porte « documenté » et la clé de sa source (§ Sources), ou « supposé ».
- **Les touches de Levain** (ADR-0036, décisions 5 et 13) : le vol, les outils et les touches seules (F, G, Espace,
  Échap) **par position** : « pos. W » est la touche à la place du W d'un QWERTY, Z sur l'AZERTY de Donnovan (sondage
  « Clavier ») ; les accords à Ctrl ou Alt **par lettre** : Ctrl+Z est la touche marquée Z. Une touche absente de la
  table de la décision 13 est « (proposé) » : l'ADR de son milestone la fixe.
- **Milestone** : de M7.3 à M7.8 ; la commande refuse tout autre texte (« M7.3 scènes » compris : la branche va dans
  la colonne Morceau).
- **Morceau** : pour M7.7, le numéro du morceau dans le tableau de l'ADR-0036, à qui le scénario revient même si la
  ligne du morceau ne le cite pas ; ailleurs, « à venir » (l'ADR de ces milestones n'est pas écrit), suivi entre
  parenthèses de la branche de M7.3 ou du morceau de M7.7 qui en livre une part.
- **Le scénario** est le nom du test qui prouvera le geste de bout en bout, par le banc d'essai de l'App (ADR-0036,
  morceau 2 : des événements rejoués, en QWERTY et en AZERTY pour une touche à position). Il est provisoire tant que le
  geste est « à faire » : la PR qui écrit le scénario peut le renommer, ici aussi.
  Le banc sait écrire des touches et des boutons ; la position de la souris (morceaux 5 et 8), le mouvement et la
  molette (11), le texte (12) et la fermeture (13) viennent avec le morceau qui en a besoin
  ([platform/README](../engine/platform/README.md)).
- **L'état** prend l'un de trois mots, et la commande refuse tout autre :
  - `à faire` : aucun test ne prouve le geste ;
  - `couvert` : le scénario existe sous ce nom et passe ; la PR qui l'ajoute change l'état ;
  - `reporté` : sorti de son milestone, avec l'accord de Donnovan ; la colonne Milestone garde l'origine, la colonne
    Morceau dit où il va (« reporté en M7.x ») ou « écarté ».

## Les gestes

| n° | Geste | Unreal (le modèle) | Unity | Levain | Milestone | Morceau | Scénario | État |
|---|---|---|---|---|---|---|---|---|
| 1 | Regarder et voler | clic droit tenu, souris, W A S D : documenté [E1] | clic droit tenu, souris, W A S D : documenté [U1] | clic droit tenu dans la Vue ; pos. W A S D (Z Q S D) | M7.7 | 11 | `gestes.vol` | à faire |
| 2 | Sélectionner au clic dans la Vue | clic gauche : documenté [E1] | clic : documenté [U7] | clic gauche | M7.4 | à venir | `gestes.clic-vue` | à faire |
| 3 | Sélectionner dans la hiérarchie | clic dans l'Outliner : documenté [E3] | clic dans la Hierarchy : supposé | clic sur la ligne (existe : editor/src/hierarchy.cpp:137-139 ; le scénario manque) | M7.7 | 8 | `gestes.clic-hierarchie` | à faire |
| 4 | Annuler | Ctrl+Z : documenté [E9] pour le sculpt, supposé ailleurs | Ctrl+Z : documenté [U6] | Ctrl+Z | M7.3 | à venir (annulation) | `gestes.annuler` | à faire |
| 5 | Cadrer la sélection | F : documenté [E1] | F : documenté [U1] | pos. F, Vue survolée | M7.7 | 11 | `gestes.cadrer` | à faire |
| 6 | Glisser le gizmo : une étape d'annulation | une transaction par portée : documenté [E7] ; une par glissé : supposé | un appui de souris sépare les groupes : documenté [U18] | glisser une poignée | M7.4 | à venir | `gestes.gizmo-une-etape` | à faire |
| 7 | Prendre l'outil déplacer, tourner, échelle | W, E, R : documenté [E2] | W, E, R : documenté [U3] | pos. W E R (Z E R) | M7.4 | à venir (la touche : M7.7, 10) | `gestes.outils` | à faire |
| 8 | Taper une valeur, la voir dans l'image | dans l'image : supposé | dans l'image : supposé | dans la même image (l'image suivante aujourd'hui) | M7.7 | 4, 5 | `gestes.champ-meme-image` | à faire |
| 9 | Enregistrer la scène | Ctrl+S : supposé | Ctrl+S : supposé | Ctrl+S | M7.3 | à venir (scènes) | `gestes.enregistrer` | à faire |
| 10 | Monter, descendre en vol | E, Q : documenté [E1] | E, Q : documenté [U1] | pos. E et Q (E et A) | M7.7 | 11 | `gestes.vol-monter` | à faire |
| 11 | Régler la vitesse du vol | molette, clic droit tenu : documenté [E1] | molette en vol : documenté [U1] | molette, clic droit tenu | M7.7 | 11 | `gestes.vol-vitesse` | à faire |
| 12 | Orbiter autour du pivot | Alt+clic gauche : documenté [E1] | Alt+clic gauche : documenté [U1] | Alt+clic gauche | M7.7 | 11 | `gestes.orbite` | à faire |
| 13 | Zoomer | molette : documenté [E1] | molette : documenté [U1] | molette, hors vol | M7.7 | 11 | `gestes.zoom` | à faire |
| 14 | Faire glisser la vue (pan) | bouton du milieu : documenté [E1] | bouton du milieu : documenté [U1] | bouton du milieu | M7.7 | 11 | `gestes.pan` | à faire |
| 15 | Voir la sélection dans la Vue | contour : supposé | contour orange, enfants en bleu : documenté [U7] | contour | M7.4 | à venir | `gestes.contour` | à faire |
| 16 | Jouer | bouton Play ; Alt+P : supposé | Ctrl+P : documenté [U6] | Alt+P | M7.5 | à venir | `gestes.jouer` | à faire |
| 17 | Arrêter : la scène revient à l'état d'avant Play | Échap, changements perdus : documenté [E5] | Ctrl+P, une bascule : documenté [U6] pour Play, supposé pour l'arrêt ; changements perdus : documenté [U20] | pos. Échap | M7.5 | à venir | `gestes.arreter` | à faire |
| 18 | Refaire | Ctrl+Y : documenté [E9] pour le sculpt, supposé ailleurs | Ctrl+Y sous Windows : documenté [U6] | Ctrl+Y, et Ctrl+Maj+Z | M7.3 | à venir (annulation) | `gestes.refaire` | à faire |
| 19 | Dupliquer | Ctrl+W, Alt+glisser : documenté [E2] | Ctrl+D : documenté [U12] | Ctrl+W ; Ctrl+D en second accord (proposé) | M7.3 | à venir (annulation) | `gestes.dupliquer` | à faire |
| 20 | Supprimer | Suppr : supposé | Maj+Suppr : documenté [U6] | Suppr (proposé) | M7.3 | à venir (annulation) | `gestes.supprimer` | à faire |
| 21 | Renommer | F2 : supposé | F2 : supposé | F2 (proposé) | M7.3 | à venir (annulation) | `gestes.renommer` | à faire |
| 22 | Ajouter à la sélection | Ctrl ou Maj+clic : documenté [E1] | Maj ajoute, Ctrl ajoute ou retire : documenté [U7] | Ctrl ou Maj+clic (proposé) | M7.4 | à venir | `gestes.selection-ajout` | à faire |
| 23 | Vider la sélection | Échap : supposé | Échap : supposé | Échap, Vue survolée (proposé) | M7.4 | à venir | `gestes.selection-vide` | à faire |
| 24 | Courir en vol | Maj : supposé | Maj : documenté [U1] | Maj tenu (proposé : absent de la décision 13) | M7.7 | 11 | `gestes.vol-courir` | à faire |
| 25 | Cadrer depuis la hiérarchie | F dans l'Outliner : documenté [E3] ; double-clic : supposé | double-clic : supposé | double-clic sur la ligne ; pos. F, hiérarchie en focus (proposé) | M7.7 | 11 | `gestes.cadrer-hierarchie` | à faire |
| 26 | Ouvrir l'éditeur : rien ne bouge, le jeu ne reçoit rien | rien ne tourne avant Play ou Simulate : supposé | un mode Édition, Play à part : documenté [U20] | le mode Édition au lancement | M7.7 | 4, 5 | `gestes.edition-ouverture` | à faire |
| 27 | Créer une entité | panneau Place Actors : supposé | Ctrl+Maj+N, en renommage : documenté [U12] | menu Entité, clic droit dans la hiérarchie (proposé) | M7.3 | à venir (annulation) | `gestes.creer` | à faire |
| 28 | Reparenter par glisser-déposer | glisser sur un acteur l'attache : documenté [E3] | glisser sur un objet : documenté [U11] | glisser la ligne sur une autre | M7.3 | à venir (annulation) | `gestes.reparenter` | à faire |
| 29 | Ajouter un composant | bouton d'ajout : supposé | Add Component, avec recherche : documenté [U14] | bouton de l'inspecteur, avec recherche (proposé) | M7.3 | à venir (annulation) | `gestes.composant-ajouter` | à faire |
| 30 | Cacher ou désactiver une entité | H cache dans la Vue : supposé | H et l'œil de la Hierarchy : documenté [U13] ; la case de l'inspecteur : supposé | pos. H, Alt+H (proposé) ; une case dans l'inspecteur | M7.3 | à venir (annulation) | `gestes.cacher` | à faire |
| 31 | Choisir un asset, ajouter un modèle | un sélecteur dans le panneau Details : supposé | un sélecteur d'objet par champ : supposé | un sélecteur pour un champ `AssetRef` ; Entité > Ajouter un modèle… | M7.3 | à venir (scènes) | `gestes.asset-choisir` | à faire |
| 32 | Lire et filtrer la console | Output Log, erreurs en rouge : documenté [E8] ; filtres : supposé | filtres, compteurs, recherche, Collapse : documenté [U21] | fenêtre Console : niveaux, compteurs, recherche, Effacer, Réduire | M7.7 | 14 | `gestes.console` | à faire |
| 33 | Voir que la scène est modifiée | « * » : supposé | « * » dans la Hierarchy : documenté [U19] | « * » dans la barre de menus | M7.3 | à venir (scènes) | `gestes.modifie-etoile` | à faire |
| 34 | Ouvrir une scène | Ctrl+O : supposé | Ctrl+O : supposé | Ctrl+O (proposé) | M7.3 | à venir (scènes) | `gestes.ouvrir` | à faire |
| 35 | Fermer : l'éditeur demande avant de quitter | supposé | supposé | une modale, jamais sous un lancement scripté | M7.7 | 13 | `gestes.fermer-demande` | à faire |
| 36 | Mettre en pause, avancer d'une image | Pause, Frame Skip : documenté [E5] | Ctrl+Maj+P, Ctrl+Alt+P : documenté [U6] | barre d'outils ; touches à décider | M7.5 | à venir | `gestes.pause-image` | à faire |
| 37 | Simuler : la scène vit, sans jouer | Alt+S : documenté [E5] | aucun : supposé | Alt+S, si le sondage de M7.5 le garde (sinon reporté) | M7.5 | à venir | `gestes.simuler` | à faire |
| 38 | Rendre la souris en jeu | Maj+F1 : documenté [E5] | Échap : supposé | Maj+F1 | M7.5 | à venir | `gestes.rendre-souris` | à faire |
| 39 | Passer du repère local au repère monde | dans la barre d'outils : documenté [E2] ; raccourci : supposé | X : documenté [U6] | barre d'outils ; touche à décider | M7.4 | à venir | `gestes.local-monde` | à faire |
| 40 | Accrocher à la grille | X bascule, X tenu active : documenté [E2] | Ctrl tenu en glissant : documenté [U4] | à décider | M7.4 | à venir | `gestes.accrochage` | à faire |
| 41 | Passer à l'outil suivant | Espace : documenté [E1] | aucun : supposé | pos. Espace | M7.4 | à venir (la touche : M7.7, 10) | `gestes.outil-suivant` | à faire |
| 42 | Avancer vers le pivot (dolly) | Alt+clic droit : documenté [E1] | Alt+clic droit : documenté [U1] | Alt+clic droit | M7.7 | 11 | `gestes.dolly` | à faire |
| 43 | Régler la vitesse de la caméra sans voler | dans les préférences : documenté [E6] ; dans la Vue : supposé | réglages de la caméra, vitesse bornée : documenté [U2] | barre d'outils | M7.7 | 12 | `gestes.vol-vitesse-barre` | à faire |
| 44 | La hiérarchie suit la sélection faite dans la Vue | défile jusqu'à elle : documenté [E3] | supposé | défile jusqu'à la ligne | M7.4 | à venir | `gestes.hierarchie-suit` | à faire |
| 45 | Chercher dans la hiérarchie | termes partiels, liés par ET : documenté [E3] | supposé | un filtre (proposé) | M7.3 | à venir (annulation) | `gestes.hierarchie-recherche` | à faire |
| 46 | Copier, coller des entités | Ctrl+C, Ctrl+V : supposé | Ctrl+Maj+V colle en enfant : documenté [U11] ; Ctrl+C, Ctrl+V : supposé | Ctrl+C, Ctrl+V (proposé) | M7.3 | à venir (annulation) | `gestes.copier-coller` | à faire |
| 47 | Réinitialiser, retirer, copier un composant | retour à la valeur par défaut : documenté [E4] | menu ⋮ du composant : documenté [U14] | clic droit sur son en-tête (proposé) | M7.3 | à venir (annulation) | `gestes.composant-menu` | à faire |
| 48 | Glisser un nombre : une étape d'annulation | supposé | un appui de souris sépare les groupes : documenté [U18] | un glissé, une étape | M7.3 | à venir (annulation) | `gestes.champ-une-etape` | à faire |
| 49 | Taper dans un champ sans voler ni lancer de raccourci | supposé | supposé | un champ actif garde ses touches et son Ctrl+Z | M7.7 | 12 | `gestes.champ-texte` | à faire |
| 50 | Agrandir, déplacer la Vue | une fenêtre ancrée : supposé | une fenêtre ancrée : supposé | la projection suit sa taille | M7.7 | 7, 8 | `gestes.vue-taille` | à faire |
| 51 | Rouvrir une fenêtre fermée | menu Window : supposé | menu Window : supposé | menu Fenêtre | M7.7 | 8 | `gestes.fenetre-rouvrir` | à faire |
| 52 | Retrouver ou réinitialiser sa disposition | supposé | enregistrer, charger, réinitialiser : documenté [U24] | gardée ; Fenêtre > Réinitialiser la disposition | M7.7 | 15 | `gestes.disposition` | à faire |
| 53 | Revenir à un point de l'historique | Undo History : supposé | Undo History, un clic : documenté [U17] | fenêtre Historique (proposé) | M7.3 | à venir (annulation) | `gestes.historique` | à faire |
| 54 | Retrouver un raccourci | Editor Preferences > Keyboard Shortcuts : documenté [E13] | Shortcuts Manager : documenté [U29] | fenêtre Aide, la liste des raccourcis | M7.7 | 12 | `gestes.aide-raccourcis` | à faire |
| 55 | Changer un raccourci | deux accords, conflit, Override : documenté [E13] | Shortcuts Manager : documenté [U29] | Édition > Préférences > Raccourcis | M7.7 | 16 | `gestes.raccourci-change` | à faire |
| 56 | Figer ou animer la Vue (temps réel) | Ctrl+R : documenté [E1] | supposé | Ctrl+R | M7.7 | 17 | `gestes.temps-reel` | à faire |
| 57 | Voir la Vue comme le jeu | G : documenté [E1] | supposé | pos. G | M7.7 | 17 | `gestes.vue-de-jeu` | à faire |
| 58 | Sculpter le terrain | clic monte, Maj+clic descend : documenté [E9] | clic monte, Maj descend : documenté [U26] | clic, Maj+clic (proposé) | M7.6 | à venir | `gestes.sculpter` | à faire |
| 59 | Changer la taille du pinceau | aucun raccourci par défaut : documenté [E12] | [ et ] : documenté [U25] | pos. [ et ] (^ et $ sur l'AZERTY) (proposé) | M7.6 | à venir | `gestes.pinceau-taille` | à faire |
| 60 | Annuler un coup de pinceau | Ctrl+Z : documenté [E9] | supposé | Ctrl+Z | M7.6 | à venir | `gestes.pinceau-annuler` | à faire |
| 61 | Poser sur le sol | supposé | Maj+Ctrl en glissant, sur les surfaces : documenté [U3] | à décider | M7.6 | à venir | `gestes.poser-au-sol` | à faire |

## Compter

La ligne d'une clôture, pour un milestone (`m=M7.7`), ou pour la phase (`m=tout`) :

```bash
m=M7.7
awk -F'|' -v m="$m" '
  /^\| n° \|/ { table = 1; next }
  table && /^[[:space:]]*$/ { table = 0 }
  !table || /^ *\|---/ { next }
  !/^ *\|/ || NF != 11 || $2 !~ /^ [0-9]+ $/ { print "ligne " NR " : pas une ligne de geste"; bad = 1; n++; next }
  {
    if ($2 + 0 != ++n) { print "ligne " NR " : rang " ($2 + 0) ", attendu " n; bad = 1; n = $2 + 0 }
    ms = $7; e = $10; gsub(/^ +| +$/, "", ms); gsub(/^ +| +$/, "", e)
    if (ms !~ /^M7\.[3-8]$/) { print "ligne " NR " : milestone inconnu « " ms " »"; bad = 1 }
    if (e != "à faire" && e != "couvert" && e != "reporté") { print "ligne " NR " : état inconnu « " e " »"; bad = 1 }
    if (m == "tout" || ms == m) { M++; N += (e == "couvert"); R += (e == "reporté") }
  }
  END {
    if (!M) { print "aucun geste pour « " m " »"; bad = 1 }
    if (bad) exit 1
    printf "%s : %d gestes sur %d couverts par un test, %d reportés\n", m, N, M, R
  }' editor/GESTES.md
```

Elle lit le tableau de son en-tête à la première ligne vide, et échoue (code 1), en nommant la ligne, sur toute ligne
qui n'y est pas une ligne de geste (sans `|` en tête, sans numéro, sans ses neuf colonnes : un `|` dans une cellule),
sur un rang qui saute ou se répète, sur un milestone ou un état hors de leur liste, et sur un milestone sans geste :
une faute de frappe rendrait sinon « 0 gestes sur 0 », ou retirerait une ligne du compte, vert (règle n°7). Elle ne
vérifie pas qu'un scénario `couvert` existe : la relecture de la PR qui change l'état le fait, `ctest -N` à l'appui.

## Sources

Les clés de l'étude des éditeurs du 2026-10-10 ; E13 et U29 sont les sources [3] et [5] de l'ADR-0036.

- E1 https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine
- E2 https://dev.epicgames.com/documentation/en-us/unreal-engine/transforming-actors-in-unreal-engine
- E3 https://dev.epicgames.com/documentation/en-us/unreal-engine/outliner-in-unreal-engine
- E4 https://dev.epicgames.com/documentation/en-us/unreal-engine/level-editor-details-panel-in-unreal-engine
- E5 https://dev.epicgames.com/documentation/en-us/unreal-engine/playing-and-simulating-in-unreal-engine
- E6 https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-preferences
- E7 https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FScopedTransaction
- E8 https://dev.epicgames.com/documentation/en-us/unreal-engine/logging-in-unreal-engine
- E9 https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-sculpt-mode-in-unreal-engine
- E12 https://answers.unrealengine.com/questions/190805/view.html (un forum, pas la documentation d'Epic)
- E13 https://dev.epicgames.com/documentation/en-us/unreal-engine/customizing-keyboard-shortcuts-in-unreal-engine
- U1 https://docs.unity3d.com/Manual/SceneViewNavigation.html
- U2 https://docs.unity3d.com/Manual/SceneViewCamera.html
- U3 https://docs.unity3d.com/Manual/PositioningGameObjects.html
- U4 https://docs.unity3d.com/Manual/SnapIncrements.html
- U6 https://docs.unity3d.com/2018.3/Documentation/Manual/UnityHotkeys.html (la table de 2018)
- U7 https://docs.unity3d.com/Manual/SelectGameObjects.html
- U11 https://docs.unity3d.com/Manual/Hierarchy.html
- U12 https://docs.unity3d.com/Manual/hierarchy-create-gameobjects.html
- U13 https://docs.unity3d.com/Manual/SceneVisibility.html
- U14 https://docs.unity3d.com/6000.4/Documentation/Manual/UsingComponents.html
- U17 https://docs.unity3d.com/Manual/UndoWindow.html
- U18 https://docs.unity3d.com/ScriptReference/Undo.html
- U19 https://docs.unity3d.com/cn/2022.2/Manual/setupmultiplescenes.html
- U20 https://docs.unity3d.com/2022.3/Documentation/Manual/ConfigurableEnterPlayMode.html
- U21 https://docs.unity3d.com/Manual/Console.html
- U24 https://docs.unity3d.com/Manual/CustomizingYourWorkspace.html
- U25 https://docs.unity3d.com/Manual/terrain-UsingTerrains.html
- U26 https://docs.unity3d.com/6/Documentation/Manual/terrain-RaiseLowerTerrain.html
- U29 https://docs.unity3d.com/Manual/ShortcutsManager.html
