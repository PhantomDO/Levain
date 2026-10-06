# Journal de bord

Une entrée par session de travail, la plus récente en haut. Les heures Donnovan sont celles qu'il déclare ;
les chiffres de performance viennent de commandes versionnées, sur la machine de référence.

## Format d'une entrée

```
## AAAA-MM-JJ — Mx.y — titre court
- Temps Donnovan : x h (estimé y h)
- Sessions Claude Code : n
- Fait : …
- Mesures : critère → valeur (commande)
- Décisions : ADR-XXXX …
- Écarts et problèmes : …
- Prochaine étape : …
```

## Cumul

| Phase | Estimé (h) | Passé (h) | Ratio |
|---|---:|---:|---:|
| 0 | 6,0 | **5,0** | **0,83** |
| 1 | 4,5 | **3,0** | **0,67** |
| 2 | 3,75 | **4,25** | **1,13** |
| 3 | 4,5 | **5,25** | **1,17** |
| 4 | 8,25 | **6,0** | **0,73** |
| 4 (M4.6) | 3,5 | **4,5** | **1,29** |
| 5 | 11,0 | **10,0** | **0,91** |

---

## 2026-10-06 — M6.3 — Clôture : le renard monte l'escalier de Sponza, et se promène dans la vallée du navigateur

- **Temps Donnovan : 2,0 h** (estimé 1,65 h), ratio 1,21. Le 06/10, Donnovan a donné « environ 2h » pour M6.3.
  Réparti entre les issues au prorata de leurs estimations : #177 1,52 h, #178 0,48 h. Mode autonome : deux
  sondages pour l'ADR-0028 (le personnage, puis la masse qui règle la poussée), un pour la démo.
- Phase 6 à ce stade : 7,5 h passées pour 4,9 h estimées (M6.1 à M6.3), ratio 1,53, provisoire jusqu'à la
  clôture de la phase.
- Sessions Claude Code : 1 (la même que M6.1 et M6.2)
- Fait, en 12 PR (#282 à #293) :
  - **l'ADR-0028** (#282) : `CharacterVirtual` de Jolt dans le moteur, la marche (gravité, saut, accélération,
    contrôle en l'air) dans un plugin moteur, `plugins/character` ; la poussée selon la masse de la caisse (choix
    de Donnovan) ; la collision du décor tirée du maillage affiché, simplifiée à 2 cm et cuite ;
  - **le personnage dans le moteur** (#283 à #286) : marches, pentes, poussée, plateformes qui le portent,
    téléportation, puis sa glu flecs (`CharacterController`, `CharacterVelocity`, `CharacterState`) ;
  - **la marche** (#287), **la collision d'un modèle** (#288), cuite en `.lvcol` (#291) ;
  - **la démo** : `--view character`, le renard dans Sponza, un escalier dans la tranchée sud, deux rampes et des
    caisses (#289, #290) ; `--view hike`, le même renard dans la vallée, la vue par défaut de la page web (#292),
    puisque Sponza ne peut pas y être publiée ;
  - **l'étude E6** (#293) : la physique dans les moteurs, et pourquoi c'est presque toujours une bibliothèque.
- Mesures :
  - **critère de M6.3, se déplacer dans Sponza, escaliers compris** : parti du fond de la tranchée (y = −0,92), le
    renard finit sur le palier, au niveau des galeries, au sol : pieds à (0,34 ; −0,02 ; −5,73), identique sur
    RADV, sous lavapipe et en ASan (`levain_sandbox --view character --walk 1,0 --steps 200`, en CI) ;
  - dans la vallée : (206,91 ; −0,05 ; 280,02) après 120 pas, identique sur Vulkan et WebGPU (`--view hike --walk
    1,0 --steps 120`, en CI) ; 14,84 m en 10 s, à 1,6 mm du terrain, sans GPU (`levain_tests -tc='*renard de la
    démo*'`) ;
  - la collision de Sponza : 262 267 triangles bruts, 32 073 sans feuillage et simplifiés à 2 cm, cuits en 64 ms
    et relus en moins d'une milliseconde (`levain_cook assets-cache` puis `--view character --steps 2`, Release) ;
  - la tenue sur une pente : moins de 5 mm de glissement en 3 s sur 30°, contre 21 cm sans `StandStill`
    (`levain_tests -tc='*pente de 30*'`, la mutation retirée) ;
  - la page web : environ 60 images/s dans Firefox WebGPU, headless (`tools/web-smoke.mjs`) ;
  - tests : 256 en natif (260 en Release), 172 en WebAssembly (`ctest`).
- Décisions de Donnovan, par sondage : la marche dans un plugin ; la collision la plus performante ; la poussée
  selon la masse de la caisse ; la démo du renard, caméra qui suit. ADR-0028.
- Écarts et problèmes :
  - cinq PR dépassent la règle n°2, toutes signalées : le personnage (#284, 588 lignes), sa glu flecs (#286,
    463), la marche (#287, 525), la collision d'un modèle (#288, 575) et la vue du personnage (#290, 590) ; les
    tests en font souvent près de la moitié ;
  - LeakSanitizer : avec lavapipe installé à côté de RADV, les tests GPU échouent en local sur une fausse fuite si
    un seul pilote n'est pas imposé (`build/GOTCHA.md`, entrée du 05/10) ; la CI n'est pas concernée ;
  - la foulée de Fox n'est pas mesurée : ses pieds peuvent glisser un peu ;
  - #292 et #293 sont d'abord passées au rouge sur le format : un commentaire modifié par script dans la
    correction de relecture, poussé sans relancer le contrôle sur tout l'arbre (`build/GOTCHA.md`, entrée du
    jour).
- Après un essai sur une vieille tablette (Chromium, environ 10 images/s), Donnovan a demandé les images/s, la
  machine et l'occupation dans la page web, et remarqué que Tracy ne servait plus. Décidé par sondage, avant M6.4 :
  le calque des mesures (#294) et Tracy remis en service, avec une capture de la vallée (#295). Réponse archivée
  dans `docs/QA.md`.
- Prochaine étape : #294, puis #295, puis M6.4, la caméra à la troisième personne, dans le dépôt du jeu.

## 2026-10-05 — M6.2 — Clôture : des volumes, des rayons, un contour à la souris, et la vallée qui collisionne

- **Temps Donnovan : 2,54 h** (estimé 1,5 h), ratio 1,69. Le 05/10, Donnovan a donné « environ 5 à 6 h en
  tout » pour M6.1 et M6.2 ensemble. On retient 5,5 h, réparties entre les deux milestones au prorata de leurs
  estimations, puis entre les issues (#175 et #176 : 1,27 h chacune) : le ratio est donc le même pour les deux.
  Mode autonome : deux sondages du matin (l'ADR-0027, puis le flux de CI et le cache des assets cuits) et deux
  questions, archivées dans `docs/QA.md`. À elles seules, les estimations de relecture des dix PR (2,2 h)
  dépassaient déjà l'estimé.
- Phase 6 à ce stade : 5,5 h passées pour 3,25 h estimées (M6.1 et M6.2), ratio 1,69 (1,54 à 1,85 selon les 5 à
  6 h données), au-dessus de la fourchette 0,8–1,25. Chiffre provisoire : le recalibrage se décide à la clôture
  de la phase (GOTCHA de `session`, « ne pas tirer de conclusion de phase sur des chiffres provisoires »).
- Sessions Claude Code : 1 (la même que M6.1)
- Fait, en 10 PR (#269 à #278, dont #270 pour la méthode) :
  - **l'ADR-0027** (#269) : les volumes posent une relation `(InsideOf, volume)` sur le corps, mais l'API parle du
    point de vue du volume (choix de Donnovan) ; des capteurs rendus cinématiques par le moteur (son choix) ;
    les requêtes ;
    les grandes formes partagées ; les lignes de debug ;
  - **les grandes formes** (#271) et **la collision de la vallée** (#272) : maillages et grilles de hauteurs
    partagés, construits une fois par donnée ; le terrain donne sa heightmap à Jolt ;
  - **les requêtes** (#273) : un rayon qui ne voit que les surfaces qu'il traverse en entrant, comme celui d'Unity,
    et une sphère lancée qui rend la normale du contact ;
  - **les lignes de debug** (#274) dans `render`, et **les contours** des formes (#275), en fonctions pures ;
  - **la sélection à la souris** (#276), le critère du milestone ;
  - **les volumes déclencheurs** (#277) et **la démo du lac** (#278), vérifiée par un test sans GPU ;
  - **la méthode** (#270, sur une question de Donnovan) : les PR d'un milestone empilées, leurs CI en
    parallèle, fusionnées en merge commit ; les assets cuits en cache dans la CI.
- Mesures :
  - **critère de M6.2, sélectionner un objet à la souris par raycast** : en CI, sur les trois presets et les deux
    backends, `levain_sandbox --view physics --pick 960,540` sélectionne une caisse (CI de #278) ; le pixel touche
    une caisse de 0,1 à 8 s, grille de départ, chute et tas compris (`--seconds N --pick 960,540`, 11 durées) ;
  - les volumes : la démo du lac, simulée 10 s sans GPU, met 64 caisses sur 64 dans le lac (`ctest -R "rive est"`,
    seuil 32) ;
  - le critère de M6.1 avec le `ContactListener` branché : 0,54 à 0,59 ms par pas en moyenne, 1,3 à 1,9 ms au pire,
    trois lancements (`levain_physics_bench`, Release, Ryzen 7 7800X3D) ; 1,74 ms et 3,3 ms sur un thread ;
  - la scène de fumée `lines` : 1 pixel sur 4 096 diffère entre RADV et lavapipe, 23 si le test de profondeur est
    coupé (`levain_smoke_render lines [webgpu]`, `VK_DRIVER_FILES` sur lavapipe) : 4 admis ;
  - tests : 218 en natif (220 en Release), 133 en WebAssembly (`ctest`, CI de #278) ;
  - la CI des neuf PR empilées : **18 min 24 pour la première vague**, toutes en parallèle (de 09:40:55 à 09:59:19,
    `gh run list --workflow ci.yml`), au lieu d'environ 1 h 30 estimées en série ; puis 10 min pour les deux
    corrigées, et 10 pour #271 rouverte.
- Décisions de Donnovan, par sondage : l'API des volumes côté volume ; les capteurs cinématiques ; les PR empilées
  et leur CI en parallèle ; le cache des assets cuits. ADR-0027 ; règle n°1 amendée (AGENTS.md).
- Écarts et problèmes :
  - dix relectures, dont trois ont trouvé un bloquant avant fusion : un maillage cinématique arrêtait Jolt en
    Debug (masse nulle), la référence des lignes aurait échoué sur lavapipe (lavapipe installé dans la distrobox
    pour le voir), et le premier cache des assets cuits n'aurait jamais servi (des GUID tirés au hasard à chaque
    run) ;
  - deux PR dépassent les 400 lignes : les grandes formes (454, dont 157 de tests) et les volumes (577, dont 200
    de tests et 59 de README ; `gh pr view <N> --json additions`), signalées ; trois découpes à la relecture (la
    collision du terrain sortie des formes, la sélection des contours, la démo du lac des volumes) ;
  - quatre images de tests de fumée en échec avaient été commitées par un `git add -A` : retirées, et
    `*.actual.ppm` ignoré ;
  - la CI empilée a trouvé deux échecs du sommet que la vérification locale avait manqués : `set_threads` avorte en
    WebAssembly, et un contrôle de la vue terrain ignorait l'étape de sélection ;
  - supprimer la branche fusionnée a **fermé** la PR suivante au lieu de la rediriger vers `main` (#271, rouverte
    et relancée) : `tools/merge-stack.sh` fusionne désormais une pile dans le bon ordre (GOTCHA de `session`) ;
  - le cache des assets cuits sert dès la première vague : quatre PR (lignes de debug, contours, sélection,
    volumes) l'ont restauré et cuit en 1 s ; cinq, lancées avant que celui de `main` existe (09:48:55), ont cuit
    en 2 min 29 à 4 min 11 et enregistré chacune le leur (160 Mo). Sur `main` après la pile : restauré en 3 s,
    cuisson en 1 s (run 37295565127). Le cache Actions du dépôt est à 9,8 Gio pour 10
    (`gh api repos/PhantomDO/Levain/actions/cache/usage`) : les caches des PR fusionnées sont à supprimer.
- Prochaine étape : M6.3, le character controller (`CharacterVirtual` de Jolt, et son corps intérieur sur la
  couche `Character`, que les volumes verront déjà).

## 2026-10-05 — M6.1 — Clôture : Jolt derrière nos types, et 1 000 caisses qui tombent

- **Temps Donnovan : 2,96 h** (estimé 1,75 h), ratio 1,69 : la part de M6.1 dans les 5,5 h retenues (Donnovan
  a donné « environ 5 à 6 h en tout » le 05/10 pour M6.1 et M6.2), réparties au prorata des estimations (#173 :
  0,42 h, #174 : 2,54 h). Premier milestone du mode autonome (04/10) : Donnovan a répondu aux sondages, un
  subagent a relu chaque PR, l'agent a fusionné après la CI verte. À elles seules, les estimations de relecture
  des sept PR (2,1 h) dépassaient déjà l'estimé.
- Sessions Claude Code : 1 (interrompue deux fois par une limite de l'API, reprise sans perte)
- Fait, en 7 PR (#261 à #267) :
  - **l'ADR-0026** (#261) : un corps se déclare par un `Collider` et un `RigidBody`, comme dans Unity ; Jolt
    fait autorité sur les corps dynamiques, flecs recopie ; une table de couches fixe ; le pool de threads de
    Jolt en natif, un seul thread dans le navigateur ;
  - **des phases de simulation ordonnées** (#262) : `Simulation` (le gameplay), `Physics`, `PostPhysics`, dans
    le pipeline du pas fixe, et non dans le pipeline par défaut de flecs ;
  - **le monde Jolt derrière nos types** (#263) : `PhysicsWorld` opaque et des fonctions libres, les couches
    fixes, des pas déterministes (même état au bit près sur un thread ou sur trois), les entrées refusées
    bruyamment avant que Jolt n'en fasse des NaN en Release, où ses assertions n'existent pas ;
  - **la glu flecs** (#264) : les corps suivent les entités (création, retrait, téléportation, refus
    réversible), un point de synchronisation avant le pas pour les écritures différées du gameplay ;
  - **des instances qui tournent** (#265) : une position et un quaternion par instance, lus par les passes
    des meshes et des ombres ;
  - **un correctif intercalé** (#266) : un « ; » dans le nom d'un cas coupait un test de #263 en deux entrées
    ctest qui n'exécutaient rien ; une entrée vide échoue désormais. Le garde-fou en a aussitôt trouvé deux
    autres dans la pile de M6.2, avant leur fusion ;
  - **la démo et le banc** (#267) : 1 000 caisses lâchées dans `levain_sandbox --view physics`, sur les trois
    presets et les deux backends en CI.
- Mesures (Release, Ryzen 7 7800X3D, 15 threads de travail, `./build/linux-release/tests/levain_physics_bench`,
  600 pas, trois lancements) :
  - **critère de M6.1, aucun pas au-dessus de 4 ms : 0,52 ms par pas en moyenne, 1,1 à 2,4 ms au pire** ; le
    pire pas est bruité : une contre-mesure de la relecture atteint 2,6 ms une fois sur trois ;
  - sur un seul thread, le réglage du navigateur, 1,7 ms et 3,3 à 3,6 ms au pire (`levain_physics_bench 0`),
    en natif : le pas n'est pas mesuré en WebAssembly, où il sera vraisemblablement plus lent ;
  - le pas qui construit les 1 001 corps : 1,4 ms (2,5 sur un thread) ;
  - la vue physics dans Firefox, en WebGPU : 61 images/s (`tools/web-smoke.sh`), sans doute plafonnées par
    l'affichage : le chiffre ne dit rien du pas ;
  - tests : 194 en natif (196 en Release), 114 en WebAssembly (`ctest`, CI de #267).
- Décisions de Donnovan, par sondage : `Collider` et `RigidBody` ; Jolt fait autorité ; couches fixes ; threads
  de Jolt en natif, un seul dans le navigateur ; l'agent fusionne après relecture par un subagent et CI verte ;
  le sandbox web publié en Artifact privé claude.ai. ADR-0026.
- Écarts et problèmes :
  - réinstaller l'état global de Jolt plantait en WebAssembly (`Factory::Register`) : installé une fois par
    processus ;
  - une relecture a trouvé l'absence du point de synchronisation : un `set` différé du gameplay arrivait après
    le pas. Contre-test fait (x = 0 au lieu de 10 sans lui) ;
  - une relecture a trouvé que `destroyBody` sur le corps invalide lisait hors du tableau de Jolt en Release :
    garde ajoutée ;
  - une première version de l'entrée GOTCHA disait Jolt sans assertions même en Debug : faux, corrigé après
    vérification (`nm` sur les deux bibliothèques) ;
  - trois PR dépassent les 400 lignes, hors de l'exception Vulkan : #263 (971, dont les tests du monde), #264
    (819, la glu et ses tests flecs) et #267 (432, le banc et sa scène). Signalé dans chaque PR, sans découpe
    naturelle trouvée ;
  - pièges ajoutés à `build/GOTCHA.md` : LLVM 23 dans la distrobox contre 22 en CI, Firefox en Flatpak pour
    `tools/web-smoke.sh`, le « ; » dans un nom de cas, LeakSanitizer avec lavapipe ;
  - le jeton `gh` a perdu le scope `read:project` : le board n'a pas pu être mis à jour (à rafraîchir par
    Donnovan, `gh auth refresh -s read:project,project`) ; rafraîchi depuis, board à jour le 05/10.
- Prochaine étape : M6.2, déjà écrite et relue (neuf PR empilées : ADR-0027, grandes formes, collision du
  terrain, requêtes, lignes de debug, contours, sélection, volumes, démo du lac).

## 2026-10-03 — M5.7 — Clôture : l'eau et l'herbe, et la fin de la phase 5

- **Temps Donnovan : 2,0 h** (estimé 1,75 h). Réparti au prorata des estimations : #136 0,55 h, #137 1,0 h,
  #138 0,45 h. Ratio 1,14. La correction des mips (#246), hors du périmètre, est comptée dedans.
- **Phase 5 close : 10,0 h pour 11,0 estimées, ratio 0,91**, dans la fourchette : aucun recalibrage. Le
  cumul des phases 0 à 5 vaut 0,92 (38,0 h pour 41,5).
- Sessions Claude Code : 1
- Fait, en 6 PR (#244 à #249) :
  - **le plugin `water`** (#244, #245) : un creux de lac dans la vallée, une carte de vaguelettes calculée par
    le code et répétable sans couture, et le lac dessiné à l'étape `Transparent`, premier dessin de cette étape.
    Le ciel s'y reflète selon le Fresnel, le soleil y brille, et le fond se voit au travers d'une eau qui
    absorbe le rouge d'abord. La profondeur se lit dans la heightmap du terrain, dont le plugin dépend ;
  - **la correction des mips des textures de données** (#246) : les normal maps et les rugosités avaient leurs
    mips moyennés en sRGB, ce qui penchait les normales au loin. Le cuiseur apprend l'usage de chaque image des
    matériaux glTF, choix de Donnovan au sondage (amendement de l'ADR-0020) ;
  - **le plugin `grass`** (#247, #248) : des brins instanciés sur le GPU, sans buffer d'instances. Le numéro
    d'instance donne la place (suite R2 de Roberts), un hachage le reste, et la carte de densité (le poids de
    l'herbe du terrain, hors de l'eau) dit où ils poussent. Le vent les plie ;
  - **l'étude E5** (#249) : forward, deferred et forward+ dans Unreal, Unity et Godot.
- Mesures (Release, RX 9070 XT, 1920 × 1080, `levain_sandbox --seconds 10 --view terrain [--camera 250,3,300]`) :
  - **critère de M5.7, la vallée avec son lac et son herbe à plus de 60 images/s : 0,94 ms GPU par image** au
    bord du lac, avec 241 000 brins demandés (1 060 images/s hors écran), et 0,58 ms depuis la crête ;
  - l'herbe coûte 0,24 ms (0,545 ms d'opaques sans elle, 0,786 avec) ; l'eau 0,004 ms vue de la crête, 0,037 ms
    quand elle couvre la moitié de l'écran ;
  - Vulkan et WebGPU donnent la même image : 0,06 % d'écart moyen pour l'eau, 0,04 % pour l'herbe ;
  - les mips corrigés ne changent pas l'écart à la visionneuse Khronos (0,77 en IBL, 1,64 en direct,
    `tools/khronos-compare.sh`), et changent Sponza de 0,1 niveau sur 255 en moyenne, sur les surfaces
    lointaines et rasantes ;
  - tests : 169 en natif, 88 en WebAssembly (`ctest`).
- Décisions de Donnovan, par sondage : l'usage des textures appris des matériaux glTF (ADR-0020, amendement du
  03/10).
- Écarts et problèmes :
  - la CI ne passait ni le format ni clang-tidy sur `plugins/` : le terrain y échappait depuis #239. Corrigé
    en #244 ; il passait les deux contrôles ;
  - un contrôle de la CI que j'avais ajouté lisait le temps GPU de l'étape transparente, que le backend WebGPU
    ne mesure pas : il échouait sous WebGPU. Il ne tourne plus que sous Vulkan, et le commentaire le dit ;
  - deux PR dépassent les 400 lignes, signalées comme blocs GPU : le rendu de l'eau (442) et celui de l'herbe
    (525) ;
  - zsh applique un modificateur à `$C:r…` dans un refspec : consigné dans `build/GOTCHA.md` ;
  - **ponytail** : seul le ciel se reflète dans l'eau, pas les versants ; un objet dans l'eau ne l'assombrit
    pas (la profondeur vient du terrain) ; l'herbe ne projette pas d'ombre ; un pixel de brin caché par un autre
    est éclairé pour rien, et une pré-passe de profondeur l'éviterait (étude E5).
- Prochaine étape : la phase 6, physique et traversée, en commençant par l'ADR d'intégration de Jolt (#173).

## 2026-10-03 — M5.6 — Clôture : le terrain, premier plugin moteur, dans un renderer à étapes

- **Temps Donnovan : 2,0 h** (estimé 1,25 h). Réparti au prorata des estimations : #134 0,4 h, #135 1,6 h.
  **Ratio 1,6** : le milestone a porté plus que le terrain, avec le renderer qui possède l'image et son registre
  d'étapes, que demandait le choix de l'ADR-0025.
- Sessions Claude Code : 1
- Fait, en 7 PR (#236 à #242) :
  - **l'ADR-0025** (#236) : un registre par étape, dans un renderer qui possède l'image, et l'éclairage partagé.
    C'est Donnovan qui a choisi le registre, contre les passes explicites que proposait Claude ; l'application y
    inscrit les plugins, `render` ne voyant pas flecs ;
  - **`FrameBindings` et `lighting.slang`** (#237), puis **le renderer et ses étapes** (#238) : `ShadowCasters`,
    `Opaque`, `Transparent`. Les dessins du sandbox s'y inscrivent comme ceux d'un plugin. Aucun pixel ne change ;
  - **le plugin terrain** (#239) : la vallée procédurale de 512 m, ses parcelles, leurs niveaux de détail et les
    poids des couches, en fonctions CPU testées ;
  - **le rendu du terrain** (#240), éclairé par `shadeSurface` et projetant son ombre ; **les niveaux de détail et
    leurs jupes** (#241) ; **les couches Poly Haven** mélangées par la carte de poids (#242).
- Mesures (Release, RX 9070 XT, 1920 × 1080, `levain_sandbox --seconds 10 --view terrain`) :
  - **critère de M5.6, une vallée de 500 m à 1 m de résolution en moins de 2 ms GPU : 0,61 ms** (0,603 à 0,609 sur
    trois lancements) ;
  - les niveaux de détail divisent les triangles de la caméra par 17 (518 000 → 29 770), et l'image ne change
    presque pas : 0,09 d'écart moyen sur 255 ;
  - lire seulement les couches de poids non nul fait passer les opaques de 1,03 à 0,50 ms ;
  - Vulkan et WebGPU donnent la même image, à 1 niveau près sur 255 ;
  - tests : 162 en natif, 87 en WebAssembly (`ctest`).
- Décisions de Donnovan, par sondage : le registre par étape et l'éclairage partagé (ADR-0025), l'inscription
  par l'application, un relief généré par le code, et trois textures Poly Haven 1k.
- Écarts et problèmes :
  - les noms de Poly Haven trompaient : `grass_path_2` est un gravier, et `rocky_terrain_02` une herbe. Le sondage
    les annonçait à l'envers ; ce sont les mêmes textures, rangées selon leur aperçu ;
  - trois pièges WebGPU, consignés dans `build/GOTCHA.md`, trouvés par la validation :
    - ni R16 normalisé ni R32 filtrable dans le cœur de WebGPU, d'où une heightmap en R16 flottant ;
    - `textureSample` est interdit dans une branche qui dépend des données ;
    - deux pipelines qui partagent un binding set doivent avoir le même layout ;
  - une erreur NVRHI : un buffer volatil jamais écrit ne se lie pas. Le renderer écrit désormais les constantes de
    scène à chaque image ;
  - le hot-reload ignorait les fichiers inclus (`brdf.slang` depuis #231) : corrigé, et vérifié par
    `tools/shader-hot-reload.sh`, qui était lui-même cassé depuis le PBR ;
  - **ponytail** : la projection plane étire la roche dans les pentes raides ; les shaders d'un plugin ne se
    rechargent pas à chaud.
- Prochaine étape : M5.7, l'eau et l'herbe, deux autres plugins moteur.

## 2026-10-03 — M5.5 — Clôture : le frustum culling, et le temps de chaque passe

- **Temps Donnovan : 1,0 h** (estimé 1,25 h). Réparti au prorata des estimations : #132 0,4 h, #133 0,6 h.
  Ratio 0,80.
- Sessions Claude Code : 1
- Fait :
  - **le frustum culling sur CPU** (#233) : chaque mesh connaît sa boîte, chaque dessin est testé contre les six
    plans du volume de vue, pour la caméra comme pour chaque cascade d'ombres ; un mesh skinné n'est jamais
    écarté ;
  - **le temps GPU de chaque passe** et **les appels de dessin et triangles par image** (#234), vérifiés en CI
    sur le renard.
- Mesures (Release, RX 9070 XT, 1920 × 1080) :
  - culling sur Sponza (`levain_sandbox --model Sponza.gltf`, ligne « culling, par image ») : depuis la caméra
    de départ, 22 dessins écartés sur 105 pour la caméra et 48 sur 420 pour les ombres ; de l'intérieur
    (`--view khronos --camera 8,3,0`), 89 sur 103 et 72 sur 412. Captures identiques avec et sans culling ;
  - **critère de M5.5 : Sponza en PBR, avec ombres et IBL, à 0,645 ms GPU par image** (0,642 à 0,646 sur trois
    lancements de 10 s), soit 1 500 images/s hors écran, pour un critère de 60 ;
  - par passe : clusters 0,005 ms, ombres 0,309, meshes 0,288, ciel 0,003, tonemapping 0,016 ; par image, 83
    appels et 372 000 triangles pour la caméra, 372 appels et 1,2 million de triangles pour les ombres ;
  - tests : 152 en natif (`ctest`).
- Écarts et problèmes : un rebase de pile raté, la branche de base ayant été supprimée par la fusion ; consigné
  dans `session/GOTCHA.md`.
- Prochaine étape : M5.6, le terrain.

## 2026-10-03 — M5.4 — Clôture : l'éclairage par le ciel, mesuré contre le glTF Sample Viewer

- **Temps Donnovan : 2,0 h** (estimé 2,25 h). Réparti au prorata des estimations : #130 1,12 h, #131 0,44 h,
  #125 0,44 h. Ratio 0,89.
- Sessions Claude Code : 1
- Fait, en 12 PR (#220 à #231) :
  - **l'HDRI du ciel** (#220) : Kloofendal (Poly Haven, CC0), en lumière linéaire ;
  - **le backend WebGPU** lit des cubemaps et écrit des storage textures (#221) ;
  - **l'environnement** (#222, #223) : la cubemap et ses mips, puis l'irradiance, le spéculaire préfiltré et la
    table BRDF, en compute, à l'échantillonnage filtré ;
  - **les meshes éclairés par le ciel** (#224), **le ciel en fond** (#225), **le soleil tiré de l'HDRI** (#226),
    **Kloofendal par défaut**, 2k en natif et 1k sur la page web (#227) ;
  - **la comparaison avec Khronos** :
    - le tonemapper PBR Neutral (#228) ;
    - `tools/khronos-compare.sh` (#229) : le viewer hébergé dans Firefox, sa caméra relue dans ses appels WebGL,
      et l'écart sphère par sphère ;
    - deux corrections qu'elle a trouvées : la compensation des rebonds multiples pour l'IBL (#230), et une BRDF
      directe qui mélange un diélectrique et un métal, comme l'annexe B de la spécification glTF (#231).
- Mesures :
  - environnement calculé au chargement en **20 ms sur Vulkan, 32 ms sur WebGPU** (Kloofendal 2k, Release, RX 9070
    XT, trois lancements), de 0,2 à 2,1 s sur lavapipe en CI (`levain_sandbox`, ligne « environnement calculé ») ;
  - les convolutions vérifiées sur Vulkan et WebGPU, aux mêmes valeurs (`gpu.environment.*`) : un ciel uniforme
    reste lui-même, un demi-ciel blanc donne une irradiance de 0,983 vers le zénith et 0,499 à l'horizon ;
  - **écart avec le glTF Sample Viewer**, ΔE76 moyen sur les 49 sphères de MetalRoughSpheres
    (`tools/khronos-compare.sh <dossier> ibl|direct`) :
    - sous le ciel : **3,75 → 0,77** (pire 13,6 → 1,6) ;
    - en lumière directe : **4,42 → 1,64** (pire 8,3 → 3,3) ;
  - le soleil de Kloofendal culmine à 72 559, au-delà du plus grand flottant 16 bits ; retiré du ciel, il devient
    une lumière d'intensité 4,30 à 48° d'élévation ;
  - tests : 149 en natif, 87 en WebAssembly (`ctest`).
- Décisions de Donnovan, par sondage : l'HDRI Kloofendal, des cubemaps, le soleil tiré de l'HDRI, Kloofendal par
  défaut (1k sur la page web), la mesure par le viewer hébergé dans Firefox, la compensation comme le viewer, et
  #125 mesuré l'IBL coupée.
- Écarts et problèmes :
  - **un défaut d'avant M5.4, trouvé par la mesure** : la BRDF directe retirait deux fois la part du métal au
    diffus ; une surface à moitié métallique sortait jusqu'à trois fois trop sombre ;
  - le viewer ne place pas sa caméra où le dit sa propre formule de cadrage : la relire dans la page évite de le
    deviner ;
  - deux pièges consignés (`build/GOTCHA.md`) : hors écran, rien ne freinait la boucle WebGPU, et la fermeture
    attendait toutes les images empilées (plus de 2 min sur lavapipe) ; un itérateur de libc++ n'est pas un
    pointeur ;
  - le temps affiché dans le navigateur ne compte que l'enregistrement des passes : `waitForIdle` n'y attend pas le
    GPU.
- Prochaine étape : M5.5, le culling et les statistiques.

## 2026-10-02 — M5.3 — Clôture : les ombres du soleil en quatre cascades

- **Temps Donnovan : 1,0 h** (estimé 1,75 h), déclaré en plus des 2 h de M5.1 et M5.2 : 3,0 h pour la journée.
  Réparti au prorata des estimations : #128 0,57 h, #129 0,43 h. **Ratio 0,57, provisoire** : les PR #214 à
  #218 ont été fusionnées sur son accord et seront relues plus tard.
- Sessions Claude Code : 1
- Fait :
  - **les cascades** (#214) : quatre tranches de profondeur au partage « pratique » (Zhang et al.), chacune
    enfermée dans une sphère, la projection du soleil calée sur les texels, pour des bords qui ne scintillent pas ;
  - **le backend WebGPU lit une profondeur par un sampler de comparaison** (#215) : il déduit des ressources et
    du WGSL ce que le layout de NVRHI ne dit pas ;
  - **la passe d'ombres** (#216) : la profondeur vue du soleil, dans un atlas de 2 × 2 cascades (4096²) ;
  - **la lecture des ombres** (#217), puis **le PCF** 3 × 3 et **le temps GPU de la passe** (#218).
- Mesures :
  - pas de trou entre cascades : 4 761 vérifications sur CPU, chaque point du volume de vue dans la shadow map de
    sa cascade (`levain_tests -tc="*cascade*"`) ;
  - l'ombre d'un cube sur un sol : 0 pixel différent de la référence, sur Vulkan et sur WebGPU
    (`ctest -R smoke.shadow`) ; le cube, dans Firefox (`tools/web-smoke.sh`) ;
  - **temps GPU de la passe d'ombres sur Sponza : 0,27 ms** (0,269 à 0,274 sur trois lancements de 10 s), pour
    une image à 0,56 ms GPU ; 4 cascades, atlas 4096², 1920 × 1080, Release, RX 9070 XT (RADV)
    (`SDL_VIDEO_DRIVER=offscreen levain_sandbox --seconds 10 --model Sponza.gltf`) ;
  - tests : 143 en natif, 86 en WebAssembly (`ctest`).
  - la scène `shadow` du test de fumée admet 16 pixels différents : le bord de l'ombre tombe d'un texel à côté
    entre RADV et lavapipe (8 pixels en CI) ; le contrôle du temps GPU des ombres se fait en CI sur le renard,
    Sponza n'y faisant que quelques images.
- Décisions : un atlas plutôt qu'un tableau de textures (une seule texture 2D, la même sur Vulkan et WebGPU) ; les
  deux faces dessinées dans les ombres (rideaux, feuillages).
- Écarts et problèmes :
  - deux pièges, consignés : NVRHI n'active le biais de profondeur sous Vulkan que si sa part constante est non
    nulle (acné sur Vulkan seulement) ; une texture comparée doit être une `DepthTexture2D` pour le WGSL ;
  - **ponytail** : une marge fixe de 50 m vers le soleil pour les objets qui projettent leur ombre dans une
    tranche (`CasterMargin`) ;
  - pas de temps GPU sur WebGPU (pas de `timestamp-query`).
- Prochaine étape : M5.4, l'éclairage d'environnement (IBL), et la comparaison avec la visionneuse Khronos (#125).

## 2026-10-02 — M5.1 et M5.2 — Clôture : la scène en PBR, en forward+ en clusters, en HDR et en AgX

- **Temps Donnovan : 2,0 h** en tout pour la journée (estimé 2,75 h) : l'ADR-0024 et son sondage, le choix
  d'AgX sur captures, le report de #125, les relectures du jour. Réparti au prorata des estimations : #123
  0,18 h, #124 0,73 h, #202 0,36 h, #126 0,36 h, #127 0,37 h. **Ratio 0,73, provisoire** : les PR #204 à #212
  ont été fusionnées sur son accord (« merge et continue ») et seront relues plus tard. Ce temps s'ajoutera.
- Sessions Claude Code : 1
- Fait :
  - **ADR-0024** (#203) : un rendu forward+ en clusters dès M5.1, par décision de Donnovan ; les clusters
    plutôt que les tuiles, pour que l'eau transparente s'éclaire par la même grille ;
  - **le tri des lumières en clusters** (#204, #205) : une grille de 16 × 9 × 24 cases, un compute qui y
    range les lumières ponctuelles, vérifié contre une référence CPU sur Vulkan et sur WebGPU ;
  - **les sommets** portent une normale et une tangente, jusque dans le skinning (#206) ; **les matériaux
    glTF** sont lus en entier, rugosité-métal et normal maps comprises (#207), et arrivent au GPU (#208) ;
  - **l'éclairage PBR** (#209) : Cook-Torrance (GGX, Smith, Schlick), un soleil, les lumières du cluster du
    pixel, une lumière ambiante en attendant l'IBL ;
  - **l'image HDR** (#210) et **le tonemapping** (#211) : AgX par défaut, ACES et la coupe nette à la demande ;
  - **la page web** dit ses erreurs critiques au lieu de rester muette (#212) ;
  - **hors milestone** : l'ordre des liens de `gpu` corrigé (#201), et *Rando* aligné sur le moteur (Rando#4).
- Mesures :
  - tri des lumières : 0 cluster différent du CPU sur 3 456, dont 160 au-delà de la limite de 32, sur Vulkan
    et sur WebGPU (`ctest -R light-clusters`) ;
  - Sponza en PBR, normal maps comprises : 69 textures, 0 erreur de validation en Debug, sur Vulkan et sur
    WebGPU (`levain_sandbox --gpu webgpu --model Sponza.gltf`) ;
  - le cube du test de fumée éclairé : 0 pixel différent de la référence, en natif sur les deux backends et
    dans Firefox (`ctest -R smoke`, `tools/web-smoke.sh`) ;
  - renard, camion, Sponza : mêmes images sur Vulkan et WebGPU (captures `--time 2` comparées) ;
  - tonemapping : 6 captures par modèle, 3 courbes × 2 expositions (`tools/tonemap-captures.sh`) ;
  - la page publiée tourne à 61 images/s dans Firefox (`node tools/web-smoke.mjs
    https://phantomdo.github.io/Levain/ - …`) ;
  - tests : 133 en natif, 86 en WebAssembly (`ctest`).
- Décisions : l'[ADR-0024](adr/0024-forward-plus-en-clusters.md) ; AgX plutôt qu'ACES, sur captures ; #125 (la
  comparaison avec la visionneuse Khronos) reportée en M5.4, après l'IBL, la visionneuse éclairant par une
  carte d'environnement.
- Écarts et problèmes :
  - trois bugs trouvés en chemin : `nvrhi` déclaré avant `nvrhi_vk` (masqué dans le dépôt, visible chez
    *Rando*) ; les binding sets WebGPU ne gardaient pas leurs ressources (le buffer d'un matériau disparaissait
    à sa création) ; un `float3` après un scalaire dans un constant buffer, aligné autrement en WGSL ;
  - une PR au-delà des 400 lignes, bloc GPU signalé : #205 (510) ;
  - **ponytail** : les textures de données sont cuites comme les couleurs (mips moyennés en sRGB, normal maps
    non renormalisées) ; la normale tourne sans la transposée de l'inverse (juste sans échelle non uniforme) ;
  - le navigateur intégré de l'application Claude n'a pas d'adaptateur WebGPU : les tests web passent par
    Firefox et par le téléphone de Donnovan.
- Prochaine étape : M5.3, les ombres en cascades.

## 2026-09-27 — M4.6 — Clôture : le renard court dans le navigateur, sur le backend WebGPU de NVRHI

- **Temps Donnovan : 4,5 h** (estimé 3,5 h), déclaré « en tout » : 2,5 h pour le prototype, l'ADR-0023 et ses
  sondages, les relectures de #187 et #188 et l'activation de GitHub Pages, puis 2 h pour relire les PR #189 à
  #199. Réparti au prorata des estimations : #182 0,64 h, #183 0,32 h, #184 1,29 h, #185 0,96 h, #186 1,29 h.
  **Ratio 1,29.** Sur la phase 4 entière, M4.6 compris : 10,5 h pour 11,75 estimées, **0,89**, dans la
  fourchette : pas de recalibrage.
- Sessions Claude Code : 1
- Fait :
  - **un backend WebGPU pour NVRHI**, dans `engine/gpu/src/webgpu/` : device, ressources, shaders WGSL, bindings,
    pipelines, command lists (#189 à #194). Il tourne en natif sur Dawn et dans le navigateur par
    emdawnwebgpu ;
  - **le cube du test de fumée dans Firefox** (#195), comparé par `tools/web-smoke.sh` ;
  - **`levain_sandbox --gpu webgpu`** (#196), qui dessine hors écran : la CI y fait courir le renard et afficher
    Sponza, en Debug ;
  - **le sandbox dans le navigateur** : `requestGpuDevice`, une boucle en fonction par image, un système de
    fichiers préchargé (#197, #198) ;
  - **publié sur GitHub Pages** (#199) : https://phantomdo.github.io/Levain/
- Mesures :
  - cube WebGPU = référence Vulkan : 0 pixel différent sur 4 096, en natif (`ctest -R smoke.webgpu.cube`) et dans
    Firefox 156 (`tools/web-smoke.sh`) ;
  - renard skinné par WebGPU : 2 pixels différents de Vulkan sur 2 073 600 ; Sponza (105 dessins) : 105 pixels,
    sans erreur de validation (`levain_sandbox --gpu webgpu --time 1.5 --capture …`, puis sur Vulkan) ;
  - le renard court sur WebGPU : os le plus rapide à 502 unités/s, contre 504 sur Vulkan (`--seconds 8
    --locomotion Survey,Walk,Run`) ;
  - le sandbox publié tourne à 61 images/s dans Firefox (`node tools/web-smoke.mjs
    https://phantomdo.github.io/Levain/ - …`) et dans un moteur Chromium : le navigateur DuckDuckGo sur le
    téléphone Android de Donnovan (WebView système), capture de Donnovan ;
  - tests : 126 en natif, 83 en WebAssembly (`ctest`).
- Décisions : l'[ADR-0023](adr/0023-cible-web-webgpu.md) (NVRHI garde toutes les plateformes, un backend WebGPU
  écrit chez nous, des fonctionnalités par plateforme). En natif, WebGPU dessine hors écran : Dawn n'a pas de
  surface pour le pilote SDL « offscreen » de la CI. Sponza n'est jamais publié (licence).
- Écarts et problèmes :
  - trois PR au-delà des 400 lignes, bloc GPU signalé : #190 (973 lignes), #192 (695), #193 (848). Environ 540
    lignes en tout sont des bouchons d'interface NVRHI ;
  - timer queries WebGPU non faites : Firefox n'offre pas `timestamp-query`, le sandbox n'affiche pas de temps GPU
    sur ce backend ;
  - la CI passait clang-tidy sur tous les `.cpp`, y compris ceux du seul navigateur : la liste vient maintenant de
    `compile_commands.json` ;
  - aucun navigateur Chromium ne charge de page sur la machine de Donnovan (Helium compris, Firefox oui) : le
    critère « Chromium » est vérifié sur son téléphone, et il testera aussi sur sa tablette.
- Prochaine étape : aligner *Rando* sur le moteur (`ports/nvrhi`, `triplets/`, Dawn et les filtres de plateforme
  dans son manifeste), puis la phase 5.

## 2026-09-25 — hors milestone — Un échec de chargement sans fuite GPU

- Temps Donnovan : 0,33 h (20 min, relecture comprise ; estimé 0,1 h)
- Sessions Claude Code : 1
- Fait : `createDemoScene` ferme et soumet sa command list d'envoi quand `uploadModel` échoue
  (`submitAbandonedUpload`). Avant, une texture illisible laissait fuir 42 objets Vulkan et finissait sur
  l'assertion de validation (code 133) ; maintenant, le sandbox sort en code 1 avec le message d'erreur.
  Piège consigné dans `.agents/skills/build/GOTCHA.md`.
- Mesures : texture remplacée par du texte (essai en local, retiré) → code 133 et « 42 leaked objects » avant,
  code 1 sans message de validation après (`SDL_VIDEO_DRIVER=offscreen levain_sandbox --seconds 1 --model
  <gltf>`, Debug) ; 114 tests (`ctest`, linux-debug).
- Écarts et problèmes : pas de test automatique du chemin d'erreur, il demanderait un asset corrompu versionné
  dans `data/`, que chaque lancement du sandbox scannerait.
- Prochaine étape : phase 5, l'ADR de Jolt (#173).

## 2026-09-25 — M4.5 et phase 4 — Clôture : le renard passe du repos à la course sans saut

- **Temps Donnovan pour M4.5 : 1,0 h**, soit le total de la journée (« 2h ») moins la 1,0 h de M4.4.
  Relectures déclarées : #166 25 min, #171 20 min ; les autres PR (#165, #167 à #170, #172) ont été lues sans
  temps annoncé. Ce total comprend deux questions (ozz est-il maintenu ? pourquoi fastgltf plutôt que tinygltf ?)
  et quatre sondages (ADR-0022, port vcpkg, modèle de test, clôture). Réparti au prorata des estimations :
  #116 à 0,13 h, #117 à 0,37 h, #118 à 0,5 h. **Ratio 0,50.**
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux (`--model Fox.gltf --locomotion
  Survey,Walk,Run`) ; critères mesurés et consignés ; CI verte, sans erreur de validation (la CI fait courir le
  renard en Debug et vérifie le critère) ; README d'`animation` et de `render` à jour ; board renseigné ; tag
  `m4.5` et release.
- **Décisions** : l'[ADR-0022](adr/0022-animation-squelettique.md), trois choix sur sondage : ozz-animation
  derrière une passerelle glTF écrite chez nous (la proposition de Donnovan), le skinning en compute, la pose dans
  un composant. Puis **son amendement** : un port vcpkg maison plutôt que `FetchContent` (43,5 Mo d'archive). Fox
  (Khronos) sert de modèle de test ; Quaternius viendra dans *Rando*.

### Critères du milestone

| Critère (ROADMAP et issues) | Mesuré | Commande |
|---|---|---|
| Le personnage passe du repos à la course selon sa vitesse, sans saut visible | L'os le plus rapide d'une image à l'autre va à **504 unités/s**, comme dans la course seule (504) ; une bascule sans fondu le porte à **178 110** (essai en local, retiré). 389 en CI (lavapipe) | `levain_sandbox --seconds 9 --model assets-cache/Models/Fox/glTF/Fox.gltf --model-scale 0.05 --locomotion Survey,Walk,Run` (Release, 3 lancements), ligne « os le plus rapide » ; `--clip Run` pour la référence |
| #117 : un clip joue en boucle, sans artefact de skinning | 8 captures du clip Run : cycle propre | idem, `--clip Run --capture` |
| #117 : coût CPU et GPU mesuré | **3 µs CPU, 5 µs GPU** par image, 24 os | idem, ligne « skinning : » |
| #116 : l'ADR est validé | ADR-0022 et son amendement | — |
| Tests | 118, trois presets, ASan et UBSan compris | `ctest -j8` |

### Temps

| Issue | Estimé | Réconcilié |
|---|---:|---:|
| #116 ADR : animation squelettique (#165) | 0,25 h | 0,13 h |
| #117 Skinning et lecture de clips (#166 à #170) | 0,75 h | 0,37 h |
| #118 Fondus et machine à états (#171, #172) | 1,0 h | 0,5 h |
| **M4.5** (ROADMAP) | **2,0 h** | **1,0 h** (ratio 0,50) |

### Phase 4

| Milestone | Estimé | Passé |
|---|---:|---:|
| M4.1 Import glTF | 1,5 h | 1,25 h |
| M4.2 Base d'assets | 1,75 h | 0,75 h |
| M4.3 Cuisson des assets | 2,0 h | 2,0 h |
| M4.4 Hot-reload des assets | 1,0 h | 1,0 h |
| M4.5 Animation squelettique | 2,0 h | 1,0 h |
| **Phase 4** | **8,25 h** | **6,0 h** — ratio **0,73** |

- **Hors de la fourchette 0,8–1,25, mais aucun recalibrage** : Donnovan l'a décidé sur sondage. Le ratio cumulé
  des phases 0 à 4 vaut 0,87 (23,5 h pour 27,0) ; après le recalibrage ×0,67 de la phase 1, les phases 2 et 3
  étaient remontées au-dessus de 1. Le point se refait à la clôture de la phase 5.
- **Les études passent à 0,4 h** (sondage) : E2 à E4 ont coûté 0,33 à 0,5 h pour 0,15 estimées. E6, oubliée des
  estimations, entre dans M6.3. La ROADMAP passe en v0.7, 56,9 h au total ; échéances inchangées.
- **La phase 6 est détaillée** : #173 à #178 dans Levain ; M6.4 et M6.5, plugins gameplay, dans *Rando*
  ([Rando#2](https://github.com/PhantomDO/Rando/issues/2), [Rando#3](https://github.com/PhantomDO/Rando/issues/3)),
  avec leurs milestones et leurs labels (sondage).
- L'étude de la phase, [E4](etudes/E4-pipelines-assets.md), a été écrite en M4.4.

### Ce que M4.5 a appris

- **Une mesure qui peut échouer vaut mieux qu'un GIF.** « Sans saut visible » se jugeait à l'œil ; la vitesse
  maximale d'un os le chiffre, et l'essai d'une bascule sans fondu (178 110 contre 504) prouve que la mesure voit
  un saut. La CI la vérifie maintenant.
- **Vérifier avant tout travail GPU.** Un nom de clip inconnu, détecté après l'ouverture de la command list
  d'envoi, a laissé des objets GPU derrière lui, et la validation Vulkan a fait échouer l'assertion en Debug. Le
  même piège existe quand un modèle échoue à s'envoyer : une tâche séparée est proposée.
- **ozz quantifie ses rotations** sur 15 bits par composante : un test qui attend l'identité à 1e-5 échoue, à
  1e-3 il passe. La tolérance des tests d'animation le dit.
- **La question de Donnovan a changé l'intégration.** « Importer ozz et faire une passerelle depuis glTF » a
  évité `gltf2ozz`, un exécutable qui embarque sa propre copie de tinygltf.

**Prochaine étape** : phase 5, M5.1 — le PBR direct (ADR forward ou forward+).

---

## 2026-09-25 — M4.4 — Clôture : une texture retouchée se voit en 0,1 s

- **Temps Donnovan pour M4.4 : 1,0 h** (« environ 1h max en tout »). Relectures déclarées : ADR-0021 5 min, #161
  10 min, #162 5 min, E4 20 min, soit 40 min ; le reste va aux deux sondages et au pilotage. Réparti au
  prorata des relectures (20 min chacune) : #93 à 0,5 h, #94 à 0,5 h.
  **Ratio 1,00.**
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux (`--model`, texture modifiée pendant que le
  sandbox tourne) ; critères mesurés et consignés ; CI verte, sans erreur de validation (Debug et ASan vérifiés
  en local, Release en CI) ; README d'`assets` à jour (invariant n°9) ; étude E4 écrite ; board renseigné ; tag
  `m4.4` et release.
- **Décisions** : l'[ADR-0021](adr/0021-hot-reload-des-textures.md), deux choix sur sondage : une texture
  modifiée est **relue depuis sa source**, sans recuisson ; **seules les textures** se rechargent. Mesure à
  l'appui : recuire une texture 1024² fige l'image 0,73 s.

### Critères du milestone

| Critère (ROADMAP et issues) | Mesuré | Commande |
|---|---|---|
| Une texture modifiée dans un logiciel externe est visible en moins de 2 s | **88, 97 et 140 ms** après l'écriture, texture 2048² du camion (Release) ; 498 ms en Debug ; 1 013 ms sous ASan ; **86 ms en CI** (damier) | `SDL_VIDEO_DRIVER=offscreen SANDBOX=./build/linux-release/sandbox/levain_sandbox ./tools/texture-hot-reload.sh` |
| Une source invalide ne fait pas planter : message dans le log, l'ancien asset reste | « unknown image type ; l'ancienne texture reste », rendu continu jusqu'au bout | idem |
| Étude E4 | [E4](etudes/E4-pipelines-assets.md), 124 lignes | — |
| Tests | 105, trois presets, ASan et UBSan compris ; le hot-reload tourne en CI (Release) | `ctest -j8` |

### Temps

| Issue | Estimé | Réconcilié |
|---|---:|---:|
| #93 Hot-reload des assets (ADR-0021, #160, #161, #162) | 0,85 h | 0,5 h |
| #94 Étude E4 (#163) | 0,15 h | 0,5 h |
| **M4.4** (ROADMAP) | **1,0 h** | **1,0 h** (ratio 1,00) |

Phase 4 à ce stade : 5,0 h passées pour 6,25 h estimées (M4.1 à M4.4), ratio 0,80. **Chiffre
provisoire** : il reste M4.5 (animation squelettique), 2,0 h estimées.

### Ce que M4.4 a appris

- **Le bon mécanisme de rechargement était déjà là.** Le fichier cuit est jugé sur le hash de sa source : il
  suffisait de rehacher le fichier modifié pour que le chargement habituel reparte de la source. Aucun code
  n'est propre au hot-reload, en dehors de la détection et du remplacement GPU.
- **Mesurer l'option écartée tranche vite** : 0,73 s de gel pour recuire une texture a rendu le choix du sondage
  évident, et l'ADR a pu le dire en chiffres.
- **Un test qui dépend d'un outil installé ne passe pas en CI.** Le négatif produit par ImageMagick a été
  remplacé par une copie du damier du dépôt (un PNG sous un nom en `.jpg`, que stb reconnaît au contenu) : plus
  aucune dépendance, et le critère se revérifie à chaque PR.
- **Deux écritures rapprochées peuvent porter la même date** : le noyau ne l'avance qu'à chaque tick de son
  horloge. Les tests avancent la date à la main (`touchLater`), et le sandbox rehache avant de décider : une date
  changée sans contenu changé ne recharge rien.
- **Une étude coûte 20 à 30 minutes, pas 10** : E2 a pris 0,33 h, E3 0,50 h, E4 0,50 h, pour 0,15 h estimées
  chacune. À corriger pour E5 à E8 lors du recalibrage de fin de phase.

**Prochaine étape** : M4.5 — l'animation squelettique (ADR #116, puis #117 et #118).

---

## 2026-09-24 — Hors milestone — `--seconds` tenu fenêtre masquée

- Temps Donnovan : 0,17 h de relecture déclarées, 10 min (estimé 0,1 h)
- Sessions Claude Code : 1
- Fait : `platform::waitEvents` prend une durée maximale (`SDL_WaitEventTimeout`) ; la boucle du sandbox, fenêtre
  masquée, n'attend plus au-delà de `--seconds`. Test `waitEvents rend la main à l'échéance…` (offscreen).
  CI : `ctest --timeout 120`, un test bloqué échoue en 2 min (le plus lent : 5,75 s).
- Mesures : minimisé par KWin, `--seconds 6` → arrêt à **6,0 s** (avant : toujours vivant à 11,5 s, arrêté par
  SIGTERM ; lu dans le log « boucle arrêtée après … », minimisation par KWin, script non versionné, dans la PR) ;
  CPU minimisée **0 ms** sur 2 s (`tools/kwin-window-smoke.sh`, inchangé).
- Écarts et problèmes : le SIGTERM ignoré sous RenderDoc, bureau verrouillé, n'est pas reproduit : minimisée,
  SIGTERM réveille l'attente (code 0), et sous RenderDoc SDL l'intercepte bien (`SigCgt`). Hypothèse non
  vérifiée : blocage dans la présentation X11. Détail au GOTCHA du skill `build`, section RenderDoc.
- Prochaine étape : reprendre M4.3.

## 2026-09-24 — M4.3 — Clôture : Sponza cuite charge 23 fois plus vite

- **Temps Donnovan pour M4.3 : 2,00 h**, soit le total de la journée (« environ 4 h en tout ») moins les 1,25 h
  de M4.1 et les 0,75 h de M4.2. Relectures déclarées : 1,83 h (ADR 10 min, puis 15, 25, 10, 25 et 25 min, et
  les 10 min de la clôture de M4.2). Ce total comprend aussi le pilotage de deux sessions parallèles, que
  Donnovan a lancées depuis des propositions de tâches : #155 (`--seconds` respecté quand la fenêtre est
  masquée) et #156 (le `.pyc` suivi par erreur). Elles n'ont pas d'issue, et leur temps n'est pas isolé.
  Réparti au prorata : #91 à 1,0 h, #92 à 1,0 h. **Ratio 1,00.**
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux (`--model`, version cuite) ; critères mesurés
  et consignés ; CI verte, sans erreur de validation (Sponza cuite vérifiée en local en Debug et sous ASan) ;
  README d'`assets`, skill `build` et son GOTCHA à jour ; board renseigné ; tag `m4.3` et release.
- **Décisions** : l'[ADR-0020](adr/0020-cuisson-des-assets.md), quatre choix sur sondage (UASTC transcodé au
  chargement, meshes en binaire brut avec un champ `encoding` réservé à la demande de Donnovan, un cuiseur
  explicite avec repli sur la source, un dossier `.cooked/`). Puis **son amendement**, sur mesures : le
  transcodage coûtait 9,4 ms par texture, et le cuiseur écrit aussi un cache BC7 pour la plateforme.

### Critères du milestone

| Critère (ROADMAP et issues) | Mesuré | Commande |
|---|---|---|
| Sponza cuite charge au moins 5 fois plus vite que le glTF brut | **19 ms au lieu de 442** (modèle 3,5 ms au lieu de 7,2 ; textures 15 ms au lieu de 435) : **×23**, et ×7,9 en comptant le scan des racines (42 ms) | `./build/linux-release/tools/cook/levain_cook assets-cache`, puis `levain_sandbox --model assets-cache/Models/Sponza/glTF/Sponza.gltf` (Release, 3 lancements), avec puis sans `assets-cache/.cooked` |
| L'outil tourne sans GPU | `levain_cook` ne lie qu'`assets` et `core` | `tools/cook/CMakeLists.txt` |
| Mémoire vidéo des textures avant et après | **128 Mo en RGBA8, 32 Mo en BC7** | idem, ligne « Mo en mémoire vidéo » |
| Niveaux de mip vérifiés dans une capture RenderDoc | `9288698199695299068.jpg` : BC7_SRGB, 1024 × 1024, 11 niveaux jusqu'à 1 × 1 | `LEVAIN_MIPS_TEXTURE=9288698199695299068.jpg LEVAIN_MIPS_PREFIX=m4.3 LEVAIN_MIPS_ARGS="--seconds 8 --model assets-cache/Models/Sponza/glTF/Sponza.gltf" QT_QPA_PLATFORM=offscreen qrenderdoc --python tools/renderdoc-mips.py` |
| En plus : l'image ne souffre pas | 0,64 niveau sur 255 d'écart moyen avec le rendu depuis les sources ; PSNR de 48 dB | captures `--capture`, comparées |
| En plus : durée de cuisson | 42 s pour 72 assets sur 12 cœurs ; 3 min 30 en CI (4 cœurs, Release) | `levain_cook assets-cache` |
| Tests | 103, trois presets, ASan et UBSan compris | `ctest -j8` |

### Temps

| Issue | Estimé | Réconcilié |
|---|---:|---:|
| #91 Outil de cuisson et format des meshes (ADR-0020, #152, #153, #154) | 1,0 h | 1,0 h |
| #92 Textures KTX2 et BC7 (#157, #158) | 1,0 h | 1,0 h |
| **M4.3** (ROADMAP) | **2,0 h** | **2,00 h** (ratio 1,00) |

Phase 4 à ce stade : 4,0 h passées pour 5,25 h estimées (M4.1 à M4.3), ratio 0,76. **Chiffre provisoire** : il reste
M4.4 (hot-reload des assets) et M4.5 (animation squelettique), la plus chère.

### Ce que M4.3 a appris

- **Écrire le risque et sa parade avant de coder fait gagner le moment où il se réalise.** Le transcodage était
  le risque nommé dans l'ADR ; mesuré à 9,4 ms par texture, il a été traité en une heure, par un amendement et
  non par une nouvelle décision.
- **Une mesure qui mélange deux choses cache la réponse** : « lu en 60 ms » comptait le scan des racines et la
  lecture du modèle. Séparés, ils ont montré que le modèle cuit se lisait en 3,6 ms, et que le scan pesait
  42 ms.
- **UBSan voit ce qu'aucun test n'avait couvert** : un `memcpy` depuis un pointeur nul, sur un tableau vide. Le
  test d'aller-retour contient maintenant un nom vide.
- **Un outil qui dépend de l'écran ne marche pas à distance.** RenderDoc attendait une fenêtre X11 jamais
  présentée, parce que la session était verrouillée. La capture passe maintenant hors écran, et une tâche
  parallèle (#155) a corrigé le sandbox qui ne s'arrêtait plus quand sa fenêtre restait masquée.

**Prochaine étape** : M4.4 — le hot-reload des assets (#93, #94 et l'étude E4).

---

## 2026-09-24 — M4.2 — Clôture : les assets ont une identité

- **Temps Donnovan pour M4.2 : 0,75 h**, soit le total de la journée (« 2 h ») moins les 1,25 h de M4.1.
  Relectures déclarées : 0,67 h (10 + 15 + 15 min, et la dernière sans chiffre). Réparti au prorata des
  estimations : #89 à 0,21 h, #90 à 0,54 h. **Ratio 0,43** (1,75 h estimées).
- Définition de « terminé » (SPECS §9) : le sandbox lancé sous Linux scanne ses racines d'assets ; critères
  mesurés et consignés ; CI verte, sans erreur de validation ; README d'`assets` et de `data` à jour, pièges de
  flecs au README de `scene` ; board renseigné ; tag `m4.2` et release.
- **Décisions** : l'[ADR-0019](adr/0019-identifiants-d-assets.md), six choix sur sondage (un GUID dans un
  `.meta`, le rattachement par le hash, des sous-assets par indice, le format « clé = valeur », le comptage par
  le monde, les `.meta` créés par le moteur). Puis un septième, à l'implémentation : **des hooks flecs plutôt
  qu'un recomptage par image**, sur mesures (27 µs pour 10 000 entités, 273 µs pour 100 000), en pensant à la
  Switch 2 et au mobile.

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| Renommer ou déplacer un fichier ne casse aucune référence | un fichier renommé et déplacé hors du moteur, sans son `.meta`, garde son GUID, retrouvé par son hash ; renommé **et** modifié, il est signalé | `levain_tests` (tests du registre) |
| Aucun chemin absolu dans les scènes | une entité ne garde qu'un `MeshRef` (GUID et indice) ; les chemins ne vivent que dans le registre | lecture d'`asset_ref.hpp` |
| En plus : chaque chemin de comptage | pose, remplacement, même valeur, copie, retrait, destruction ; déchargement en fin d'image, pas si l'asset est repris | `levain_tests` |
| En plus : un `.meta` oublié est refusé | `assets.metas-committed` | `ctest -R metas` |
| Tests | 95, trois presets, ASan et UBSan compris, en parallèle | `ctest -j8` |

### Temps

| Issue | Estimé | Réconcilié |
|---|---:|---:|
| #89 ADR-0019 (#146) | 0,5 h | 0,21 h |
| #90 Registre, références, contrôle (#147, #148, #149) | 1,25 h | 0,54 h |
| **M4.2** (ROADMAP) | **1,75 h** | **0,75 h** (ratio 0,43) |

Phase 4 à ce stade : 2,0 h passées pour 3,25 h estimées (M4.1 et M4.2), ratio 0,62. **Chiffre provisoire**, sur
deux milestones d'une seule journée : la phase en compte encore trois, dont la cuisson, estimée la plus chère.
Pas de conclusion avant sa clôture (leçon de M3.4).

### Ce que M4.2 a appris

- **Mesurer tranche mieux qu'argumenter.** À la question « quelle solution pour la Switch 2 ? », trois mesures
  de deux minutes ont répondu, là où les arguments se valaient.
- **ASan trouve ce que le Debug laisse passer** : `b.set(a.get<T>())` lisait de la mémoire libérée quand `b`
  rejoignait la table de `a`. Le test était vert en Debug.
- **Une contrainte de bibliothèque se découvre au test, pas à la lecture** : les hooks `on_replace` interdisent
  `clone()`, ce que la documentation de flecs ne dit qu'à propos de `get_mut`. Les deux pièges sont au README de
  `scene`.
- **Une identité d'asset ne répare pas les références internes des fichiers sources** : un `.gltf` cherche
  sa texture par son nom. C'est la limite de Unity avec un FBX ; la cuisson (M4.3), qui convertira les glTF en
  format moteur, pourra y substituer des GUID.

**Prochaine étape** : M4.3 — la cuisson des assets (#91, #92).

---

## 2026-09-24 — M4.1 — Clôture : Sponza s'affiche

- **Temps Donnovan pour M4.1 : 1,25 h**, le total de la journée (« 1 h 15 en tout »). Relectures déclarées :
  0,83 h. Réparti au prorata des estimations : #140 à 0,21 h, #87 à 0,62 h, #88 à 0,42 h. **Ratio 0,83.**
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux (`--model`) ; critères mesurés et consignés ;
  CI verte, sans erreur de validation (Sponza en Debug dans la CI) ; README d'`assets` à jour ; board
  renseigné ; tag `m4.1` et release.
- **Ce que M4.1 a livré** :
  - **les captures** (#140) : `--capture` écrit la dernière image rendue, pour que Donnovan voie les rendus
    sur sa tablette ; le rendu WebGPU est noté en candidat v2, à sa demande ;
  - **l'import glTF** (#87) : fastgltf, invisible hors d'`engine/assets`, lit meshes, nœuds et matériaux ; un
    nœud devient une entité, avec sa hiérarchie ;
  - **les assets de test** : téléchargés par `tools/fetch-assets.sh` à un commit figé, vérifiés par SHA-256,
    jamais versionnés. Sponza, sous licence non permissive, ne sert qu'aux tests (décision de Donnovan).
- **Décision** : le lien entre une entité et son mesh reste un **indice provisoire** (`MeshInstance`), jusqu'à
  l'ADR de M4.2 (choix de Donnovan, sur sondage).

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| La scène Sponza s'affiche | oui, avec ses 25 textures de couleur de base ; capture envoyée à Donnovan | `./tools/fetch-assets.sh`, puis `levain_sandbox --model assets-cache/Models/Sponza/glTF/Sponza.gltf --capture sponza.png` |
| Temps de chargement mesuré | Release : **215 ms** de lecture et décodage, **252 à 268 ms** de mips et d'envoi au GPU (3 lancements) ; Debug : 936 ms et 2 308 ms | idem, preset `linux-release` puis `linux-debug` |
| En plus : un glTF simple avec sa hiérarchie (#87) | le Cesium Milk Truck : 6 nœuds, les roues (nœuds enfants) dans leurs passages de roue | `levain_sandbox --model assets-cache/Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf` |
| En plus : fastgltf invisible hors d'`assets` | `deps.fastgltf-visibility`, qui échoue quand on le viole | `ctest -R fastgltf` |
| Tests | 85, trois presets, ASan et UBSan compris | `ctest` |

### Temps

| Issue | Estimé | Déclaré | Réconcilié |
|---|---:|---:|---:|
| #140 Capture PNG (#141) | 0,25 h | — | 0,21 h |
| #87 Import glTF (#142, #143) | 0,75 h | 0,5 h | 0,62 h |
| #88 Matériaux et Sponza (#144) | 0,5 h | 0,33 h | 0,42 h |
| **M4.1** (ROADMAP, capture comprise) | **1,5 h** | | **1,25 h** (ratio 0,83) |

### Ce que M4.1 a appris

- **Une grosse scène trouve ce qu'une petite ne voit pas, et seulement en Debug.** Les 105 dessins de Sponza
  dépassaient les 16 versions du buffer des constantes ; la validation de NVRHI, éteinte en Release, était la
  seule à le voir. La limite est nommée (`MaxMeshDrawsPerCommandList`), la CI lance Sponza en Debug, et le
  piège est au GOTCHA du build.
- **Montrer vaut mieux que décrire** : la première capture du camion n'avait pas de roues visibles. Il a
  suffi de le tourner de trois quarts pour voir qu'elles étaient là, cachées par la carrosserie : un doute
  levé en une image plutôt qu'en un test de plus.
- **Le Debug coûte cher au chargement** : 4 à 9 fois plus lent qu'en Release (stb_image et les mips sans
  optimisation). C'est le point de départ chiffré de la cuisson (M4.3), qui doit charger Sponza au moins 5 fois
  plus vite.

**Prochaine étape** : M4.2 — la base d'assets, en commençant par l'ADR des identifiants et du format `.meta`
(#89).

---

## 2026-09-23 — Phase 3 — Clôture (M3.6 compris) : le jeu a son dépôt

- **Temps Donnovan pour M3.6 : 0,50 h**, soit le total de la journée (« 2 h en tout ») moins les 1,50 h de
  M3.5. Réparti au prorata des estimations : #114 à 0,25 h, #115 à 0,25 h. **Ratio 1,00.**
- Définition de « terminé » pour M3.6 (SPECS §9) : le jeu est lançable sous Linux ; les critères sont mesurés et
  consignés ; la CI est verte dans les deux dépôts, sans erreur de validation ; les README sont à jour (Levain
  et *Rando*) ; le board est renseigné ; le tag `m3.6` et la release suivent. Étude de la phase : **E3, écrite
  et relue** (#119).
- **Ce que M3.6 a livré** :
  - l'[ADR-0018](adr/0018-moteur-plugins-et-jeu.md) : deux dépôts, trois niveaux (moteur, plugins moteur,
    plugins gameplay) et le classement de chaque fonctionnalité, validé sans correction ;
  - le moteur utilisable par un autre dépôt (#122) ;
  - le dépôt public [PhantomDO/Rando](https://github.com/PhantomDO/Rando), avec sa CI et la protection de
    `main` ([Rando#1](https://github.com/PhantomDO/Rando/pull/1)). La page de game design y a déménagé.

### Critères de M3.6

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| La CI du jeu compile contre un commit figé du moteur | `63f75bf` téléchargé depuis GitHub : trois presets verts, en local et sur la CI de *Rando* | `cmake --preset <preset> && cmake --build --preset <preset>` (dans *Rando*) |
| Une modification locale du moteur se voit dans le jeu sans rien publier | une ligne de log modifiée dans le clone de Levain s'affiche au lancement de *Rando* (modification annulée ensuite) | `cmake --preset linux-debug -B build/local-engine -DFETCHCONTENT_SOURCE_DIR_LEVAIN=../Levain` |
| En plus : le jeu tourne | hors écran, 2 s : 20 171 frames (Debug), 76 167 (Release), 15 396 (ASan) | `SDL_VIDEO_DRIVER=offscreen ./build/<preset>/game/rando --seconds 2` |
| En plus : un manifeste vcpkg incomplet est refusé | « dépendance absente ou différente : stb », à la configuration du jeu | `cmake --preset linux-debug` (dans *Rando*) |
| En plus : les contrôles attrapent vraiment les erreurs | chaque contrôle désactivé à la main fait échouer son test | `ctest -R "cmake\."` |

### Temps de M3.6

| Issue | Estimé | Passé |
|---|---:|---:|
| #114 ADR-0018 (#121) | 0,25 h | 0,25 h |
| #115 Le moteur vu d'un jeu, le dépôt *Rando* (#122, Rando#1) | 0,25 h | 0,25 h |
| **M3.6** (ROADMAP) | **0,5 h** | **0,50 h** (ratio 1,00) |

### Phase 3 — le ratio

| Milestone | Estimé | Passé |
|---|---:|---:|
| M3.1 Intégration de flecs et explorer | 1,0 h | 0,50 h |
| M3.2 Transforms et hiérarchie | 0,75 h | 0,42 h |
| M3.3 Boucle à pas fixe | 1,0 h | 1,33 h |
| M3.4 Input par actions et caméra libre | 1,0 h | 1,00 h |
| M3.5 Choix du jeu | 0,25 h | 1,50 h |
| M3.6 Dépôt du jeu | 0,5 h | 0,50 h |
| **Phase 3** | **4,5 h** | **5,25 h** — ratio **1,17** |

**Dans la fourchette 0,8–1,25 : aucun recalibrage.** Sans M3.5, le ratio serait de 0,88. Le dépassement vient
du choix du jeu, qui a cadré toute la v1, et non du travail de moteur. Le ratio cumulé des phases 0 à 3,
(5,0 + 3,0 + 4,25 + 5,25) / (6,0 + 4,5 + 3,75 + 4,5) = **0,93**, est aussi dans la fourchette. Les ajouts du
jeu (+9,5 h) sont déjà dans la ROADMAP v0.6 : la fin visée reste le 16/05/2027.

### Phase 5 détaillée

Seize issues créées, #123 à #138, sur M5.1 à M5.7, avec estimation (10,25 h au total, celui de la ROADMAP) et
phase sur le board. Deux ADR y figurent : forward ou forward+ (#123), et **des passes de rendu venues d'un
plugin** (#134). Ce second ADR est le coût que l'ADR-0018 annonçait pour ranger le terrain en plugin.

### Ce que la phase 3 a appris

- **Mesurer avant de suivre la documentation** : `ChildOf`, la voie documentée, ratait le critère de M3.2 d'un
  facteur 7,5. La documentation de flecs le dit elle-même, mais seulement dans le manuel des hiérarchies.
- **Un singleton absent désactive une requête sans bruit** (M3.3) : le benchmark affichait 0,001 ms, un temps
  trop beau pour être vrai. Un chiffre trop bon se vérifie comme un chiffre trop mauvais.
- **Le temps déclaré PR par PR sous-estime** : ×4 en M3.3, ×2,4 en M3.4. Seul le total de la journée compte.
- **Un choix de produit coûte plus cher qu'un choix technique** : M3.5 a pris six fois son estimation, et
  valait la peine, parce que chaque ajout a été arbitré avant d'écrire du code.
- **Mon outil local peut masquer une panne de la CI** : CMake 4.4 active des politiques qu'un `cmake -P` de la
  CI n'a pas (GOTCHA du build).

**Prochaine étape** : la phase 4, en commençant par M4.1, l'import glTF (#87, #88).

---

## 2026-09-23 — M3.5 — Clôture : le jeu s'appelle *Rando*

- **Temps Donnovan pour M3.5 : 1,50 h**, le total de la journée annoncé à la fin (« 1 h 30 en tout »), dont
  1 h déjà déclarée pour #68. Estimé : 0,25 h, soit **un ratio de 6,0**. Le chiffre est juste ; c'est le
  périmètre qui a changé (voir plus bas).
- Définition de « terminé » (SPECS §9) : page de game design validée par Donnovan ; ROADMAP v0.6 et SPECS à
  jour ; étude E3 écrite et relue ; board renseigné ; tag `m3.5`. Pas de code, donc pas de démo.
- **Le jeu** ([JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md)) : *Rando*, un vertical slice de 5 à 10 minutes dans l'esprit de *Breath of the
  Wild*. Une vallée de 500 m, un sanctuaire visible dès le départ ; planer, nager, marcher en gérant une
  endurance ; des cœurs perdus par la chute, la noyade et les pièges ; des énigmes d'objets physiques ; pas
  d'ennemis.
- **Décisions** : prises en six tours de sondages, consignées une par une en bas de `JEU.md`. Les plus
  lourdes :
  - l'animation squelettique entre en v1 ;
  - un vrai système de terrain ;
  - de l'herbe dense ;
  - **le jeu vit dans son propre dépôt**, avec des plugins moteur (level design) et des plugins gameplay
    (dans le dépôt du jeu).

### Critères du milestone

| Critère (ROADMAP) | Résultat | Où |
|---|---|---|
| La page est validée par Donnovan | validée le 23/09, titre *Rando* choisi par lui | [JEU.md](https://github.com/PhantomDO/Rando/blob/main/docs/JEU.md), #113 |
| La ROADMAP est mise à jour en conséquence | v0.6 : 7 milestones créés, 2 étendus, échéances recalées, milestones et issues GitHub créés (#114 à #118) | [ROADMAP.md](ROADMAP.md) |
| Étude E3 | écrite et relue | [E3](etudes/E3-modeles-objets.md), #119 |

### Temps

| Issue | Estimé | Passé |
|---|---:|---:|
| #68 Page de game design et roadmap (#113) | 0,35 h | 1,00 h |
| #69 Étude E3 (#119) | 0,15 h | 0,50 h |
| **M3.5** (ROADMAP) | **0,25 h** | **1,50 h** (ratio 6,0) |

Les estimations des deux issues (0,50 h) ne concordent pas avec celle de la ROADMAP (0,25 h) ; le ratio se
calcule sur la ROADMAP, comme pour les autres milestones.

Phase 3 à ce stade : **4,75 h passées pour 4,0 h estimées** (M3.1 à M3.5), **ratio 1,19**, encore dans la
fourchette 0,8–1,25. **Chiffre provisoire** : la phase compte désormais M3.6 (0,5 h), qui la clôturera.

### Ce que M3.5 a appris

- **« Choisir le jeu » était en fait « cadrer toute la v1 ».** L'estimation (0,25 h) supposait un choix de
  genre. En six tours de questions, le choix a fait entrer dans la v1 l'animation, le terrain, l'eau, l'herbe,
  une caméra à la troisième personne et un deuxième dépôt. Soit +9,5 h, et une fin reportée du 21/03 au
  16/05/2027. Ce temps est bien placé : chaque ajout a été arbitré au moment où il coûtait le moins cher, avant
  d'écrire du code.
- **Les sondages font gagner du temps à Donnovan** (sa demande, au premier tour) : une décision par clic, et
  « Other » pour les réponses qui sortent des cases. Trois de ses réponses en sont sorties, et ce sont elles
  qui ont le plus changé la roadmap (deux dépôts, les plugins, le paquet installé à terme).
- **Annoncer un coût cumulé à chaque tour** a évité une surprise à la fin, mais pas une erreur : 8,75 h
  annoncées pendant les questions, 9,5 h une fois tout compté (le dépôt du jeu et les pièges). L'écart est
  signalé dans #113.

**Prochaine étape** : M3.6 — l'ADR moteur, jeu et plugins (#114), puis la coquille du dépôt *Rando* (#115),
qui clôture la phase 3.

---

## 2026-09-22 — M3.4 — Clôture

- **Temps Donnovan pour M3.4 : 1,00 h** (ratio 1,00), réconcilié sur le total de la journée : 4 h 30
  annoncées le soir, dont 3 h 30 déjà imputées jusqu'à M3.3. Déclaré PR par PR : 0,42 h, soit **2,4 fois
  moins**. Cinquième fois que le déclaré sous-estime ; réparti au prorata : #66 à 0,79 h, #67 à 0,21 h.
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux et pilotable ; critères mesurés et
  consignés ; CI verte, zéro erreur de validation ; README de `input` (nouveau), de `platform` et de `scene` à
  jour ; board renseigné ; tag `m3.4` et release. Pas d'étude : E3 vient à la fin de la phase 3, après M3.5.

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| Une même action pilotée au clavier et à la manette | un test de bout en bout (événement brut → liaisons → action → caméra) : « W » puis le stick gauche donnent le même déplacement, 0,2 unité par pas | `./build/linux-debug/tests/levain_tests` |
| Changement de touches sans recompiler | les liaisons viennent de `data/input.cfg`, relu à chaque démarrage ; un nom inconnu de SDL échoue avec son numéro de ligne | idem, et `levain_sandbox` |
| En plus : le fichier livré est valide | vérifié par un test, pas seulement par l'œil | idem |
| En plus : le regard à la souris ne dépend pas de la cadence | 10 pixels donnent le même angle à 60 et à 120 images/s | idem |
| En plus : `scene` ne lie pas `input` | la caméra lit un singleton `FpsInput` ; le graphe de SPECS §7 tient | build |

### Temps

| Issue | Estimé | Passé |
|---|---:|---:|
| #66 Actions et axes (ADR-0017, #106, #107, #108, #109) | 0,5 h | 0,79 h |
| #67 Caméra libre (#110) | 0,5 h | 0,21 h |
| **M3.4** (ROADMAP) | **1,0 h** | **1,00 h** (ratio 1,00) |

Phase 3 à ce stade : **3,25 h passées pour 3,75 h estimées** (M3.1 à M3.4), **ratio 0,87** — dans la
fourchette 0,8–1,25. L'alerte lancée avec le chiffre provisoire (0,71) tombe d'elle-même : c'était le déclaré
PR par PR qui la provoquait, pas le travail. Le ratio définitif se calculera après M3.5 et l'étude E3.

### Ce que M3.4 a appris

- **Une bibliothèque peut mentir par son nom** : `SDL_GetGamepadButtonFromString("south")` rend −1 alors que
  l'énumération s'appelle `SDL_GAMEPAD_BUTTON_SOUTH` — la table de noms de SDL3 est restée celle d'une manette
  Xbox. Vérifié **avant** d'écrire l'ADR, ce qui a évité d'inscrire un exemple faux dans une décision.
- **Le critère d'un milestone peut devenir un test** : « la même action au clavier et à la manette » aurait pu
  rester une démonstration à la main ; en partant d'événements bruts, il tient en vingt lignes et ne se
  détériorera pas.
- **Une découpe en trois PR vaut mieux qu'une PR de 1205 lignes**, même quand le travail est d'un bloc dans la
  tête : platform, puis le fichier, puis l'état. Seule la deuxième a dépassé la règle des 400.
- **L'input est le premier module qui lit un fichier de configuration.** Le format choisi (texte ligne à ligne,
  noms de SDL) servira sans doute de modèle aux réglages de l'éditeur : c'est pour ça qu'il est documenté dans
  son README plutôt que seulement dans l'ADR.

**Prochaine étape** : M3.5 — le choix du jeu (#68), puis l'étude E3 et la clôture de la phase 3.

## 2026-09-22 — M3.4 — Caméra libre pilotée par les actions (#67)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : `scene/camera_control.hpp` (`FpsController`, `FpsInput`, `applyFpsInput`, et les pièges nommés de
  l'ADR-0011 : `normalizeOrZero`, `clampPitch`, `horizontalBasisFrom`) ; système `ApplyFpsInput` dans le
  pipeline de simulation ; la caméra du sandbox devient une entité, interpolée entre deux pas ; le sandbox lit
  `data/input.cfg`, met à jour l'input et pose `FpsInput` à chaque image ; capture de la souris pendant le
  regard ; huit tests.
- Mesures :
  - **la même caméra au clavier et à la manette** : un test de bout en bout (événement brut → liaisons →
    action → caméra) déplace la caméra de 0,2 unité par pas, d'abord avec « W », puis avec le stick gauche.
    Seule la source de l'événement change (`levain_tests`) ;
  - **le code de la caméra ne connaît ni SDL ni les touches** : `applyFpsInput` prend un `FpsInput` de valeurs
    déjà lues ; `scene` ne lie pas `input` (SPECS §7) ;
  - la diagonale n'avance pas plus vite que la ligne droite, le tangage est borné à ±85° et ne fait pas
    décoller la caméra : un test par piège ;
  - le sandbox tourne avec la caméra en entité, regard identique aux milestones précédents (lacet 10,3°,
    tangage −10,1°, ce qui vise exactement l'ancien point de mire) ;
  - trois presets verts, 68 tests en Debug.
- Écarts et problèmes :
  - la caméra porte son état précédent (`PreviousTransform`) pour être interpolée : sans lui, le regard
    avancerait par paliers de 16 ms alors que le rendu va plus vite ;
  - le rendu lit la **matrice monde** de la caméra, pas son `Transform` : c'est elle qui porte l'interpolation ;
  - `runMainLoop` ne lit pas le fichier de liaisons : le chargement remonte dans `main`, avec les autres mises
    en place, et une liaison absente arrête le sandbox avec un message clair.
- Prochaine étape : clôture de M3.4.

## 2026-09-22 — M3.4 — Input par actions : le brut, puis les intentions (#66)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : ADR-0017 ; `platform/input.hpp` (événements bruts du clavier, de la souris et des manettes,
  résolution des noms de SDL, capture de la souris, ouverture des manettes branchées à chaud) ;
  `pollEvents`/`waitEvents` rendent désormais fenêtre **et** périphériques ; nouveau module `engine/input`
  (`Bindings`, `parseBindings`, `InputState`, `updateInput`) ; `data/input.cfg` ; sept tests.
- Mesures :
  - **la même action au clavier et à la manette** : un test enfonce « Space » puis le bouton « a » de la
    manette, la même action répond, sans rien changer d'autre (`levain_tests`) ;
  - **changer les touches sans recompiler** : les liaisons viennent d'un fichier texte, dont un test vérifie
    la validité au build ; un nom inconnu de SDL échoue avec son numéro de ligne (contre-test : `key:Spacee`
    et `pad:south` refusés) ;
  - **le regard à la souris ne dépend pas de la cadence** : un axe est une vitesse, le déplacement de la
    souris est divisé par la durée de l'image ; 10 pixels donnent le même angle à 60 et à 120 images/s ;
  - trois presets verts, 67 tests en Debug.
- Écarts et problèmes :
  - la table de noms de SDL est restée celle d'une manette Xbox : `SDL_GetGamepadButtonFromString("south")`
    rend −1 alors que l'énumération s'appelle `SDL_GAMEPAD_BUTTON_SOUTH`. Vérifié avant d'écrire l'ADR, et le
    fichier de liaisons utilise `pad:a` ;
  - `pollEvents` change de type de retour (`Events`, deux listes) : la fenêtre et les périphériques sortent de
    la même file SDL, les séparer à la source évitait un `variant` ;
  - les bornes des codes (512 touches, 32 boutons…) sont vérifiées par `static_assert` contre celles de SDL :
    une version qui en ajoute casse le build au lieu de déborder d'un tableau d'état.
- Prochaine étape : #67 — la caméra libre, pilotée par ces actions.

## 2026-09-22 — M3.3 — Clôture

- **Temps Donnovan pour M3.3 : 1,33 h** (ratio 1,33), réconcilié sur le total de la journée : 3 h 30, dont
  2 h 10 déjà imputées jusqu'à M3.2. Déclaré PR par PR : 0,33 h — **quatre fois moins**. L'écart n'est pas une
  erreur de sa part : entre les deux PR, il a posé deux questions de conception (la struct enveloppe,
  `Transform` contre `WorldTransform`) et ouvert le sujet du jeu visé (Breath of the Wild, Xenoblade). Ce
  temps-là est du pilotage, il compte, et aucune question « combien pour cette PR ? » ne le capte. Réparti au
  prorata du déclaré : #64 à 0,33 h, #65 à 1,00 h.
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux ; critère mesuré et consigné ; CI verte,
  zéro erreur de validation ; README de `scene` à jour ; board renseigné ; tag `m3.3` et release. Pas d'étude :
  E3 vient à la fin de la phase 3.

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| État de simulation identique au bit près après N ticks, que le rendu tourne à 30, 60 ou 144 images/s | 120 pas donnent la même position aux trois cadences, comparée **sans tolérance** | `./build/linux-debug/tests/levain_tests` |
| En plus : coût d'un pas de simulation, 100 000 entités | **0,156 ms** | `./build/linux-release/tests/levain_scene_bench` |
| En plus : coût d'une passe de rendu, 100 000 entités toutes interpolées | **2,09 ms** | idem |
| En plus : M3.2 ne perd rien | 1,43 ms en chaînes, 1,42 en arbre large | idem |
| En plus : le sandbox tourne avec la nouvelle boucle | 34 959 frames en 10 s, 286 µs par image contre 214 en M3.2 | `SDL_VIDEO_DRIVER=offscreen ./build/linux-release/sandbox/levain_sandbox --seconds 10` |

### Temps

| Issue | Estimé | Passé |
|---|---:|---:|
| #64 ADR de la boucle à pas fixe (#102) | 0,35 h | 0,33 h |
| #65 Pipeline de simulation et interpolation (#103) | 0,65 h | 1,00 h |
| **M3.3** (ROADMAP) | **1,0 h** | **1,33 h** (ratio 1,33) |

Phase 3 à ce stade : 2,25 h passées pour 2,75 h estimées (M3.1 à M3.3), ratio 0,82. Le recalibrage se calcule
à la clôture de la phase, après M3.4, M3.5 et l'étude E3.

### Ce que M3.3 a appris

- **Un silence vaut un échec** : une requête dont le singleton manque ne correspond à rien, et son système ne
  tourne pas — sans rien dire. C'est la règle n°7 rencontrée pour la troisième fois du projet. Le banc l'a vue
  parce qu'un chiffre est tombé à 0,001 ms ; sans banc, le rendu aurait simplement gelé les objets.
- **Une signature peut coûter 30 % du budget** : `worldMatrix(parent, Transform)` composait la matrice locale
  elle-même et empêchait le compilateur d'éviter une copie — 0,45 ms sur 100 000 entités. L'écart traînait
  depuis M3.2, attribué à tort au coût des termes de requête. La méthode qui l'a trouvé (réécrire le même
  corps à la main dans un binaire de test, puis supprimer une différence à la fois) est notée dans le GOTCHA
  du skill `build`.
- **L'interpolation crée ses propres bugs** : une entité neuve s'affiche à l'origine, un objet téléporté
  traverse l'écran. Les deux se règlent au même endroit, un observateur sur le `Transform` posé à la main.
- **Deux questions de conception de Donnovan** (la struct enveloppe, `Transform` contre `WorldTransform`) sont
  archivées dans `docs/QA.md`. Sa règle pour trancher, qui vaut au-delà de ce cas : « je préfère que les choses
  soient explicites plutôt que quelqu'un qui relit le code ne le comprenne pas ». La paire de flecs, plus
  idiomatique et plus économe, est donc écartée au profit du type explicite ; le raisonnement est recopié
  au-dessus du composant, pas seulement dans `QA.md`.
- **Une question coûte du temps Donnovan**, et ce temps n'apparaît dans aucune réponse « combien pour cette
  PR ? ». C'est la quatrième fois que le déclaré par PR sous-estime, et la plus forte : ×4.

**Prochaine étape** : M3.4 — input par actions et caméra libre (#66, #67).

## 2026-09-22 — M3.3 — Boucle à pas fixe et interpolation (#64, #65)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : ADR-0016 (sondage : accumulateur et pipeline dédié, interpolation automatique de ce que la simulation
  déplace) ; `fixed_step.hpp` (`FixedStep`, `planSteps`, le plafond de 4 pas) ; composants `PreviousTransform`
  et `RenderAlpha` ; étiquette `Simulation` et pipeline créé par le module ; `SavePreviousTransform` ;
  `ApplyVelocity` passe dans le pipeline de simulation ; `ComputeWorldTransforms` affiche l'entre-deux ;
  observateur qui remet l'état précédent quand un `Transform` est posé à la main (naissance, téléportation) ;
  `advanceWorld` ; le sandbox y passe ; `levain_scene_bench` mesure le pas et la passe de rendu.
- Mesures (Release, machine de référence) :
  - **état identique au bit près quelle que soit la cadence** : 120 pas simulés à 30, 60 et 144 images/s
    donnent la même position, comparée sans tolérance (`levain_tests`) ;
  - **les tick sources de flecs ne rattrapent pas** : un système à `interval(1/60)` sur 60 images à 30
    images/s tourne 60 fois au lieu de 120 — d'où l'accumulateur (mesuré au prototype, consigné dans l'ADR) ;
  - un **pas de simulation** de 100 000 entités : **0,156 ms** ; une **passe de rendu** avec les 100 000
    interpolées : **2,09 ms** (`levain_scene_bench`) ;
  - les matrices monde de M3.2 ne perdent rien : **1,43 ms** en chaînes, 1,42 en arbre large ;
  - coût dans le sandbox (hors écran, `--seconds 10`) : 34 959 frames, **286 µs par frame** contre 214 en
    M3.2 ; les 10 000 cubes ont une `Velocity`, donc tous sont interpolés ;
  - trois presets verts, 60 tests en Debug.
- Écarts et problèmes :
  - **une requête dont le singleton manque ne correspond à rien** : sans `RenderAlpha` dans le monde, le
    système des matrices monde ne tournait pas — et se taisait (règle n°7). Trouvé par le banc, qui est tombé
    à 0,001 ms. Le module pose le singleton à l'import, et un test vérifie qu'un `progress` seul compose
    quand même ;
  - **une entité neuve s'affichait à l'origine** pendant une image : son état précédent était vide. Un
    observateur le remet à la valeur posée, ce qui couvre aussi les téléportations ;
  - **la signature d'une fonction a coûté 0,45 ms** sur 100 000 entités : `worldMatrix(parent, Transform)`
    composait la matrice locale elle-même, et le compilateur ne savait plus éviter une copie de matrice. Elle
    prend maintenant les deux matrices. Le même écart traînait depuis M3.2 sans être compris ;
  - le plafond de 4 pas se voit dans les tests : une image d'un quart de seconde ne simule que 4 pas, pas 15.
- Prochaine étape : clôture de M3.3, puis M3.4 (input par actions et caméra libre).

## 2026-09-22 — M3.2 — Clôture

- **Temps Donnovan pour M3.2 : 0,42 h** (ratio 0,56), confirmé par le total de la journée : 2 h 10 annoncées
  le soir, dont 1 h 45 déjà imputées jusqu'à M3.1. Pour une fois, le déclaré PR par PR (10 min pour #99,
  15 min pour #100) tombe juste — parce que les deux PR se sont suivies sans allers-retours entre elles.
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux ; critères mesurés et consignés ; CI verte,
  zéro erreur de validation ; README de `scene` à jour, limite d'instances nommée dans `render` ; board
  renseigné ; tag `m3.2` et release. Pas d'étude : E3 vient à la fin de la phase 3.

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| 100 000 entités sur 10 niveaux de profondeur recalculées en moins de 2 ms | **1,45 ms** en chaînes (1 enfant par parent), **1,43 ms** en arbre large (10 enfants par parent) | `./build/linux-release/tests/levain_scene_bench` |
| Un test vérifie qu'un enfant suit son parent déplacé (issue #63) | 4 tests, dont un petit-enfant dans une hiérarchie bâtie dans le désordre | `./build/linux-debug/tests/levain_tests` |
| En plus : bout en bout, par l'API de l'explorer | la grille levée de 10 lève la matrice monde du cube de 10, son `Transform` ne bouge pas | `./tools/explorer-check.sh` (Debug et ASan) |
| En plus : M3.1 tient toujours | `ApplyVelocity` seul, 100 000 entités : 0,065 à 0,098 ms | `levain_scene_bench` |
| En plus : le stockage écarté, pour mémoire | `ChildOf` + cascade : 15,1 ms en chaînes, 180 264 tables contre 280 | idem |

### Temps

| Issue | Estimé | Passé |
|---|---:|---:|
| ADR-0015, le stockage de la hiérarchie (#99) | — | 0,17 h |
| #63 Hiérarchie et matrices monde (#100) | 0,65 h | 0,25 h |
| **M3.2** (ROADMAP) | **0,75 h** | **0,42 h** (ratio 0,56) |

### Ce que M3.2 a appris

- **Un ADR peut contredire la ROADMAP, chiffres à l'appui.** La ROADMAP prévoyait `ChildOf` et une requête en
  cascade, la voie que montrent les exemples de flecs. Mesurée, elle ratait le critère d'un facteur 7,5 sur des
  chaînes, parce que chaque parent crée sa propre table. Une demi-heure de mesures avant d'écrire une ligne.
- **Le défaut d'une bibliothèque n'est pas toujours celui qu'on croit** : `group_by` range les tables par
  groupe mais **ne trie pas les groupes**. Le test qui échoue sans le drapeau a été écrit avant le code du
  système, et c'est lui qui a trouvé le piège.
- **Une requête juste peut coûter 26 fois trop cher** : demander « les enfants de `grid` » est exact, mais ne
  se résout pas table par table avec le stockage `Parent` (212 µs par frame contre 8). Le chiffre n'est apparu
  qu'en comptant les frames du sandbox — le banc, lui, ne mesurait pas cette requête.
- **Le coût d'un choix se paie ailleurs** : le sandbox passe de 62 à 214 µs par frame, pour 10 000 matrices
  monde recalculées à chaque tour. C'est assumé (ADR-0015) ; la détection de changement de flecs n'y servirait
  à rien, puisque `ApplyVelocity` écrit dans tous les `Transform` à chaque frame.

**Prochaine étape** : M3.3 — boucle à pas fixe, pipeline de simulation et interpolation (#64), avec un ADR à
écrire sur l'accumulateur et le garde-fou contre la « spirale de la mort ».

## 2026-09-22 — M3.2 — Hiérarchie et matrices monde (#63)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : ADR-0015 (sondage : le composant `flecs::Parent` plutôt que la relation `ChildOf`) ; composant
  `WorldTransform`, ajouté automatiquement avec `Transform` (trait `With`) ; `transform.hpp` (`localMatrix`,
  `worldMatrix`, `worldPosition`), sans flecs ; système `ComputeWorldTransforms` en `PostUpdate`, rangé par
  profondeur ; les 10 000 cubes du sandbox sont enfants d'une entité `grid` ; `levain_scene_bench` mesure les
  deux stockages ; `tools/explorer-check.sh` vérifie qu'un cube suit sa grille.
- Mesures (Release, machine de référence) :
  - **100 000 entités sur 10 niveaux recalculées en moins de 2 ms** : **1,45 ms** en chaînes (1 enfant par
    parent), **1,43 ms** en arbre large (10 enfants par parent), tour du monde complet
    (`./build/linux-release/tests/levain_scene_bench`) ;
  - pour mémoire, le stockage écarté, `ChildOf` + `cascade` : **15,1 ms** en chaînes (180 264 tables) et
    1,76 ms en arbre large (18 264 tables), contre 280 tables avec `Parent` ;
  - **un enfant suit son parent déplacé** : quatre tests (`levain_tests`), dont un petit-enfant dans une
    hiérarchie bâtie dans le désordre ; bout en bout par l'API de l'explorer, la grille levée de 10 lève la
    matrice monde du cube de 10 sans toucher à son `Transform` (`./tools/explorer-check.sh`, Debug et ASan) ;
  - M3.1 tient toujours : `ApplyVelocity` seul reste à **0,065 à 0,098 ms** pour 100 000 entités ;
  - coût du monde dans le sandbox (hors écran, `--seconds 10`) : 46 617 frames, soit **214 µs par frame**
    contre 62 µs en M3.1. Les 150 µs de plus sont les 10 000 matrices monde, recalculées à chaque tour ;
  - trois presets verts, 54 tests en Debug.
- Écarts et problèmes :
  - **`group_by` ne trie pas** : il parcourt les groupes dans l'ordre inverse de leur création. Sans
    `EcsQueryGroupByOrdered`, un petit-enfant est calculé avant son parent (x = 1 au lieu de 3, vérifié en
    retirant le drapeau) ;
  - **la première requête du rendu coûtait 212 µs par frame** au lieu de 8 : demander « les enfants de `grid` »
    (`(ChildOf, grid)` + un composant) ne se résout pas table par table avec le stockage `Parent`. Le sandbox
    marque ses cubes d'un tag ;
  - **lire le parent par l'API C++** (`entity(...).get<T>()`) ou appeler `it.world()` dans la boucle coûtait
    0,5 ms par 100 000 entités : le monde et l'identifiant du composant sont capturés une fois ;
  - rien n'est recalculé à la demande (pas de « dirty flags ») : tout est recalculé à chaque tour, comme dit
    l'ADR-0015. Dans le sandbox, la détection de changement de flecs ne servirait à rien de toute façon :
    `ApplyVelocity` écrit dans tous les `Transform` à chaque frame ;
  - `git checkout <fichier>` pour défaire un essai a effacé les modifications non indexées du fichier ; noté
    dans le GOTCHA du skill `session`.
- Prochaine étape : clôture de M3.2, puis M3.3 (boucle à pas fixe, ADR à écrire).

## 2026-09-22 — M3.1 — Clôture

- **Temps Donnovan pour M3.1 : 0,50 h**, réconcilié en fin de journée. Déclaré PR par PR : 15 min pour #96,
  5 min pour #97. Total de la journée annoncé le soir : 1 h 45, dont 1 h 15 déjà imputées jusqu'à la clôture de
  la phase 2 ; les 30 min restantes vont à M3.1, réparties au prorata du déclaré. Troisième fois que les
  réponses PR par PR sous-estiment (après M1.2, M2.1 et M2.2) : le total du soir reste la seule source.
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux ; critères mesurés et consignés ; CI verte, zéro
  erreur de validation ; README de `scene` (nouveau) et de `render` à jour ; board renseigné ; tag `m3.1` et
  release. Pas d'étude : E3 vient à la fin de la phase 3.

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| Les entités de la démo sont visibles et modifiables dans l'explorer | `cube_50_50` lu par l'API de l'explorer ; une vitesse posée le fait monter de ~5 unités en 1 s ; essayé par Donnovan dans flecs.dev/explorer | `./tools/explorer-check.sh` |
| Mise à jour de 100 000 entités (Transform + Velocity) en moins d'1 ms | médiane **0,071 à 0,095 ms** | `./build/linux-release/tests/levain_scene_bench` |
| En plus : flecs absent de `core`, `platform`, `gpu` et `render` | oui, contrôlé à chaque `ctest` | `ctest -R deps.flecs-visibility` |
| En plus : l'explorer n'écoute que sur la boucle locale, et pas du tout en Release | oui | `./tools/explorer-check.sh` |

### Temps

| Issue | Estimé | Passé |
|---|---:|---:|
| #61 Monde flecs, composants et systèmes (#96) | 0,5 h | 0,38 h |
| #62 Rendu du monde et explorer (#97) | 0,5 h | 0,12 h |
| **M3.1** (ROADMAP) | **1,0 h** | **0,50 h** (ratio 0,50) |

Le ratio de M3.1 ne déclenche rien seul : le recalibrage se calcule à la clôture de la phase 3 (ROADMAP,
« Recalibrage »).

### Ce que M3.1 a appris

- **Un outil de développement peut ouvrir une porte** : l'explorer de flecs écoutait par défaut sur toutes les
  interfaces, avec une API qui supprime des entités et exécute des scripts. Le contrôle qui le vérifie fait
  partie du script de M3.1.
- **La propriété de la mémoire traverse les API C** : `EcsRest::ipaddr`, libéré par flecs, ne se voyait qu'à la
  sortie du programme. ASan l'a trouvé ; le chemin normal n'aurait rien montré.
- **Tester la réflexion par son format de sortie** : le test JSON a trouvé le piège de `count = 1` avant que
  l'explorer n'affiche des tableaux.

**Prochaine étape** : M3.2 — relations `ChildOf` et matrices monde en cascade (#63).

## 2026-09-22 — M3.1 — Le rendu lit le monde ; explorer flecs en Debug (#62)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : réflexion des composants (addon meta : `vec3`, `quat`, `Transform`, `Velocity`) ; `updateInstances` ;
  les 10 000 cubes du sandbox sont des entités (`cube_x_z`), le rendu relève leurs positions à chaque frame ;
  addon REST en Debug seulement, sur `127.0.0.1` ; `tools/explorer-check.sh`.
- Mesures :
  - **entités visibles et modifiables** : `cube_50_50` lu par l'API REST de l'explorer, avec ses composants ;
    une vitesse de 5 posée par l'API le fait monter de 5,19 et 5,33 unités en une seconde
    (`./tools/explorer-check.sh`, Debug et ASan) ;
  - **l'explorer n'est pas compilé en Release** : `LEVAIN_ENABLE_EXPLORER` n'y est pas défini, aucun serveur
    n'écoute sur le port 27750 (même script) ;
  - **le serveur n'écoute que sur la boucle locale** ; contre-test sans `ipaddr` : `0.0.0.0:27750`, le script
    échoue ; restauré ;
  - coût du monde dans le sandbox (Release, hors écran, `--seconds 10`) : 161 321 et 162 088 frames, soit
    ~62 µs par frame contre ~45 µs en M2.2 ; 0,043 ms de GPU ;
  - trois presets verts, 49 tests.
- Écarts et problèmes : trois pièges de flecs, trouvés par un test ou par ASan, notés dans le README de
  `scene` : un champ déclaré avec `count = 1` devient un tableau ; `EcsRest::ipaddr` est libéré par flecs
  (« double free » à la sortie) ; le serveur REST écoute par défaut sur toutes les interfaces.
- Prochaine étape : clôture de M3.1.

## 2026-09-22 — M3.1 — Module scene : monde flecs, Transform, Velocity (#61)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : module `engine/scene` (flecs 4.1.6, PUBLIC) ; `Transform`, `Velocity` ; `applyVelocity`, logique sans
  flecs ; `SceneModule`, module flecs dont le système `ApplyVelocity` tourne dans `OnUpdate` ; trois tests ;
  `levain_scene_bench` ; test `deps.flecs-visibility`, qui échoue si flecs apparaît dans `core`, `platform`,
  `gpu` ou `render`.
- Mesures :
  - **100 000 entités (Transform + Velocity) mises à jour en moins d'1 ms** : médiane **0,071 à 0,095 ms** sur
    quatre lancements de 500 tours (`./build/linux-release/tests/levain_scene_bench`, Release) ;
  - **aucun en-tête flecs sous scene/** : `ctest -R deps.flecs-visibility` ; contre-test, un
    `#include <flecs.h>` dans `render` le fait échouer ; restauré ;
  - trois presets verts, 48 tests.
- Écarts et problèmes : l'issue demandait une mesure « avec Tracy » ; un bench dédié, comme celui des allocateurs,
  la rend reproductible sans profileur. `render` est ajouté à la liste des modules sans flecs : il n'est pas
  au-dessus de `scene` dans le graphe de SPECS §7.
- Prochaine étape : #62 — le renderer dessine le monde ; explorer flecs en Debug.

## 2026-09-22 — Phase 2 — Clôture (M2.3 compris)

- **Temps Donnovan pour la journée : 1,25 h** (« environ 1 h 15 »), réparti ainsi :
  - le sondage de l'ADR-0014, #83 et #84 : 0,5 h déclarées (« 30 min au plus ») ;
  - la lecture de l'étude E2 : 0,33 h (20 min) ;
  - le reste, 0,42 h, la relecture de #85 : réparti entre #45 et #46 au prorata de leurs estimations.
- Définition de « terminé » pour M2.3 (SPECS §9) : démo lançable sous Linux ; critères mesurés et consignés ; CI
  verte, zéro erreur de validation ; README de `core`, `platform` et `render` à jour ; board renseigné ; tag
  `m2.3` et release. Étude de la phase : **E2, écrite et lue** (#86).

### Critères de M2.3

| Critère (ROADMAP et issues) | Mesuré | Commande |
|---|---|---|
| Modification visible en moins d'1 s, sans redémarrer | **~450 ms**, du fichier enregistré au pipeline recréé | `./tools/shader-hot-reload.sh` |
| Une erreur de compilation ne fait pas planter | message de slangc dans le log, la boucle continue avec le shader précédent | idem |
| Seuls les pipelines touchés sont recréés | `triangle.slang` modifié : recompilé, « aucun pipeline à recréer » | `touch shaders/triangle.slang` pendant le sandbox |

### Temps de M2.3

| Issue | Estimé | Déclaré | Réconcilié |
|---|---:|---:|---:|
| #45 Surveillance et recompilation (ADR-0014, #83, #84, #85) | 0,65 h | 0,5 h | 0,84 h |
| #46 Repli sur erreur (#85) | 0,15 h | — | 0,08 h |
| #47 Étude E2 (#86) | 0,15 h | 0,33 h | 0,33 h |
| **M2.3** (ROADMAP) | **1,0 h** | | **1,25 h — ratio 1,25** |

### Phase 2 — le ratio

| Milestone | Estimé | Passé |
|---|---:|---:|
| M2.1 Caméra, meshes et binding sets | 1,75 h | 2,0 h |
| M2.2 Textures | 1,0 h | 1,0 h |
| M2.3 Hot-reload des shaders | 1,0 h | 1,25 h |
| **Phase 2** | **3,75 h** | **4,25 h** — ratio **1,13** |

**Dans la fourchette 0,8–1,25 : aucun recalibrage.** Le recalibrage de la phase 1 (×0,67) avait un peu trop
coupé, comme le craignaient ses deux réserves ; la phase 2 corrige en partie. Le ratio cumulé des phases 0 à 2,
(5,0 + 3,0 + 4,25) / (6,0 + 4,5 + 3,75) = **0,86**, est lui aussi dans la fourchette. M2.3 touche la borne haute
(1,25) : un ADR, trois PR de code et une étude, pour 1,0 h estimée. À surveiller en phase 3, qui a elle aussi un
ADR par milestone ou presque.

### Phase 4 détaillée

Huit issues créées, #87 à #94, sur les milestones M4.1 à M4.4, avec estimation (6,0 h au total, celui de la
ROADMAP) et phase sur le board. Chacune note qu'elle sera ajustée après M3.5, le choix du jeu.

### Ce que la phase 2 a appris

- **Les preuves visuelles deviennent des commandes** : RenderDoc s'automatise sans fenêtre (niveaux de mip,
  comparaison du filtrage), le hot-reload se vérifie par un script. Aucune capture d'écran de bureau.
- **Un test d'image évite ce que Vulkan laisse approcher** : un damier réduit dépendait du niveau de mip choisi
  par chaque pilote (152 pixels d'écart entre lavapipe et RADV).
- **Découper tôt coûte moins cher** : M2.3 faisait 575 lignes ; ADR, puis deux PR empilées de 227 et 348 lignes,
  relues en une matinée.
- **Une dépendance tierce peut déclencher un sanitizer** : stb_image_resize2 remplacé par vingt lignes plutôt
  qu'éteindre UBSan (règle n°4).

- Prochaine étape : M3.1 — intégration de flecs et explorer (#61, #62).

## 2026-09-22 — M2.3 — Hot-reload des shaders (#45, #46)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : ADR-0014 (sondage : relancer le build des shaders plutôt que la bibliothèque Slang) ;
  `platform::runProcess` (SDL_CreateProcessWithProperties) ; `core::FileWatch` (dates de modification) ;
  `render::reloadMeshPassShaders`, qui recrée le pipeline seul, sur une copie ; `sandbox/src/shader_reload.cpp`
  relie le tout ; `tools/shader-hot-reload.sh` vérifie teinte, erreur et retour à l'original.
- Mesures (machine de référence, Debug avec validation) :
  - **modification visible en moins d'1 s** : **~450 ms** du fichier enregistré au pipeline recréé. Détection
    6 à 98 ms (surveillance toutes les 100 ms), build 335 à 364 ms, pipeline 0,2 à 2,1 ms, sur six rechargements
    (`./tools/shader-hot-reload.sh`, fenêtre Wayland, hors écran et ASan) ;
  - **une erreur de compilation ne fait pas planter** : le message de slangc (fichier, ligne, colonne) va dans
    le log, la boucle tourne ses 10 s jusqu'au bout avec le shader précédent ;
  - **seuls les pipelines touchés sont recréés** : `triangle.slang` modifié → recompilé, « aucun pipeline à
    recréer » ;
  - options de mesure comparées pour l'ADR : build des shaders 330 à 400 ms, `slangc` seul 145 ms par point
    d'entrée ;
  - trois presets verts, 44 tests ; sous ASan et UBSan, le script passe aussi.
- Écarts et problèmes : le build bloque la boucle le temps de la compilation (~350 ms, une image figée) ; noté
  `ponytail:` dans le code, un thread si ça gêne.
- Prochaine étape : clôture de M2.3, puis étude E2 (#47) et clôture de la phase 2.

## 2026-09-21 — M2.2 — Clôture

- **Temps Donnovan pour M2.2 : 1,0 h**, réconciliée sur le total de la journée : 6 h déclarées, dont 5 h déjà
  affectées à la phase 1 (3,0 h) et à M2.1 (2,0 h). Les réponses après chaque PR donnaient 0,67 h (15, 10 et
  15 min) ; l'écart de 0,33 h, qui comprend l'installation de RenderDoc et sa question du premier lancement,
  est réparti au prorata des issues.
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux (Windows hors périmètre, ADR-0011) ; critères
  mesurés et consignés ; CI verte, zéro erreur de validation ; README de `assets`, `render` et `gpu` à jour ;
  board renseigné ; tag `m2.2` et release. Pas d'étude : E2 vient à la fin de la phase 2.

### Critères du milestone

| Critère (ROADMAP et issues) | Mesuré | Commande |
|---|---|---|
| Niveaux de mip vérifiés dans une capture RenderDoc | 9 niveaux, de 256 × 256 à 1 × 1 ; le niveau 1 × 1 à 192, la moyenne en lumière linéaire (160 sur les octets) | `QT_QPA_PLATFORM=offscreen qrenderdoc --python tools/renderdoc-mips.py` |
| Une texture s'affiche sur le cube | sandbox et test de fumée, référence regardée, identique sous lavapipe | `ctest -R smoke.cube` |
| Différence visible entre trilinéaire et anisotrope | contraste du damier près de l'horizon 0,088 → 0,160 | `tools/renderdoc-anisotropy.py` |
| Le niveau d'anisotropie est un paramètre | `SamplerSettings::maxAnisotropy`, `--anisotropy N` | `ctest -R clampAnisotropy` |

### Temps

| Issue | Estimé | Déclaré | Réconcilié |
|---|---:|---:|---:|
| #43 Textures et mipmaps (#78, #79) | 0,65 h | 0,42 h | 0,63 h |
| #44 Samplers et anisotrope (#80) | 0,35 h | 0,25 h | 0,37 h |
| **M2.2** (ROADMAP) | **1,0 h** | 0,67 h | **1,0 h — ratio 1,00** |

Phase 2 à ce jour : 3,0 h passées pour 2,75 h estimées sur M2.1 et M2.2, **ratio 1,09**, dans la fourchette
0,8–1,25. Le recalibrage de la phase 1 (×0,67) tient pour l'instant ; le point de contrôle reste la clôture de
la phase 2.

### Ce que M2.2 a appris

- Un test d'image doit éviter ce que Vulkan laisse approcher à chaque pilote : un damier réduit dépend du niveau
  de mip choisi (152 pixels d'écart entre lavapipe et RADV) ; une texture agrandie, non.
- Une bibliothèque tierce peut déclencher un sanitizer : stb_image_resize2 remplacé par vingt lignes plutôt que
  d'éteindre UBSan.
- RenderDoc s'automatise entièrement (capture, extraction de textures, images finales), sans fenêtre et sans
  capture d'écran : les preuves visuelles deviennent des commandes versionnées.

**Prochaine étape** : M2.3 — hot-reload des shaders.

## 2026-09-21 — M2.2 — Samplers et filtrage anisotrope (#44)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : `SamplerSettings`, `createSampler`, `clampAnisotropy` ; le sampler passe de la passe au matériau
  (`createMaterialBindings`) ; fonctionnalité `samplerAnisotropy` du device ; `createPlane` ; le sandbox
  ajoute un sol de 1000 unités, une caméra basse sur le côté de la grille et `--anisotropy N` (16 par défaut) ;
  `tools/renderdoc_capture.py`, partagé par `renderdoc-mips.py` et le nouveau `renderdoc-anisotropy.py`.
- Mesures (machine de référence) :
  - **différence visible entre trilinéaire et anisotrope** : planche `docs/images/m2.2-anisotropy.png`, tirée de
    deux captures RenderDoc (`QT_QPA_PLATFORM=offscreen qrenderdoc --python tools/renderdoc-anisotropy.py`) ;
    contraste du damier près de l'horizon **0,088 → 0,160**, et 0,32 → 0,34 près de la caméra
    (`magick captures/m2.2-aniso<N>.png -alpha off -crop 920x100+1000+400 +repage -colorspace gray
    -format '%[fx:standard_deviation]' info:`, puis `+1000+560`) ;
  - **le niveau est un paramètre** : `SamplerSettings::maxAnisotropy`, `--anisotropy N` ; `clampAnisotropy`
    testé, NaN compris ;
  - **coût** (Release, 10 000 cubes et le sol, `--seconds 10`) : 0,047 → 0,072 ms de GPU sous Wayland,
    0,024 → 0,036 ms hors écran (`./build/linux-release/sandbox/levain_sandbox --seconds 10 --anisotropy <N>`) ;
  - **contre-test** : sans `samplerAnisotropy`, la validation refuse le sampler
    (`VUID-VkSamplerCreateInfo-anisotropyEnable-01070`), assertion ; restauré ;
  - zéro erreur de validation en Debug ; le test de fumée, trilinéaire, est inchangé.
- Écarts et problèmes : deux pièges de mesure avec ImageMagick (alpha dans l'écart-type, `-gravity` qui
  persiste), notés dans le skill `build`.
- Prochaine étape : clôture de M2.2.

## 2026-09-21 — M2.2 — Cube texturé et mips vérifiées dans RenderDoc (#43, 2/2)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : `createTexture` (SRGBA8, tous les niveaux envoyés par `writeTexture`) ; binding set de matériau dans
  `space2` (texture et sampler trilinéaire), `space1` laissé vide ; coordonnées de texture sur le cube ; le
  sandbox et le test de fumée dessinent le damier `data/textures/checker.png` ; `tools/renderdoc-mips.py`
  capture une frame sous RenderDoc et en extrait chaque niveau de mip ; les `debugName` de NVRHI arrivent
  jusqu'à Vulkan (RenderDoc, messages de validation).
- Mesures :
  - **niveaux de mip dans une capture RenderDoc** : 9 niveaux, de 256 × 256 à 1 × 1, extraits de la frame 235
    (`QT_QPA_PLATFORM=offscreen qrenderdoc --python tools/renderdoc-mips.py`) ; planche
    `docs/images/m2.2-mips.png` (commande `magick` dans la PR) ;
  - **le niveau 1 × 1 vaut 192** : la moyenne en lumière linéaire des cases 255 et 64. Une moyenne des octets
    aurait donné 160 (`magick captures/m2.2-mip8.png txt:-`) ;
  - **texture sur le cube** : référence du test de fumée regardée, même géométrie, texture teintée par face. Le
    test lit `tests/data/rgbw-2x2.png`, agrandie, et non le damier : réduit sur une face de 20 pixels, le damier
    dépend du niveau de mip que chaque pilote choisit, et la CI (lavapipe) différait de 152 pixels de RADV ;
  - **coût du texturage** : nul à la mesure près. 10 000 cubes, Release : 0,043 ms de GPU sous Wayland, 0,022 ms
    hors écran, comme sans texture (`./build/linux-release/sandbox/levain_sandbox --seconds 10`) ;
  - zéro erreur de validation en Debug, sous RenderDoc compris ; trois presets verts.
- Écarts et problèmes :
  - qrenderdoc bloquait avant tout script : la question sur les statistiques d'usage, invisible en offscreen.
    Donnovan y a répondu (il a accepté).
  - RenderDoc 1.45 masque la surface Vulkan de Wayland : la capture passe par X11 (XWayland).
  - Pièges notés dans le skill `build`.
- Prochaine étape : #44 — samplers et filtrage anisotrope.

## 2026-09-21 — M2.2 — Module assets : images et mipmaps (#43, 1/2)

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : module `engine/assets` (dépend de `core` seul) ; `loadImage` (stb_image, toujours en RGBA8),
  `mipCountFor`, `buildMipChain` : filtre boîte 2 × 2, moyenne en lumière linéaire. Six tests unitaires, dont
  une image de test de 2 × 2 pixels générée par une commande notée dans le test.
- Mesures :
  - **moyenne en lumière linéaire** : un damier noir et blanc réduit à un pixel donne 188 ; contre-test avec une
    moyenne des octets, **128**, test rouge (`./build/linux-debug/tests/levain_tests -tc="*linéaire*"`) ;
  - trois presets verts, ASan et UBSan compris, clang-tidy et clang-format propres.
- Écarts et problèmes : stb_image_resize2 v2.10 déclenche UBSan (lecture 64 bits non alignée) : remplacé par
  une vingtaine de lignes, sans désactiver le sanitizer (règle n°4). Piège noté dans le skill `build`.
- Prochaine étape : #43, 2/2 — envoi au GPU avec tous les niveaux, binding set de matériau (`space2`,
  ADR-0013), cube texturé, vérification des niveaux dans une capture RenderDoc.

## 2026-09-21 — M2.1 — Clôture

- **Temps Donnovan pour M2.1 : 2,0 h**, réconciliées sur le total de la journée : 5 h déclarées, dont 3,0 h
  déjà affectées à la phase 1. Les réponses après chaque PR ne donnaient que 1,08 h ; l'écart de 0,92 h est
  réparti au prorata des issues (skill `session`). Il comprend le pilotage du recalibrage de la phase 1, que
  rien ne permet de séparer.
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux (Windows hors périmètre, ADR-0011) ; critères
  mesurés et consignés ; CI verte, zéro erreur de validation ; README de `render` à jour ; board renseigné ; tag
  `m2.1` et release. Pas d'étude : E2 vient à la fin de la phase 2.

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| 10 000 cubes instanciés à plus de 60 images/s en 1080p | **120 images/s**, la fréquence de l'écran ; 0,043 ms de GPU | `./build/linux-release/sandbox/levain_sandbox --seconds 10` |
| Temps GPU affiché | dans le titre et le log de fin, par timer queries : 0,011 ms pour 1 cube, 1,089 ms pour 1 000 000 | contre-test sur `GridSide` (#42) |
| En plus : depth buffer et sens des faces justes | test de fumée du cube ; sens inversé → 454 pixels différents, test rouge | `ctest -R smoke.cube` |

### Temps

| Issue | Estimé | Déclaré | Réconcilié |
|---|---:|---:|---:|
| #40 ADR-0013 : binding sets | 0,35 h | 0,33 h | 0,62 h |
| #41 Caméra, depth buffer et meshes | 0,65 h | 0,50 h | 0,92 h |
| #42 Instancing et temps GPU | 0,65 h | 0,25 h | 0,46 h |
| **M2.1** | **1,75 h** (ROADMAP ; 1,65 h sur le board) | 1,08 h | **2,0 h — ratio 1,14** |

Le ratio reste dans la fourchette 0,8–1,25. C'est le premier milestone mesuré après le recalibrage de la phase 1
(×0,67) : s'il se confirme au-dessus de 1, le recalibrage aura trop coupé, et la clôture de la phase 2 le
corrigera (ROADMAP, « Recalibrage »).

### Décisions

- **ADR-0013** : binding sets rangés par fréquence de changement (frame, passe, matériau), choisis par Donnovan
  sur sondage ; passer au bindless demandera un nouvel ADR.

### Ce que M2.1 a appris

- Un test d'image attrape ce qu'aucun test unitaire ne voit : le sens des faces inversé change 454 pixels.
- Le résultat d'une timer query arrive deux frames après sa mesure : il faut un anneau de requêtes, pas une seule.
- Les caches GitHub sont propres à une branche et à `main` : une PR ne profite du cache qu'une fois que `main`
  l'a enregistré. Les erreurs 504 de GitHub se traitent par une relance, pas par le code.

**Prochaine étape** : M2.2 — Textures (#43, #44).

## 2026-09-21 — M2.1 — Instancing et temps GPU (#42)

- **Temps Donnovan : 0,25 h déclarées** (relecture de #76), 0,46 h après réconciliation (clôture ci-dessus)
- Sessions Claude Code : 1
- Fait : `Instances` et `createInstances` — un vertex buffer de décalages, lu une fois par instance
  (`setIsInstanced`, slot 1) ; `drawMesh` dessine toutes les instances en un seul `drawIndexed` ; `GpuTimer` —
  un anneau de trois timer queries NVRHI, relues deux frames plus tard. Le sandbox dessine une grille de
  100 × 100 cubes dans une fenêtre 1920 × 1080 et affiche le temps GPU dans son titre ; le test de fumée du cube
  passe par le même chemin avec une seule instance, contre la même référence.
- Mesures (Release, machine de référence, `--seconds 10`, 10 000 cubes, 1920 × 1080 px) :
  - **Wayland** : 1204 frames en 10 s, soit **120 images/s**, la fréquence de l'écran (FIFO) ; **0,043 ms de
    GPU** par frame (`./build/linux-release/sandbox/levain_sandbox --seconds 10`) ;
  - **hors écran**, sans attente de l'écran : 241 542 frames en 10 s ; 0,022 ms de GPU
    (`SDL_VIDEO_DRIVER=offscreen ./build/linux-release/sandbox/levain_sandbox --seconds 10`) ;
  - **le chronomètre mesure bien le dessin** : 0,011 ms pour 1 cube, 1,089 ms pour 1 000 000 de cubes
    (`GridSide` passé à 1 puis 1000, `--seconds 3`, hors écran ; valeur d'origine restaurée) ;
  - **zéro erreur de validation** en Debug, sous Wayland et hors écran.
- Écarts et problèmes : en ASan local, les tests de fumée signalent la fuite de 128 octets de RADV déjà connue
  (`.agents/skills/build/GOTCHA.md`) ; ils passent avec `LD_PRELOAD=/usr/lib/libvulkan_radeon.so`.
- Prochaine étape : clôture de M2.1.

## 2026-09-21 — M2.1 — Test de fumée du cube (#41, 3/3)

- Temps Donnovan : à renseigner (compté avec #41)
- Sessions Claude Code : 1
- Fait : le test de fumée dessine le triangle **ou** le cube (`levain_smoke_render triangle|cube`, deux tests
  ctest) ; référence `tests/data/cube.ppm`, 64 × 64, cube à 35° autour de (1, 1, 0).
- Mesures (critère de #41 : « un cube tourne avec un depth buffer correct ») :
  - **référence regardée** : faces du dessus (verte) et avant (bleue) pleines, une tranche de la face gauche,
    aucune face intérieure visible ;
  - **élimination des faces** : désactivée, 0 pixel différent de la référence — elle ne retire que des faces
    cachées ; **sens des faces inversé**, 454 pixels différents — le test l'attrape ;
  - la réécriture du tableau des sommets (coins nommés, pour la lisibilité) passe contre la référence générée
    avant elle : géométrie inchangée.
- Prochaine étape : #42 — instancing et timestamps GPU.

## 2026-09-21 — M2.1 — Le cube : mesh indexé, depth buffer, constantes par frame (#41, 2/3)

- **Temps Donnovan : 0,33 h** (20 min déclarées, relecture de #74)
- Sessions Claude Code : 1
- Fait : `Mesh`, `createMesh`, `createCube` ; `MeshPass` — input layout, binding layout de frame dans `space0`
  (ADR-0013) avec un volatile constant buffer, depth buffer `D32`, élimination des faces arrière ;
  `shaders/mesh.slang`, compilé avec `-matrix-layout-column-major` ; le sandbox dessine un cube qui tourne.
- Mesures :
  - **zéro erreur de validation** au lancement, cube compris (Debug, RADV) : la convention d'espaces de
    l'ADR-0013 tient avec un binding layout (`setRegisterSpaceAndDescriptorSet(0)`) ;
  - le rendu à l'image près et le sens des faces sont vérifiés par le test de fumée du cube (#41, 3/3).
- Écarts et problèmes :
  - Deux pièges nommés dans le code : `VertexBufferBinding` et `IndexBufferBinding` n'ont pas de valeur par
    défaut pour leur slot, leur format et leur décalage ; la disposition des matrices est imposée à `slangc`
    plutôt que laissée à son défaut.
- Prochaine étape : le test de fumée du cube (#41, 3/3).

## 2026-09-21 — M2.1 — Caméra, chargement des shaders et cache CI (#41, 1/3)

- **Temps Donnovan : 0,17 h** (10 min déclarées, relecture de #73 ; estimé 0,65 h pour tout #41)
- Sessions Claude Code : 1
- Fait : première des trois PR de #41. GLM (port vcpkg 1.0.3) ; `Camera` et `viewProjectionOf` dans `render`,
  avec deux tests ; chargement des shaders sorti de `triangle.cpp` dans `src/shader.cpp`, pour servir aussi la
  passe des meshes ; cache des téléchargements de vcpkg en CI, à la demande de Donnovan.
- Mesures :
  - **le piège de la profondeur est testé** : `viewProjectionOf` range la profondeur de 0 à 1 ; contre-test avec
    la projection d'OpenGL (`perspectiveRH_NO`), le plan proche tombe à −1 et le test échoue ;
  - 28 tests.
- Écarts et problèmes :
  - **#41 découpée en trois PR** : la version d'un bloc faisait 640 lignes, et ce n'était pas du code Vulkan
    indivisible. Suivent le cube (mesh indexé, depth buffer, passe) et son test de fumée, déjà écrits et vérifiés.
  - Un `git stash` a refusé de remiser des fichiers marqués `git add -N` : sauvegarde complète d'abord, index
    vidé, puis remisage. Rien de perdu.
  - **Premier passage de la CI rouge sur les trois jobs** : le script de bootstrap de vcpkg refuse de démarrer
    si le dossier `VCPKG_DOWNLOADS` n'existe pas, et je ne le créais qu'après. Second commit : restauration et
    création du dossier avant le bootstrap.
- Prochaine étape : le cube (#41, 2/3).

## 2026-09-21 — M2.1 — ADR-0013 : binding sets (#40)

- Temps Donnovan : à renseigner (estimé 0,35 h pour #40, après recalibrage)
- Sessions Claude Code : 1
- Fait : les deux modèles de liaison de NVRHI écrits côte à côte (C++ et Slang), puis un sondage. **Donnovan
  retient les binding sets** ; passer au bindless demandera un nouvel ADR, justifié par une mesure. ADR-0013 écrit,
  avec la convention qui en découle : un binding layout par fréquence de changement (`space0` frame, `space1` passe,
  `space2` matériau), `setRegisterSpaceAndDescriptorSet` pour que l'espace devienne le descriptor set sous Vulkan.
- Écarts et problèmes : la convention d'espaces est lue dans le code de NVRHI, pas encore vérifiée par la
  validation ; elle le sera avec les premières constantes de M2.1 (#41).
- Prochaine étape : #41 — caméra 3D, depth buffer et meshes indexés.

## 2026-09-21 — Recalibrage après la phase 1

- Temps Donnovan : compté dans les 3,0 h de la journée
- Fait : ROADMAP v0.5 — estimations des phases 2 à 8 multipliées par le ratio de la phase 1 (0,67), arrondies au
  quart d'heure, échéances recalculées à 1,5 h par semaine (script dans la PR). **58,5 h → 39,0 h restantes ;
  68 h → 47 h au total ; le jeu jouable passe du 01/08/2027 au 21/03/2027.**
- Décisions : **Donnovan retient l'option 1**, la règle appliquée telle quelle (ratio 0,67), parmi trois : le ratio
  cumulé des phases 0 et 1 (0,76), ou attendre la phase 2. Le prochain point de contrôle est la clôture de la
  phase 2.
- Après fusion : échéances des milestones GitHub M2.1 à M8.3 et estimations des issues des phases 2 et 3 (#40 à
  #47, #61 à #69) mises à jour au même ratio.
- Prochaine étape : M2.1, en commençant par l'ADR de liaison (#40).

## 2026-09-21 — Phase 1 — Clôture (M1.3 compris)

- **Temps Donnovan pour la journée : 3,0 h** (« 3 h grand max », relecture de #60 et sondages sur E1 compris), le
  seul chiffre déclaré comme total. Réparti :
  - M1.1 : 1,25 h, déjà réconcilié sur le total du matin ;
  - l'après-midi, 1,75 h : les morceaux déclarés (#12 : 20 min, #13 : 15, #14 : 20, #15 : 15) sont augmentés au
    prorata ; **#16 et #17 n'ont jamais été chiffrés à part, et leurs 0,15 h chacun sont une répartition de ma
    part**. Le total de la phase ne dépend pas de cette répartition.
- Définition de « terminé » pour M1.3 (SPECS §9) : démo lançable sous Linux ; critères mesurés et consignés ; CI
  verte, zéro erreur de validation ; README de `render`, `core` et `gpu` à jour ; board renseigné ; tag `m1.3`.
  Étude de la phase : E1, écrite avant la phase, relue et corrigée (voir plus bas).

### Critères de M1.3

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| Triangle affiché sous Linux | oui, relu à l'image près par le test de fumée, dans le bon sens | `ctest -R smoke` |
| Zéro erreur de validation | 0, dans toutes les configurations | sandbox, CI |
| Frame time CPU < 1 ms | **0,090 ms** d'enregistrement et de soumission, 0,149 ms pour la frame hors attente de l'écran (Debug, validation) | `tools/tracy-capture.sh` |
| Image du test de fumée identique à la référence | oui sous lavapipe en CI, à ±2 près par canal, avec une référence générée sur RADV | CI |

### Phase 1 — le ratio

| Milestone | Estimé | Passé |
|---|---:|---:|
| M1.1 Fenêtre et boucle | 1,5 h | 1,25 h |
| M1.2 Device NVRHI et swapchain | 1,5 h | 0,72 h |
| M1.3 Premier triangle | 1,5 h | 1,03 h |
| **Phase 1** | **4,5 h** | **3,0 h** — ratio **0,67** |

**Hors de la fourchette 0,8–1,25, et cette fois ce n'est pas une erreur de mesure** : le total vient de Donnovan,
qui relit les diffs par petits morceaux entre deux tâches, et plus vite que prévu. La règle de la ROADMAP s'applique :
les estimations des phases 2 à 8 sont multipliées par 0,67 et les échéances avancées. **Proposé dans une PR à part,
`docs(roadmap): recalibrage phase 1`, que Donnovan valide ou amende.** Deux réserves à y peser : l'échantillon tient
en une journée intense, et la phase 0 donnait 0,83.

### #17 — lecture de l'étude E1, par sondages

Donnovan a demandé à être interrogé plutôt que de relire. Huit questions, **5 bonnes réponses** : durée de vie,
suivi d'états, niveaux d'abstraction, threads d'Unreal, frontière de NVRHI sont acquis. Les trois erreurs portent
sur la liaison des ressources (décalages de binding, binding sets contre bindless, volatile constant buffers), qui
arrive en phase 2 ; réponses archivées dans `docs/QA.md`. En relisant E1, j'y ai trouvé deux erreurs, corrigées :
`nvrhi::validation::createDevice` n'existe pas (c'est `createValidationLayer`), et la compilation des shaders ne
passe plus par ShaderMake (ADR-0005, amendement).

### Phase 3 détaillée

Neuf issues créées, #61 à #69, sur les milestones M3.1 à M3.5, avec estimation et phase sur le board (les
estimations suivent la ROADMAP actuelle ; le recalibrage les mettra à jour s'il est validé).

### Ce que la phase 1 a appris

- **Les garde-fous ont payé** : la boucle de CI trop courte (deux fois), la validation qui a arrêté le premier
  triangle sur `shaderDrawParameters`, `TRACY_ENABLE` désactivé par Tracy 0.14 — chaque fois, un contrôle a
  échoué bruyamment au lieu de rester vert.
- **La mesure du temps a encore dérivé**, sous une troisième forme : des morceaux « depuis ta dernière réponse »
  qui oubliaient le temps entre deux. Seul le total de la journée l'a rattrapée.
- **Le code Vulkan de base ne se découpe pas en PR de 400 lignes** ; le reste, si. Deux exceptions admises,
  une découpe faite (#58, #59).

- Prochaine étape : la PR de recalibrage, puis M2.1 — caméra, meshes et binding sets, en commençant par l'ADR de
  liaison (#40), là même où le quiz a montré les lacunes.

## 2026-09-21 — M1.3 — Test de fumée du rendu (#16)

- **Temps Donnovan : 0,15 h** (non chiffré à part : répartition de ma part dans le total de la journée, voir la clôture de la phase 1 ; estimé 0,25 h)
- Sessions Claude Code : 1
- Fait : `tests/smoke_triangle.cpp`, enregistré dans ctest (`smoke.triangle`) : fenêtre offscreen, triangle dessiné
  dans une texture de 64 × 64, recopié dans une texture lisible par le CPU, comparé à `tests/data/triangle.ppm`
  à ±2 près par canal ; `LEVAIN_UPDATE_REFERENCE=1` réécrit la référence.
- Mesures :
  - **le triangle est dessiné, et dans le bon sens** : référence générée sur RADV, validation active, zéro
    message ; regardée agrandie (rouge en bas à gauche, vert au sommet, bleu en bas à droite). C'est la première
    preuve à l'image que le rendu fonctionne, sans capture d'écran du bureau ;
  - **le test échoue si le triangle ne s'affiche plus** (critère de #16) : dessin retiré, test rouge ; le
    triangle couvre 512 pixels sur 4 096, soit 12,5 %, tous différents sans lui ;
  - 26 tests verts en Debug, Release et ASan (avec RADV préchargé).
- Écarts et problèmes :
  - **La référence vient de RADV, la CI compare avec lavapipe.** La tolérance de ±2 absorbe les écarts
    d'interpolation ; un écart de couverture sur les bords du triangle ferait échouer le test. Non vérifiable ici,
    lavapipe n'étant pas installé sur la machine de référence : la CI de cette PR le dira.
  - clang-tidy refusait une multiplication en `int` convertie en décalage de pointeur : calculée en `size_t`.
- Prochaine étape : clôture de M1.3, puis de la phase 1 (ratio, recalibrage, détail de la phase 3).

## 2026-09-21 — M1.3 — Premier triangle (#15)

- **Temps Donnovan : 0,31 h** (15 min déclarées, relecture de #59 comprise ; réconcilié, voir la clôture de la phase 1 ; estimé 0,5 h)
- Sessions Claude Code : 1
- Fait : module `engine/render` et `TrianglePass`, qui ne connaît que `nvrhi::IDevice` (SPIR-V ou DXIL choisi
  d'après l'API du device) ; `readFile` dans `core` ; `swapchainFormat` dans `gpu` ; le triangle dessiné par le
  sandbox, avec des zones Tracy pour mesurer le travail CPU d'une frame.
- Mesures :
  - **frame time CPU** (critère de M1.3, < 1 ms) : **0,090 ms** pour enregistrer et soumettre une frame, **0,149 ms**
    pour la frame entière hors attente de l'écran, en Debug avec validation ; un pic isolé à 1,33 ms sur 19 945
    frames (`SDL_VIDEO_DRIVER=offscreen ./tools/tracy-capture.sh 3 captures/m1.3.tracy`, zones `commandes` et
    `rendu`) ;
  - **zéro message de validation** : Debug sous Wayland, X11 et offscreen, Release, ASan avec RADV préchargé ;
  - 25 tests (23 + 2 pour `readFile`).
- Écarts et problèmes :
  - **Première vraie erreur de validation attrapée par l'assertion** : le SPIR-V de Slang déclare
    `DrawParameters` pour traduire `SV_VertexID`, et le device n'activait pas `shaderDrawParameters`. Corrigé
    dans `device_vk.cpp` ; la raison est dans l'ADR-0005.
  - **Je n'ai pas vu le triangle** : pas de capture d'écran (règle du skill `build`). Ce qui est vérifié :
    pipeline créé, draw enregistré, zéro erreur de validation. La preuve à l'image près viendra du test de fumée
    (#16), qui relit les pixels ; en attendant, c'est à Donnovan de le regarder.
  - **Bridage passager de l'environnement** : pendant quelques minutes, 20 images/s très régulières sous Wayland
    et une fenêtre X11 qui ne s'ouvrait plus, puis tout est revenu sans changement de code. Noté dans
    `build/GOTCHA.md`, avec la parade : mesurer en offscreen.
  - **CI rouge en Release, et c'est le garde-fou de M1.2 qui a sonné** : le démarrage du runner a pris ~10 s
    (2,3 s rien que pour le device, contre 0,9 s d'habitude), et le SIGTERM des 8 s est tombé avant la première
    frame — boucle de 0,5 s, contrôle « ≥ 1 s » en échec. En M1.1, j'avais préféré `timeout` à une option du
    sandbox, pour écrire moins de code ; les faits me donnent tort. Parade : `--seconds N`, compté depuis le
    premier tour de boucle, et `timeout` à 60 s comme simple filet (arguments testés : 2 et 0,5 acceptés,
    `abc` et `-1` refusés avec le code 2).
- Prochaine étape : #16, le test de fumée sous lavapipe qui compare l'image rendue à une référence.

## 2026-09-21 — M1.3 — Compilation des shaders Slang (#14)

- **Temps Donnovan : 0,42 h** (20 min déclarées, relecture de #58 comprise ; réconcilié, voir la clôture de la phase 1 ; estimé 0,5 h)
- Sessions Claude Code : 1
- Fait : compilation des shaders Slang au build par des commandes CMake et `slangc` (ports vcpkg `shader-slang`
  et `directx-dxc`), en SPIR-V et en DXIL, choix de Donnovan inscrits dans l'ADR-0005 ; `shaders/triangle.slang`,
  qui servira au premier triangle (#15).
- Mesures :
  - **une erreur de shader fait échouer le build** : `error[E30015]: undefined identifier`, code 255 (faute
    injectée depuis une copie, puis retirée) ;
  - **seul un shader modifié est recompilé** : après `touch shaders/triangle.slang`, `ninja -n` ne liste que ses
    deux points d'entrée ; sans modification, `no work to do` ;
  - **DXIL bien formé** : deux tests `dxil.*` (désassemblage par `dxc -dumpbin`), le fichier porte un hash de
    shader.
- Écarts et problèmes :
  - Mon premier contrôle « seul un shader modifié est recompilé » ne prouvait rien : le reste du projet n'était
    pas encore recompilé, et `ninja -n` listait aussi du C++. Refait après un build complet.
  - PR découpée par issue : la version commune avec le triangle faisait 504 lignes, et ce n'était pas un bloc
    Vulkan indivisible (règle n°2).
- Prochaine étape : #15, le premier triangle.

## 2026-09-21 — M1.2 — Clôture

- **Temps Donnovan pour M1.2 : 0,72 h**, réconcilié à la clôture de la phase 1 (0,58 h déclarées d'abord, puis 0,88 h,
  puis la répartition finale) ; ce qui suit décrit la version provisoire. 0,58 h déclarées (10 + 10 + 15 min) : ce sont des réponses
  « depuis la dernière fois », que la clôture de M1.1 a montrées incomplètes. À réconcilier avec le total de la
  journée en fin de session (skill `session`).
- Définition de « terminé » (SPECS §9) : démo lançable sous Linux (Windows hors périmètre, ADR-0011) ; critères
  mesurés et consignés ; CI verte, zéro erreur de validation ; README de `gpu` et de `platform` à jour ; board
  renseigné ; tag `m1.2` et release. Pas d'étude : elle vient à la fin de la phase 1 (E1, déjà écrite).

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| Zéro erreur de validation sur 5 minutes avec redimensionnements | **0 message, 584 redimensionnements**, sandbox vivant | `./tools/kwin-window-smoke.sh 300` |
| Temps de démarrage mesuré | device créé en **30 à 40 ms** (RADV) ; 0,9 à 1,3 s en CI (lavapipe) | sandbox, « device créé en » |
| En plus, reporté de M1.1 : minimisation sous Wayland | la boucle s'endort, 0 ms de CPU | même script |

### Temps

| Issue | Estimé | Passé déclaré |
|---|---:|---:|
| #12 Device Vulkan et NVRHI | 1,0 h | 0,33 h |
| #13 Swapchain et écran effacé | 0,5 h | 0,25 h |
| **M1.2** (ROADMAP) | **1,5 h** | **0,58 h** — ratio 0,39, à réconcilier |

### Décisions

- **ADR-0012** : vk-bootstrap pour le device Vulkan, choisi par Donnovan sur question posée avant l'implémentation.
- **Règle n°2** : du code Vulkan qui forme un bloc peut dépasser les ~400 lignes s'il reste lisible et que l'écart
  est signalé (Donnovan, après relecture de #56). Inscrit dans `AGENTS.md`.

### Ce que M1.2 a appris

- **Une panne silencieuse de plus, en CI** : le démarrage sur lavapipe mangeait tout le délai du sandbox, qui ne
  testait plus la boucle en restant vert. La CI exige maintenant une boucle d'au moins une seconde.
- **Les pièges de NVRHI viennent de sa façon d'être compilé et enveloppé** : dispatcher de Vulkan-Hpp à définir
  soi-même (bibliothèque statique), sémaphores accessibles seulement sous l'enveloppe de validation.
- **Mon propre `GOTCHA.md` ne sert que s'il est relu** : le piège `[[maybe_unused]]`, inscrit le matin, m'a coûté
  un build l'après-midi. Relu avant #13, il n'a pas resservi.

- Prochaine étape : M1.3 — premier triangle (shaders Slang, pipeline graphique, test de fumée sous lavapipe).

## 2026-09-21 — M1.2 — Swapchain, redimensionnement et écran effacé (#13)

- **Temps Donnovan : 0,31 h** (15 min déclarées, relecture de #56 comprise ; réconcilié, voir la clôture de la phase 1 ; estimé 0,5 h)
- Sessions Claude Code : 1
- Fait : swapchain Vulkan (vk-bootstrap) dont les images sont enveloppées en textures NVRHI ; reconstruction dès
  que la taille de la fenêtre change ; sémaphores d'acquisition et de présentation ; deux frames en vol au plus
  (*event queries* NVRHI) ; présentation FIFO ; chaque frame efface l'écran à une couleur ; `windowPixelSize`
  dans `platform` ; redimensionnements en boucle dans `tools/kwin-window-smoke.sh` ; réponse sur Windows et
  Direct3D 12 archivée dans `docs/QA.md`.
- Mesures (machine de référence, Wayland sauf mention) :
  - **5 minutes de redimensionnements, validation active : 584 changements de taille, zéro message de
    validation**, sandbox toujours vivant, code 0 (`./tools/kwin-window-smoke.sh 300`, sandbox en Debug) ;
  - **la fenêtre apparaît sous Wayland**, et KWin lit son titre : `Levain - 8.333 ms (min 7.176, max 9.490) -
    120 images/s` (`gdbus … krunner1.Match Levain`) ;
  - **minimisation sous Wayland** (reportée de M1.1) : « masquée » puis « visible », 0 ms de CPU en 2 s
    minimisée ;
  - **CPU visible : 40 à 50 ms en 2 s, contre 2 010 ms** avant la swapchain : la présentation FIFO cadence la
    boucle sur les 120 Hz de l'écran (600 frames en 5,0 s) ;
  - zéro message de validation aussi sous X11, en offscreen (surface headless, comme la CI), en Release, et
    sous ASan avec RADV préchargé (code 0).
- Écarts et problèmes :
  - **`queueWaitForSemaphore` n'existe pas sur `nvrhi::IDevice`**, seulement sur `nvrhi::vulkan::IDevice`, que
    l'enveloppe de validation n'expose pas. La swapchain garde donc le device Vulkan de NVRHI, et le moteur
    l'enveloppe, comme Donut.
  - **Les images de la swapchain sont des textures NVRHI** : elles doivent disparaître avant le device NVRHI.
    La swapchain devient le troisième membre de `GpuDevice`, déclaré en dernier pour être détruit en premier.
  - clang-tidy refuse une constante globale `nvrhi::Color` (constructeur non `noexcept`) : rendue locale.
  - En offscreen, la surface headless ne cadence rien : ~11 000 images/s. Sans conséquence, la CI vérifie la
    durée de la boucle, pas sa fréquence.
- Prochaine étape : clôture de M1.2 (critères tenus : 5 minutes sans erreur de validation, temps de démarrage
  mesuré), puis M1.3 — premier triangle.

## 2026-09-21 — M1.2 — Device Vulkan et NVRHI (#12)

- **Temps Donnovan : 0,41 h** (10 + 10 min déclarées, relectures de #54 et #55 ; réconcilié, voir la clôture de la phase 1 ; estimé 1,0 h)
- Sessions Claude Code : 1
- Fait : module `engine/gpu` — instance, surface et device Vulkan créés avec vk-bootstrap (ADR-0012), puis device
  NVRHI par-dessus ; couches de validation Vulkan et couche de validation NVRHI en Debug, messages redirigés vers
  nos logs, et arrêt sur assertion à la première erreur ; nom du GPU et version du pilote journalisés ; temps de
  création mesuré dans le sandbox ; lavapipe et couches de validation installés en CI.
- Mesures :
  - GPU choisi : **AMD Radeon RX 9070 XT (RADV GFX1201), pilote radv Mesa 26.2.3, Vulkan 1.4.354** — le
    discret, pas l'iGPU (`SDL_VIDEO_DRIVER=x11 ./build/linux-debug/sandbox/levain_sandbox`) ;
  - **device créé en 30 à 40 ms**, validation comprise (même commande, sous Wayland et X11) ;
  - **zéro message des couches de validation** au lancement. Les deux seules erreurs journalisées viennent du
    loader, qui signale la couche Lossless Scaling cassée (SPECS §10) ;
  - validation réellement active : couche `VK_LAYER_KHRONOS_validation` insérée en Debug, absente en Release
    (`VK_LOADER_DEBUG=layer`) ;
  - **contre-tests** : un buffer Vulkan de taille 0 (`VUID-VkBufferCreateInfo-size-00912`) et une texture NVRHI
    de largeur 0 arrêtent chacun le programme sur l'assertion (code 133, SIGTRAP) ;
  - sanitizers sur le vrai GPU : 21 tests verts ; le sandbox signale 128 octets alloués par RADV, un faux
    positif (voir ci-dessous) ; **code 0 sous Wayland avec RADV préchargé**.
- Écarts et problèmes :
  - **Fuite de 128 octets signalée dans RADV**, même famille que libX11 en M1.1 : le loader décharge le pilote
    à la destruction de l'instance. Isolé par élimination : elle disparaît quand RADV reste chargé, persiste
    sans couche de validation (build Release instrumenté) et sans `device_select`.
  - **Le piège `[[maybe_unused]]` inscrit le matin même dans `build/GOTCHA.md`** a resservi dès l'après-midi :
    le build Release a échoué sur `isValidationError`, qui ne sert qu'à une assertion. Il aurait fallu relire le
    fichier avant d'écrire, comme le demande `AGENTS.md`.
  - Pour retirer une faute injectée, un `git checkout` du fichier a effacé l'intégration du device dans le
    sandbox, pas encore commitée. Réécrite ; la parade est dans `build/GOTCHA.md`.
  - Sous X11, précharger RADV et libX11 ensemble bloque `SDL_CreateWindow` (`XIfEvent` attend un `MapNotify`).
    Configuration de diagnostic seulement ; noté.
  - **CI verte du premier coup sur lavapipe** (llvmpipe, Mesa 25.2.8, Vulkan 1.4.318), sans message de
    validation ni fuite. Mais **la boucle n'y tournait plus** : le device y met 0,9 à 1,3 s à se créer, et le
    démarrage complet mangeait les 3 s du délai — la création est journalisée après le SIGTERM. L'étape restait
    verte sans rien tester de la boucle : une panne silencieuse de plus. Parade : délai porté à 8 s, et le
    sandbox journalise la durée de sa boucle, que la CI exige d'au moins une seconde (contre-test de
    l'expression : 0,4 s refusé, 1,2 s accepté).
  - PR découpée : l'ADR-0012 et la correction du temps de M1.1 sont partis d'abord (#54), règle n°3.
- Prochaine étape : #13 — swapchain, redimensionnement et écran effacé, avec les points reportés de M1.1.

## 2026-09-21 — M1.1 — Clôture

- **Temps Donnovan pour M1.1 : 1,25 h** — total de la journée déclaré par Donnovan (« environ 1 h 15 depuis ce
  matin »). Les réponses données après chaque PR ne faisaient que 40 min (20 + 10 + 10) : elles oubliaient le
  temps passé entre deux, en allers-retours. L'écart est réparti au prorata sur les issues.
- Définition de « terminé » (SPECS §9) : 6 critères sur 7 remplis. Démo lançable sous Linux (Windows hors
  périmètre, ADR-0011) ; critères chiffrés mesurés et consignés ; CI verte, avec trois checks requis ; README de
  `platform` et de `core` à jour ; board renseigné ; tag `m1.1` et release publiés. Pas d'étude : elle vient à
  la fin de la phase.

### Critères du milestone

| Critère (ROADMAP) | Mesuré | Commande |
|---|---|---|
| Redimensionnement et minimisation sans plantage | oui, sous X11 ; **Wayland reporté en M1.2** (#13), la fenêtre n'y apparaît qu'avec la première image | `tools/kwin-window-smoke.sh` |
| Frame time affiché | oui, dans le titre, lu par KWin | `gdbus … krunner1.Match Levain` |
| Zéro fuite signalée par les sanitizers | oui, job `linux-asan` requis sur `main` | CI |
| En plus : capture Tracy (#38) | 1,8 µs par frame, dont 85 % à pomper les événements SDL | `tools/tracy-capture.sh` |

### Temps

| Issue | Estimé | Passé |
|---|---:|---:|
| #10 Fenêtre, boucle, événements | 0,75 h | 0,63 h |
| #11 Frame time et sanitizers | 0,75 h | 0,31 h |
| #38 Capture Tracy (reportée de M0.3) | 0,25 h | 0,31 h |
| #32 Issues de phase 1 après l'ADR-0010 | 0,25 h | 0 h — fait en phase 0, déjà compté dans ses 5,0 h |
| **M1.1** (estimation de la ROADMAP) | **1,5 h** | **1,25 h** — ratio **0,83** |

Ratio dans la fourchette 0,8–1,25, le même que celui de la phase 0. La première version de cette entrée
annonçait 0,44, calculé sur les seuls morceaux déclarés : **la même erreur de mesure qu'en phase 0, sous une
autre forme**. La question « depuis ta dernière réponse » laisse tomber le temps passé entre deux ; seule la
question sur le total de la journée l'a rattrapé.

### Ce que M1.1 a appris

- **Trois pannes silencieuses de plus, toutes attrapées cette fois** : le titre qui n'atteignait jamais l'écran
  sous X11 (bug SDL), la fenêtre invisible sous Wayland, et Tracy 0.14 qui compilait un profilage vide. La règle
  « un contrôle échoue bruyamment » est devenue la règle n°7, et chacun de ces cas a son garde-fou ou son
  assertion.
- **Les instructions ont changé de forme** : `AGENTS.md` et quatre skills avec leurs `GOTCHA.md`, à la demande de
  Donnovan. Ils ont servi dès la PR suivante, et j'y ai ajouté deux pièges le jour même.
- **Un incident** : une capture d'écran qui a saisi le navigateur de Donnovan au lieu du profileur. Supprimée ;
  la règle est inscrite. Le serveur MCP de Tracy, signalé par Donnovan, rendra les captures d'écran inutiles.

- Prochaine étape : M1.2 — `DeviceManager` Vulkan pour NVRHI (#12), puis swapchain et écran effacé (#13, avec les
  trois points reportés de M1.1). Mettre en place le serveur MCP de Tracy au passage.

## 2026-09-21 — M1.1 — Capture Tracy de la vraie boucle (#38)

- **Temps Donnovan : 0,31 h** (10 min déclarées ; 0,31 h après réconciliation, voir la clôture ; estimé 0,25 h)
- Sessions Claude Code : 1 (la même que #10, #11 et #51)
- Fait : port overlay `ports/tracy`, Tracy **0.14.1 client seul** (51 lignes, contre 759 pour le
  port officiel et ses quatre patches, qui ne concernent que les outils) ; garde-fou CMake sur `TRACY_ENABLE` ;
  outils 0.14.1 officiels dans `~/.local/opt/tracy-0.14.1/`, avec l'accord de Donnovan ; script
  `tools/tracy-capture.sh` ; zone `titre` dans la boucle ; amendement de l'ADR-0007 (ports overlay) ; `ports/`
  dans la clé du cache CI.
- Mesures (`SDL_VIDEO_DRIVER=x11 ./tools/tracy-capture.sh 3 captures/m1.1.tracy`) :
  - **1 807 095 frames en 3,3 s** : ~1,8 µs par frame, ~550 000 images/s avec Tracy (~820 000 sans) ;
  - **`événements` : 85,2 % du temps**, 1 555 ns en moyenne (de 1 190 ns à 254 709 ns). La frame est donc
    presque entièrement le pompage des événements de SDL, la seule chose que fait la boucle avant M1.2 ;
  - `titre` : 3 appels, 41,5 µs en moyenne, une fois par seconde ;
  - client actif : il écoute sur `*:8086` (`ss -ltnp`), `TRACY_NO_EXIT=1` l'empêche de sortir (code 137 au
    délai), 1 046 symboles `tracy::` dans le binaire (`nm -C | grep -c`) ;
  - release officielle : SHA-256 identique à celle publiée par GitHub (`gh api …/releases/tags/v0.14.1`) ;
  - vcpkg `master` toujours en 0.13.1 au 21/09 (troisième critère de l'issue).
- Écarts et problèmes :
  - **Tracy 0.14 a fait passer `TRACY_ENABLE` de ON à OFF par défaut.** Le premier build profilé compilait,
    mais sans aucun symbole `tracy::` : un profilage vide, sans un mot. Le port force l'option, et
    `engine/core/CMakeLists.txt` refuse de configurer sans elle. Contre-test : l'option retirée, CMake échoue.
  - **Capture d'écran ratée, et un incident.** Pour photographier le profileur, j'ai demandé le focus à KWin puis
    lancé `spectacle -a` (fenêtre active). KWin n'a pas donné le focus : l'image montrait le navigateur de
    Donnovan, sur une page de connexion. Supprimée aussitôt, jamais commitée ni envoyée. Règle inscrite dans le
    skill `build` : pas de capture d'écran du bureau, on la demande à Donnovan.
  - `pkill -f <motif>` a tué le shell qui l'exécutait, sa ligne de commande contenant le motif.
- Prochaine étape : capture d'écran de la timeline par Donnovan, puis clôture de M1.1.

## 2026-09-21 — M1.1 — AGENTS.md et skills

- Temps Donnovan : compté avec #11 (10 min pour les relectures de #50 et #51)
- Sessions Claude Code : 1 (la même que #10 et #11)
- Fait : à la demande de Donnovan, `CLAUDE.md` (182 lignes) devient `AGENTS.md`, source unique des
  instructions quel que soit l'outil, et `CLAUDE.md` ne fait plus que l'importer. Les procédures passent dans
  quatre skills sous `.agents/skills/` — `session`, `build`, `cloture`, `questions` —, chacun avec un
  `GOTCHA.md` qui recense les pièges rencontrés depuis le début du projet. `.claude/skills` est un lien vers ce
  dossier.
- Décisions : pas d'ADR, ce n'est pas une décision d'architecture du moteur. La règle « un contrôle échoue
  bruyamment », adoptée à la clôture de la phase 0, devient la règle non négociable n°7.
- Écarts et problèmes : la découverte des skills par Claude Code **à travers le lien** n'est pas vérifiable dans
  cette session, la liste étant chargée au démarrage. Sans conséquence si elle échoue : `AGENTS.md` donne le
  chemin de chaque skill.
- Prochaine étape : au début de la prochaine session, vérifier que les quatre skills apparaissent. Puis #38,
  capture Tracy.

## 2026-09-21 — M1.1 — Frame time, sanitizers et avertissements (#11)

- **Temps Donnovan : 0,31 h** (10 min déclarées, relectures de #50 et de #51 ; 0,31 h après réconciliation,
  voir la clôture ; estimé 0,75 h)
- Sessions Claude Code : 1 (la même que #10, PR découpée)
- Fait : `recordFrame` dans `core` (moyenne, minimum et maximum par période d'une seconde) et trois tests ;
  frame time dans le titre de la fenêtre ; `setWindowTitle` et son assertion ASCII ; preset `linux-asan`
  (AddressSanitizer, LeakSanitizer, UBSan, `-fno-sanitize-recover=all`) et son job CI, avec un garde-fou qui
  vérifie que le binaire est bien instrumenté ; `-Wall -Wextra -Werror` sur tout le code du moteur.
- Mesures :
  - titre lu par KWin, build ASan : `Levain - 0.002 ms (min 0.002, max 0.083) - 605107 images/s`
    (`gdbus call --session --dest org.kde.KWin --object-path /WindowsRunner --method org.kde.krunner1.Match Levain`) ;
  - temps passé masquée exclu du frame time : max **0,036 ms** 0,3 s après une minimisation de 2 s ;
    **contre-test**, la remise à l'heure retirée : max **2 314,870 ms** (même commande, pilotage par KWin) ;
  - sanitizers : 21 tests et 3 s de sandbox, zéro fuite et zéro comportement indéfini, en offscreen (comme la
    CI) et sous Wayland (`SDL_VIDEO_DRIVER=offscreen timeout --foreground --preserve-status -k 10 3 ./build/linux-asan/sandbox/levain_sandbox`) ;
  - garde-fou d'instrumentation : 49 symboles `__ubsan_handle` dans le binaire ASan, aucun `__asan_init` dans
    le binaire Debug (`nm … | grep`) ;
  - avertissements à l'activation : **5**, pas 4 comme annoncé — ma première mesure ne portait que sur le
    Debug. 4 `-Wmissing-designated-field-initializers` (un `WindowEvent` sans `pixelSize`), et en Release une
    variable qui ne servait qu'à une assertion (build des trois presets avec `-Wall -Wextra -Werror`).
- Écarts et problèmes :
  - **Première vraie trouvaille du sanitizer, et elle est chez SDL.** Sous X11, LeakSanitizer signalait une
    fuite dans `SDL_X11_SetWindowTitle` (`SDL_x11window.c:2300`). En remontant : en locale C, la conversion
    du titre pour l'ancienne propriété `WM_NAME` échoue sur « — », et SDL abandonne **sans rien dire** —
    sans libérer la conversion, et sans envoyer le titre UTF-8. KWin affichait toujours « Levain » : le frame
    time n'avait jamais atteint l'écran. Confirmé par l'expérience (titre ASCII : ni fuite, titre affiché) ;
    même code sur la branche `main` de SDL. Parade : titres ASCII, vérifiés par assertion.
  - **Faux positifs LeakSanitizer sous X11** : 50 052 octets en 913 allocations, la mémoire permanente de
    libX11 que SDL décharge par `dlclose` à la sortie. Précharger libX11 les fait disparaître sans masquer les
    vraies fuites : la fuite du titre restait signalée. Zéro sous Wayland et en offscreen.
  - clang-tidy 22 compte `optional::value()` comme un accès non vérifié, et ne voit pas le `REQUIRE` de
    doctest comme une garde : les tests passent par `value_or(FrameTimeSummary{})`, dont les zéros font
    échouer les `CHECK` si aucun résumé n'est rendu.
  - **`LEVAIN_ASSERT` en Release** passait de `((void)0)` à `((void)sizeof(static_cast<bool>(expression)))` :
    l'expression est compilée sans être évaluée. Une variable qui ne sert qu'à une assertion n'est plus
    « inutilisée », et une assertion qui ne compile plus casse aussi le Release. Reste un cas : une fonction
    interne qui n'apparaît que dans une assertion, que clang déclare inutile (`isAscii`, `[[maybe_unused]]`).
- Décisions (Donnovan, 21/09) : `linux-asan` devient un **check requis** pour fusionner sur `main` ;
  avertissements activés ; **rien n'est signalé en amont à SDL**, dont les mainteneurs n'acceptent pas les
  contributions d'IA. Les deux bugs restent documentés dans `engine/platform/README.md`.
- Prochaine étape : #38, capture Tracy. vcpkg n'ayant toujours pas Tracy 0.14, la voie suivante décidée en
  M0.3 est un port overlay en 0.14.1.

## 2026-09-21 — M1.1 — Fenêtre SDL3, boucle et événements (#10)

- **Temps Donnovan : 0,63 h** (20 min déclarées, 0,63 h après réconciliation avec le total de la journée, voir
  la clôture ; estimé 0,75 h)
- Sessions Claude Code : 1
- Fait : module `engine/platform` sur SDL3 3.4.12 (vcpkg, fonctionnalités `x11` et `wayland` seulement, sans
  ibus ni dbus) ; événements traduits vers nos types (`CloseRequested`, `Resized` en pixels, `Hidden`,
  `Shown`) ; boucle du sandbox qui dort quand la fenêtre est masquée ; script `tools/kwin-window-smoke.sh`
  qui pilote la vraie fenêtre à travers KWin ; sandbox lancé 3 s en CI, arrêté par SIGTERM.
- Mesures :
  - redimensionnement puis minimisation et restauration sous X11 : aucun plantage, tailles reçues
    640 × 332 et 1600 × 872 px pour des cadres de 640 × 360 et 1600 × 900 — la barre de titre prend 28 px
    (`tools/kwin-window-smoke.sh`) ;
  - temps CPU : **2 010 ms en 2 s visible, 0 ms minimisée** (même script) ;
  - arrêt propre sur SIGTERM, code 0, en Debug et en Release
    (`SDL_VIDEO_DRIVER=offscreen timeout --foreground --preserve-status -k 10 3 ./build/<preset>/sandbox/levain_sandbox`) ;
  - aucun en-tête SDL hors de `engine/platform/src/` (`grep -rn "include.*SDL" engine sandbox tests`) ;
  - SDL3 compilé par vcpkg en 22 s à froid (`time cmake --preset linux-debug`).
- Écarts et problèmes :
  - **Sous Wayland, la fenêtre n'apparaît pas.** Une surface Wayland n'est affichée qu'après son premier
    buffer, et le moteur ne présente encore rien : KWin ne la connaît pas, alors qu'elle s'affiche sous X11.
    M1.1 se teste donc sous XWayland (`SDL_VIDEO_DRIVER=x11`), et la minimisation sous Wayland est reportée à
    M1.2, ajoutée à l'issue #13.
  - **Assertion de SDL sur un double signal** (`SDL_quit.c:171`) : `timeout` envoie SIGTERM à l'enfant puis
    au groupe de processus ; le second signal tombe entre la remise à zéro d'un drapeau et l'assertion qui la
    vérifie. Le SDL de Debug a alors ouvert une boîte de dialogue zenity sur le bureau, et le programme a
    attendu jusqu'au SIGKILL. Parade : `timeout --foreground`, un seul signal.
  - Un processus lancé avec `&` depuis un shell non interactif hérite d'un SIGINT **ignoré**, et SDL respecte
    ce choix : c'est pourquoi la CI et le script arrêtent le sandbox par SIGTERM.
  - **CI rouge deux fois sur SDL3** : la liste de paquets `-dev` suggérée par le port vcpkg ne suffit pas.
    SDL refuse de se configurer s'il manque une extension X11 demandée, d'abord Xcursor, puis XTest. La liste
    vient maintenant des contrôles stricts de `cmake/sdlchecks.cmake` (huit extensions) et de la section
    Ubuntu du `README-linux` de SDL. Bon point : SDL échoue bruyamment au lieu de désactiver la fonction.
  - **PR découpée** : la version complète de M1.1 faisait environ 710 lignes, près du double de la règle
    n°2. Le frame time et les sanitizers (#11) suivent dans une seconde PR, déjà prête.
  - Tracy (#38) : vcpkg `master` est toujours en 0.13.1 au 21/09.
- Prochaine étape : PR de l'issue #11 — frame time dans le titre, ASan et UBSan en CI.

## 2026-09-20 — M0.3 et phase 0 — Clôture

- **Temps Donnovan : 5,0 h** (12h00–14h30 et 21h30–23h55, soit ~4,9 h, arrondi au quart d'heure sur le board)
- Définition de « terminé » (SPECS §9) : 5 critères sur 7 remplis — démo lançable sous Linux, mesures
  consignées, CI verte, README du module à jour, **étude E0 écrite**. Deux sans objet : pas de démo Windows
  (hors périmètre, ADR-0011), pas de binaire à publier.

### L'erreur de mesure, corrigée par Donnovan

**Je mesurais la mauvaise chose depuis le début du projet.** Je demandais « combien de temps sur la
**relecture** » et n'enregistrais que ça, alors que les « Heures Donnovan » de la ROADMAP comptent **tout**
l'engagement : pilotage, questions, décisions, relecture.

| | Estimé | Passé | Ratio | Conséquence |
|---|---:|---:|---:|---|
| Mesure erronée (relectures seules) | 6,0 h | 3,0 h | **0,50** | recalibrage déclenché, −30 % sur 63 h |
| **Mesure réelle** (Donnovan, 20/09) | 6,0 h | **5,0 h** | **0,83** | **dans la fourchette, rien à changer** |

Le ratio erroné aurait amputé la roadmap d'environ 19 h sans aucune raison. CLAUDE.md est corrigé : la question
de fin de session porte désormais sur le **temps total**, avec la raison écrite pour qu'elle ne redérive pas.

### Phase 0 — bilan

| Milestone | Estimé | Passé |
|---|---:|---:|
| M0.1 Dépôt, suivi et specs | 1,0 h | 1,25 h |
| M0.2 Squelette de build et CI | 1,5 h | 1,50 h |
| M0.4 Socle Rust | 1,0 h | 0,25 h |
| M0.5 Retour au C++ | 1,0 h | 0,75 h |
| M0.3 Core minimal | 1,5 h | 1,25 h |
| **Phase 0** | **6,0 h** | **5,0 h** — ratio **0,83** |

**Aucun recalibrage** : le ratio est dans la fourchette 0,8–1,25 de la ROADMAP. À réexaminer à la clôture de la
phase 1, premier échantillon de vrai code de rendu — la phase 0 était faite de specs, d'ADR et de
configuration, et ne prédit pas grand-chose.

### Trois pannes silencieuses, un même motif

La journée en a produit trois, toutes « vertes » pendant qu'elles ne faisaient rien :

1. **L'épinglage de LLVM 22 en CI** — inopérant depuis M0.2, trois milestones sur clang 18. Le workflow
   imprimait la version à chaque run ; personne ne l'a lue.
2. **La baseline vcpkg** — contournée en local par le `spdlog` d'Arch, sans un mot. Découverte parce que Tracy
   n'existe pas en paquet système.
3. **Tracy sans `TRACY_NO_EXIT`** — le programme se termine avant qu'un profileur ait pu se connecter, sans
   avertissement.

Règle inscrite : **un contrôle doit échouer bruyamment quand sa condition n'est pas réunie, jamais se contenter
de ne pas s'exécuter.** Deux garde-fous ajoutés en conséquence (vérification de la chaîne LLVM, refus de
configurer sans toolchain vcpkg).

### Numérotation des ADR

La ROADMAP pré-attribuait cinq numéros d'ADR ; l'aller-retour Rust en a consommé deux (0010, 0011) et toute la
suite avait glissé — M2.1 renvoyait à « ADR-0009 », devenu le style C++. **Les numéros pré-attribués sont
retirés** : un ADR prend son numéro au moment où on l'écrit. Règle ajoutée à la ROADMAP.

### Phase 2 détaillée

Le rituel de clôture de phase demande de détailler la phase N+2. **8 issues créées** (#40 à #47) pour 5,5 h,
conformes au découpage de la ROADMAP : stratégie de binding, caméra et meshes, instancing et timestamps,
textures et mipmaps, samplers, hot-reload Slang, repli sur erreur, étude E2.

- Prochaine étape : **phase 1**, M1.1 — fenêtre SDL3, boucle principale, frame time, sanitizers. Voir aussi
  l'issue #38 (capture Tracy reportée) et #32 déjà close.

## 2026-09-20 — M0.3 — Allocateurs, Tracy et étude E0 (issues #7, #8, #9)

- Temps Donnovan : à renseigner (relecture estimée 0,3 h)
- Sessions Claude Code : 1
- Fait : `LinearAllocator` et `PoolAllocator` avec 11 tests, benchmark, macros de profilage Tracy,
  instrumentation du sandbox, étude **E0 — Comment démarre un moteur**.
- **Benchmark des allocateurs** (Release, machine de référence, médiane de 5 exécutions de 100 000 allocations
  de 64 octets) :

  | Allocateur | ns / allocation | Rapport à `malloc` |
  |---|---:|---:|
  | `malloc` + `free` | 14,50 | 1,0× |
  | `LinearAllocator` | **1,32** | **11,0×** |
  | `PoolAllocator` (alloc + free) | **2,38** | **6,1×** |

  Commande : `cmake --preset linux-release && cmake --build --preset linux-release &&
  ./build/linux-release/tests/levain_bench`

- **Tracy, coût nul quand désactivé — vérifié, pas affirmé** :

  | | Symboles Tracy | Bibliothèques liées | Taille du sandbox |
  |---|---:|---:|---:|
  | Désactivé (défaut) | **0** | **0** | 7 607 232 o |
  | `-DLEVAIN_PROFILING=ON` | 1 025 | 1 | 9 422 024 o |

  Commandes : `nm -C <binaire> \| grep -ci tracy`, `ldd <binaire> \| grep -ci tracy`.

- **Un test a trouvé un vrai bug dans mon allocateur.** `LinearAllocator::allocate` alignait l'**offset** dans
  le tampon et non l'**adresse réelle** ; `make_unique<std::byte[]>` ne garantit que l'alignement par défaut
  (16 octets), donc toute demande supérieure rendait un pointeur mal aligné — silencieusement, puisque ça
  « marche » sur x86. Corrigé en alignant l'adresse. C'est exactement ce que le critère « tests d'alignement »
  de l'issue devait attraper.
- **clang-tidy a trouvé quatre défauts de plus** : deux conversions implicites `void**` → `void*` dans les
  `memcpy` de la liste des libres, une multiplication en `int` élargie en `size_t`, et une exception pouvant
  s'échapper du `main` du benchmark. Tous corrigés, aucun désactivé. Troisième passage de l'outil sur du code
  neuf, troisième récolte.
- **Défaut de ma démo, trouvé par Donnovan en la lançant** : 120 frames à 200 µs font ~24 ms, impossible d'y
  connecter un profileur à la main. La parade est `TRACY_NO_EXIT=1`, qui fait attendre le client jusqu'à ce que
  le profileur se connecte et ait tout reçu. Documenté dans CLAUDE.md. Sans elle, le programme se termine sans
  le moindre avertissement — encore une panne silencieuse.
- Écarts et problèmes : **le critère « capture d'écran Tracy » de l'issue #8 n'est pas rempli, et il est
  reporté à M1.1** (issue #38) plutôt que maquillé. Trois raisons cumulées :
  1. **Versions incompatibles.** Le client vient de vcpkg en **0.13.1** ; les binaires Linux du profileur ne
     commencent qu'à **0.14.0**, et Tracy refuse une connexion dont le protocole ne correspond pas. vcpkg ne
     connaît aucune version ≥ 0.14 (`versions/t-/tracy.json`), donc pas d'`override` possible. Compiler le
     profileur 0.13.1 par `tracy[gui-tools]` reste faisable, mais c'est une interface graphique complète à
     construire depuis les sources.
  2. Aucun profileur installé sur la machine, et pas de paquet Arch.
  3. **La capture n'aurait rien montré d'utile** : boucle factice de 120 frames dont l'essentiel est un `sleep`.
     À M1.1 il y aura une vraie boucle, et une capture dira enfin où part la frame.

  Ce qui est vérifié aujourd'hui : les symboles `__tracy_source_location` sont présents dans le binaire
  instrumenté, et le coût nul quand Tracy est désactivé est mesuré.
- Le sandbox a maintenant une **boucle simulée de 120 frames** : il fallait quelque chose à découper pour que
  `LEVAIN_PROFILE_FRAME` ait un sens. La vraie boucle arrive en M1.1.
- **Deuxième contrôle silencieusement inopérant de la session**, trouvé par Donnovan en lançant la commande
  Tracy que je lui avais donnée. `VCPKG_ROOT` n'était pas exportée dans son shell, donc la toolchain vcpkg
  n'était pas chargée — et **CMake a trouvé le `spdlog` d'Arch dans `/usr/lib/cmake/spdlog` et continué sans
  rien dire**, contournant la baseline figée de l'ADR-0007. Le build n'a échoué que sur Tracy, qui n'existe pas
  en paquet système. Une dépendance de moins et personne ne s'apercevait de rien.
  Garde-fou posé : le `CMakeLists.txt` racine refuse de se configurer si `VCPKG_TOOLCHAIN` n'est pas défini,
  avec un message qui dit quoi faire. Vérifié dans les deux sens.
- **Le motif de la session** : deux vérifications ont passé pendant des semaines en ne faisant rien — l'épinglage
  de LLVM en CI, et la baseline vcpkg en local. Les deux étaient « vertes ». À retenir : **un contrôle doit
  échouer bruyamment quand sa condition n'est pas réunie, jamais se contenter de ne pas s'exécuter.**
- Prochaine étape : clôture de M0.3 et de la phase 0 — ratio, recalibrage de la roadmap, et détail des issues
  de la phase 2.

## 2026-09-20 — M0.3 — Logs, assertions et gestion d'erreurs (issue #6)

- Temps Donnovan : à renseigner (relecture estimée 0,25 h)
- Sessions Claude Code : 1
- Fait : `log.hpp` (catégories et niveaux, spdlog), `assert.hpp` (`LEVAIN_ASSERT`, `LEVAIN_VERIFY`),
  `error.hpp` (`Result<T>` = `std::expected<T, Error>`), **ADR-0008**, 7 nouveaux tests, README du module
  `core` mis à jour.
- **ADR-0008 : le débat « exceptions ou codes de retour » est mal posé.** Il y a deux sortes d'échecs et elles
  n'appellent pas la même réponse : un **bug du moteur** s'arrête au plus près de la faute (`LEVAIN_ASSERT`),
  un **échec de l'environnement** se renvoie (`Result<T>`). C'est le cœur de l'ADR.
- Décision inattendue : **`-fno-exceptions` n'est pas activé**, alors que c'est la pratique courante des
  moteurs. Raison trouvée dans la doc Godot : avec les exceptions désactivées, le `throw` de libstdc++ se
  replie sur `__builtin_trap()` — arrêt brutal sans message. Unreal et Godot peuvent se le permettre parce
  qu'ils ont remplacé la STL par leurs propres conteneurs ; nous l'utilisons pleinement.
- Mesures :

  | Critère de l'issue | Résultat | Commande |
  |---|---|---|
  | Une assertion affiche fichier, ligne et message, puis s'arrête dans le débogueur | **oui**, et code de sortie **133** (SIGTRAP) | programme de démonstration lié à `levain_core` |
  | Tests | **9 verts en Debug et en Release** | `ctest --test-dir build/linux-{debug,release}` |
  | clang-tidy | **0 finding** | `clang-tidy -p build/linux-debug --warnings-as-errors='*'` |

  Sortie de l'assertion violée :
  ```
  [critical] [assert] assertion violée : frameCount >= 0
    message  : le compteur de frames ne peut pas être négatif
    assert_demo.cpp:5 (int main())
  ```

- **clang-tidy a encore trouvé deux vrais points** dès la première exécution sur ce code : `ErrorCode` et
  `LogLevel` utilisaient `int` comme type sous-jacent là où `std::uint8_t` suffit (`performance-enum-size`).
  Corrigé, pas désactivé. C'est la deuxième fois que l'outil paye dès son premier passage sur du code neuf.
- Conformité à l'ADR-0011 (forme du code) : tout est en **fonctions libres avec les dépendances dans la
  signature**, et **chaque piège porte son nom** — `isLogEnabled` avant le formatage pour qu'un `Trace` dans
  une boucle de rendu ne paie pas son `std::format`, `LEVAIN_VERIFY` pour l'expression à effet qui ne doit pas
  disparaître en Release. Les trois règles ont tenu sur du vrai code.
- Écarts et problèmes : `isLogEnabled` fait une **recherche par chaîne à chaque appel**. Noté dans le code :
  si une capture Tracy (issue #8) montre le log dans le profil, la réponse sera un handle de catégorie obtenu
  une fois, pas une optimisation de la table.
- Prochaine étape : issues #7 (allocateurs et benchmarks), #8 (Tracy) et #9 (étude E0), dans une seconde PR.

## 2026-09-20 — M0.5 — Clôture du milestone

- Temps Donnovan : inclus dans les 0,25 h de la relecture de la PR #33
- **Cumul de la phase 0 à ce stade** : **2,75 h passées pour 3,75 h estimées**, soit un **ratio de 0,73** sur
  quatre milestones terminés (M0.1, M0.2, M0.4, M0.5). Reste M0.3, estimé 1,5 h.
  **L'aller-retour par Rust aura coûté 0,5 h de Donnovan au total** — deux milestones, quatre relectures d'un
  quart d'heure. C'est le prix d'avoir tranché la question du langage définitivement, à 107 lignes de code.
- Définition de « terminé » (SPECS §9) : 4 critères sur 7 s'appliquent et sont remplis (démo lançable sous
  Linux, mesures consignées, CI verte, docs à jour). Trois restent sans objet à ce stade : pas de démo Windows
  (hors périmètre, ADR-0011), pas d'étude de phase due en M0.5, pas de binaire à publier.
- Fait : ADR-0011 accepté, tag `m0.5`, release avec les mesures, milestone fermé. Issue #32 fermée — elle
  demandait de réécrire les issues de phase 1 pour la pile Rust, sans objet depuis le retour au C++.
- **Question archivée en Q&R** : « Clang existe aussi sous Windows, pourquoi MSVC ? » Réponse contre-intuitive —
  clang-cl ne règle **aucun** des deux bugs de M0.2 (il reproduit volontairement le piège `__cplusplus` et
  consomme la STL de Microsoft), mais il donnerait **exactement C++23 sur les deux plateformes** au lieu du
  sur-ensemble `/std:c++latest`. Noté dans l'ADR-0011 comme première option à évaluer quand Windows reviendra.
- Observation sur la journée : trois décisions prises par défaut ont été attrapées par Donnovan et transformées
  en choix argumentés — la forme du code (variante C), le périmètre Windows, et le compilateur Windows. Aucune
  n'était signalée comme incertaine dans les ADR d'origine. **À faire systématiquement : marquer dans un ADR ce
  qui est un choix raisonné et ce qui est une convention reprise sans examen.**
- Prochaine étape : M0.3 — logs, assertions, ADR-0008 (gestion d'erreurs), allocateurs, Tracy, étude E0.

## 2026-09-20 — M0.5 — Retour au C++ (ADR-0011)

- Temps Donnovan : **0,25 h** pour 1,0 h estimée (ratio 0,25) — relecture de la PR #33. Issue #35 créée
  rétroactivement pour que le board porte ce temps : le travail avait été décidé en conversation, sans issue.
- Sessions Claude Code : 1
- Contexte : Donnovan revient sur la décision Rust. Sa thèse : ce qu'il trouvait plus lisible venait de la
  **simplicité du langage**, et du C++ écrit en exploitant ses atouts devrait se lire aussi bien. Il précise
  aussi que **le projet est sous Linux** et que Windows n'est pas un sujet pour l'instant.
- **Deux biais reconnus dans l'ADR-0010**, et c'est ce qui rend le revirement fondé :
  1. **La manche 1 comparait flecs à bevy_ecs, pas C++ à Rust.** La propriété que Donnovan a aimée — les
     dépendances dans la signature — appartient à bevy_ecs, pas à Rust. Mon C++ était handicapé par la glu
     flecs (lambda `[](flecs::iter&, size_t, …)`, `it.world().get<>()` caché dans le corps). Une fonction libre
     a exactement la même propriété. **Je n'ai pas écrit le meilleur C++ possible.**
  2. **La manche 3 facturait au C++ un Windows dont le projet n'a pas besoin.** Et surtout : **les deux seuls
     bugs de M0.2 étaient des bugs Windows** (`/Zc:__cplusplus`, `<ostream>` non inclus en cascade par la STL
     de Microsoft). En périmètre Linux, ni l'un ni l'autre n'existe.
- Décisions (ADR-0011, remplace ADR-0010) : retour au C++23 ; **Linux d'abord**, presets et CI Windows retirés,
  M1.4 différé (et non plus supprimé) ; **forme du code fixée** — variante C choisie par Donnovan sur lecture
  de trois variantes : fonctions libres avec toutes les dépendances dans la signature, glu ECS confinée à une
  ligne, chaque piège portant son nom (`normalizeOrZero`, `clampPitch`, `horizontalBasisFrom`).
- Méthode : le revert n'a **pas** annulé `docs/JOURNAL.md` ni l'ADR-0010, restaurés depuis `main`. Un journal
  et une décision sont de l'historique, ils ne se revertent pas.
- Mesures :

  | Critère | Résultat | Commande |
  |---|---|---|
  | Build et sandbox | `Levain 0.1.0 — clang 22.1.8 — __cplusplus 202302` | `cmake --build --preset linux-debug` |
  | Tests | **2 verts** | `ctest --test-dir build/linux-debug` |
  | Format | conforme | `clang-format --dry-run --Werror` |
  | clang-tidy | **0 finding** | `clang-tidy -p build/linux-debug --warnings-as-errors='*'` |
  | Infrastructure | **347 lignes** (contre 435 en v0.3) | `wc -l` sur les 9 fichiers |

- **Correction d'une estimation que j'avais donnée pour une mesure** : j'annonçais « environ 300 lignes » avant
  d'avoir mesuré ; le vrai chiffre est **347**. La moitié des 435 lignes de la v0.3 était du format et du lint,
  indépendants de la plateforme. Ce que le périmètre Linux fait disparaître n'est pas du volume mais **la part
  qui causait les pannes**.
- Écarts et problèmes : les jobs Windows n'existent plus, donc les checks requis par la protection de `main`
  (`linux-debug`, `linux-release`) restent valides sans modification — les noms de presets n'ont pas changé.
  L'issue #32 (réécrire les issues de phase 1) devient sans objet et sera refermée.
- Bilan des deux allers-retours : **le détour par Rust a produit quelque chose**. Sans la comparaison, la
  variante C n'aurait jamais été écrite, et l'ADR-0009 seul n'avait pas suffi à la produire. Règle retenue pour
  la suite : **avant de conclure qu'une alternative est meilleure, vérifier qu'on a écrit la meilleure version
  de ce qu'on compare.**
- Prochaine étape : validation de l'ADR-0011, clôture de M0.5, puis M0.3 — logs, assertions, ADR-0008,
  allocateurs, Tracy, étude E0.

## 2026-09-20 — M0.4 — Socle Rust (issues #27 à #30)

- Temps Donnovan : **0,25 h** pour 1,0 h estimée (ratio 0,25) — relecture de la PR #31. Les quatre issues ayant
  été livrées en une seule PR, le temps est porté sur l'issue #27 au board plutôt que réparti.
- Sessions Claude Code : 1
- Fait : migration complète vers Rust. Workspace cargo, crates `levain-core` et `levain-sandbox`, CI réécrite,
  documentation répercutée, C++ supprimé de l'arbre de travail.
- Mesures :

  | Critère | Résultat | Commande |
  |---|---|---|
  | Le sandbox tourne | `Levain 0.1.0 — linux/x86_64 — rust edition 2024` | `cargo run -p levain-sandbox` |
  | Tests | **2 tests verts** | `cargo test --workspace` |
  | Un code mal formaté fait échouer la CI | code de sortie **1** | `cargo fmt --all --check` |
  | Un défaut clippy fait échouer la CI | **erreur** sur une fonction jamais utilisée | `cargo clippy --workspace --all-targets -- -D warnings` |
  | Infrastructure de build et CI | **136 lignes** contre 435 en C++ | `wc -l` sur Cargo.toml ×3 et ci.yml |

- Durées de CI, premier run, **les 4 jobs verts du premier coup** :

  | Job | Rust (à froid) | C++ à froid | C++ à chaud |
  |---|---:|---:|---:|
  | `linux-debug` | **16 s** | 85 s | 24 s |
  | `linux-release` | **8 s** | 125 s | 24 s |
  | `windows-debug` | **38 s** | 233 s | 52 s |
  | `windows-release` | **34 s** | 288 s | 91 s |
  | **Total** | **96 s** | 731 s | 191 s |

  **Réserve importante : ce n'est pas une comparaison équitable.** Le workspace Rust n'a aujourd'hui
  **aucune dépendance externe**, là où le build C++ compilait nvrhi, flecs et vulkan-headers. L'écart mesuré
  reflète surtout ça. La comparaison honnête viendra quand wgpu et bevy_ecs seront réellement ajoutés (M1.2 et
  M3.1) — et wgpu est un gros crate. Ce que ces chiffres établissent vraiment, c'est que **le chemin Windows a
  fonctionné du premier coup**, sans `vcvars`, sans `vswhere` et sans divergence de bibliothèque standard, là
  où M0.2 avait demandé deux correctifs.

- Outillage : `rustup` installé en mode utilisateur dans `~/.cargo` (pas de sudo), Rust **1.98.1**, avec
  rustfmt et clippy.
- Décisions prises en chemin :
  - **Noms de jobs de CI conservés à l'identique** (`linux-debug`, `linux-release`, `windows-debug`,
    `windows-release`) alors que le profil cargo s'appelle `dev`. Les renommer aurait rendu `main`
    infusionnable : ce sont les checks requis par la protection de branche. Le piège était noté dans l'issue #28,
    et le contourner coûte moins cher que de reconfigurer la protection.
  - **`clippy::all` seulement, pas `pedantic`.** Même raisonnement que pour clang-tidy en M0.2 : on active ce
    qui attrape de vrais défauts. `pedantic` pousse vers `must_use` partout, ce que l'ADR-0009 a écarté sous le
    nom de « décoration maximale ».
  - **Le test de version du C++ n'a pas été porté tel quel.** Il vérifiait que CMake injectait correctement
    `LEVAIN_VERSION` — un câblage qui n'existe plus, `env!("CARGO_PKG_VERSION")` étant automatique. Remplacé par
    un test qui vérifie que la version est un semver à trois composants, ce qui attrape un `Cargo.toml` malformé.
  - **Pas de dossier `tests/` à la racine** : les tests unitaires vivent dans le crate (`#[cfg(test)] mod tests`).
    SPECS §7 mis à jour en conséquence.
- **Erreur corrigée dans l'ADR-0010** : l'en-tête ne listait pas l'ADR-0003 (SDL3) parmi les ADR remplacés, alors
  que la décision remplace bien SDL3 par winit. Relevé en appliquant l'ADR. L'ADR-0003 est passé à « remplacé ».
- Écarts et problèmes : **les issues de la phase 1 nomment encore NVRHI, SDL3 et Slang** (#10, #12 à #17). Elles
  ne sont pas réécrites ici — le périmètre de l'issue #30 s'arrêtait à SPECS, ROADMAP, CLAUDE.md et aux statuts
  d'ADR — mais elles devront l'être **avant d'attaquer M1.1**.
- Roadmap : **M1.4 supprimé**, **M0.4 ajouté**, total inchangé à 68 h. Phase 0 passe de 4,0 à 5,0 h, phase 1 de
  5,5 à 4,5 h.
- Prochaine étape : clôture de M0.4, puis M0.3 — logs, assertions, ADR-0008, allocateurs, Tracy, étude E0.

## 2026-09-20 — M0.2 — Réévaluation du langage : passage à Rust (ADR-0010)

- Temps Donnovan : à renseigner (trois manches de lecture de code + relecture de l'ADR, estimée 0,3 h)
- Sessions Claude Code : 1
- Contexte : Donnovan demande à la clôture de M0.2 pourquoi ne pas passer à Rust, puisqu'il n'écrit pas le code
  et qu'il fait du C++ toute la semaine. L'ADR-0001 prévoyait explicitement cette réévaluation.
- Méthode : trois manches de comparaison sur du code réel plutôt qu'une discussion de principes, comme pour
  l'ADR-0009.
  1. **Transform + déplacement FPS** → Donnovan trouve le **Rust plus lisible sans en avoir jamais lu**, et le
     C++ plus verbeux alors qu'il en fait tous les jours.
  2. **Propagation hiérarchique des transforms** → manche **favorable au C++** : le `cascade()` de flecs tient
     en 10 lignes, la version Rust sûre en demande 20. Bevy utilise `unsafe` dans la sienne (bevy#4697).
  3. **Build et CI** → mesuré sur le dépôt réel : **435 lignes d'infrastructure C++ contre 94 en Rust**, pour
     107 lignes de moteur. Commande : `wc -l` sur ci.yml, CMakePresets.json, les CMakeLists, vcpkg.json,
     .clang-format et .clang-tidy, contre l'équivalent cargo écrit et compté.
- Décision proposée : **ADR-0010**, passage à Rust edition 2024. wgpu, bevy_ecs, winit, glam, rapier3d, gltf,
  WGSL, egui, tracing, tracy-client, kira. Remplace les ADR 0001, 0002, 0004, 0005, 0007 et la moitié nommage
  de l'ADR-0009.
- `bevy_ecs` et non `flecs_ecs` : le binding Rust de flecs existe et couvre les hiérarchies, mais il est
  auto-déclaré **alpha**, maintenu par une personne, à 11 359 téléchargements, et son `World` est
  `!Send`/`!Sync`. On ne pose pas le cœur du moteur dessus.
- Conséquence sur la roadmap : **M1.4 (backend Direct3D 12) disparaît** — wgpu choisit son backend seul.
  1,0 h de budget et une session rendues. Nouveau milestone **M0.4 — Socle Rust** pour refaire l'équivalent
  de M0.2.
- Correction d'une erreur de l'ADR-0001 : « le borrow checker résiste aux graphes d'objets » était vrai en
  général et hors sujet ici, la réponse Rust à ce problème étant l'ECS, qu'on avait déjà choisi.
- Écarts et problèmes : on perd le parcours de lecture Donut, sur lequel l'ADR-0002 et CLAUDE.md étaient
  bâtis, et l'objectif « comprendre Unreal, Unity, Godot » se paie plus cher puisqu'ils sont tous en C++.
  Assumé et écrit dans l'ADR.
- Prochaine étape : validation de l'ADR-0010 par Donnovan. **Rien n'est migré tant qu'il n'est pas accepté.**
  Ensuite : ROADMAP et SPECS mis à jour, milestone M0.4 créé, puis socle Rust.

## 2026-09-20 — M0.2 — clang-tidy, doctest et protection de main (issue #5)

- Temps Donnovan : à renseigner (relecture estimée 0,3 h)
- Sessions Claude Code : 1
- Fait : `.clang-tidy` (nommage de l'ADR-0009 + bugprone, performance, quelques modernize), doctest par vcpkg
  avec 2 cas découverts individuellement, `ctest` branché, contrôle de format et analyse statique en CI,
  protection de `main`.
- Mesures — les trois critères de l'issue :

  | Critère | Résultat | Commande |
  |---|---|---|
  | Un code mal formaté fait échouer la CI | code de sortie **123** sur format invalide, **0** sinon | `find … \| xargs clang-format --dry-run --Werror` |
  | `ctest` lance au moins un test | **2 tests**, verts | `ctest --test-dir build/linux-debug` |
  | Push direct sur `main` refusé | **refusé** (voir plus bas) | `git push origin main` |

  Le test sait échouer : `LEVAIN_EXPECTED_VERSION` forcé à `9.9.9` → `50% tests passed, 1 tests failed`.

- Durées de CI avec LLVM 22 et les tests : `linux-debug` 69 s, `linux-release` 56 s, `windows-debug` 289 s,
  `windows-release` 278 s. L'installation de LLVM 22 coûte une trentaine de secondes aux jobs Linux par rapport
  au run précédent (24 s), pour la garantie que clang-format et clang-tidy sont les mêmes qu'en local.

- **Deux vraies trouvailles, aucune stylistique.**

  1. **clang-tidy, première exécution** : `std::print` peut lever et `main` laissait l'exception s'échapper, ce
     qui appelle `std::terminate`. Corrigé dans `main`, pas désactivé (règle n°4). La politique générale reste
     l'affaire de l'ADR-0008 en M0.3.
  2. **CI Windows** : `version_test.cpp` ne compilait pas sous MSVC alors que Linux était vert. Pour afficher la
     valeur d'un `CHECK` qui échoue, doctest instancie `operator<<` vers un `ostream` ; la STL de Microsoft
     déclare cet opérateur pour `std::string_view` **sans inclure `<ostream>` en cascade**, là où libstdc++ le
     fait. Réglé par un `#include <ostream>`. **Première divergence de plateforme du projet**, sur 20 lignes de
     test, et détectable uniquement par le job Windows — celui qu'on avait gardé non bloquant « au cas où ».

- Correction d'une erreur d'analyse de ma part : j'avais justifié Clang 18 en CI par « la CI est plus
  conservatrice ». C'est faux — Clang 18 n'est pas plus strict, il est moins complet, et surtout clang-format 18
  et 22 ne produisent pas la même sortie. Un contrôle de format en 18 aurait rejeté des fichiers corrects
  formatés en 22. LLVM 22 installé en CI via `apt.llvm.org`, `LLVM_VERSION` dans le workflow à garder égal à la
  machine de référence (SPECS §10).

- Protection de `main` : PR obligatoire, `linux-debug` et `linux-release` requis, force-push et suppression
  interdits, **`enforce_admins` activé**. Les jobs Windows sont volontairement **hors des checks requis** :
  ils sont `continue-on-error`, les inscrire annulerait ce compromis. À ajouter le jour où ils deviendront
  bloquants.

- Écarts et problèmes : `enforce_admins: true` s'applique aussi à Donnovan. Pour lever la protection en cas de
  besoin : `gh api -X DELETE repos/PhantomDO/Levain/branches/main/protection`. Conséquence pour les sessions
  suivantes : **plus aucun push direct sur `main`**, y compris pour une entrée de journal.

- `bugprone-easily-swappable-parameters` est le seul check écarté : il signalerait
  `allocate(size_t size, size_t alignment)` et à peu près toute l'API d'un moteur.

- Prochaine étape : clôture de M0.2, puis M0.3 — logs, assertions, ADR-0008, allocateurs, Tracy, étude E0.

## 2026-09-20 — M0.2 — CI Windows + Linux (issue #4)

- Temps Donnovan : à renseigner (relecture estimée 0,25 h)
- Sessions Claude Code : 1
- Fait : `.github/workflows/ci.yml` — matrice de 4 jobs (Debug et Release × `ubuntu-latest` et
  `windows-latest`), Ninja partout, vcpkg cloné au tag `2026.07.29` et cache binaire via `actions/cache`.
  Jobs Windows en `continue-on-error`. **Les 4 jobs sont verts au premier run.**
- Mesures — durées de CI, à froid puis à chaud (cache vcpkg peuplé) :

  | Job | À froid | À chaud | Gain |
  |---|---:|---:|---:|
  | `linux-debug` | 85 s | **24 s** | −72 % |
  | `linux-release` | 125 s | **24 s** | −81 % |
  | `windows-debug` | 233 s | **52 s** | −78 % |
  | `windows-release` | 288 s | **91 s** | −68 % |
  | **Total** | **731 s** | **191 s** | **−74 %** |

  Commande : `gh api repos/PhantomDO/Levain/actions/runs/<id>/jobs`, différence entre `started_at` et
  `completed_at`. Runs `35518846705` (froid) et `35519196360` (chaud).

  Windows coûte 2 à 3 fois plus cher que Linux, à froid comme à chaud.

- **Confirmation empirique de l'ADR-0001.** Le même sandbox, mêmes sources, annonce :
  `Linux / Clang 18.1.3 / -std=c++23 → __cplusplus 202302` et
  `Windows / MSVC 19.51.36256 / /std:c++latest → __cplusplus 202400`.
  `202400` n'est la valeur d'aucune norme publiée : c'est un mode brouillon post-C++23. Le raisonnement de
  l'ADR-0001 est donc vérifié par la mesure, et le `-pedantic-errors` côté Linux n'est pas une précaution
  théorique.
- Écarts et problèmes :
  - **Erreur de ma part** : le premier run annonçait `__cplusplus 199711` sous Windows. MSVC épingle cette macro
    à la valeur de C++98 sauf si on passe `/Zc:__cplusplus`. La doc Microsoft que j'avais lue en écrivant
    l'ADR-0001 le dit explicitement ; je ne l'avais pas appliqué. Corrigé (`19b5fbc`), avec le constat écrit
    dans le commentaire du `CMakeLists.txt`.
  - **La CI Linux tourne sur Clang 18.1.3, la machine de référence sur Clang 22.1.8** — quatre versions
    majeures d'écart. L'écart va dans le sens le moins dangereux (la CI est plus conservatrice que la machine
    de dev), mais du code qui compile localement peut casser en CI. Laissé tel quel faute de cas concret ;
    à rouvrir si ça mord. Alternative : installer Clang 22 via `apt.llvm.org`, ~20 s par job.
  - **Pas de `ctest`** : aucun test n'existe encore. Test de fumée (lancement du sandbox) en attendant
    doctest, issue #5.
  - **`VCPKG_TAG` et `builtin-baseline` ne sont pas couplés automatiquement** : deux valeurs écrites à la main
    dans deux fichiers. Cohérentes aujourd'hui, vérifiées manuellement.
- Prochaine étape : issue #5 — clang-tidy (nommage de l'ADR-0009), doctest et `ctest`, vérification du format
  en CI, protection de `main`.

## 2026-09-20 — M0.2 — Norme de style C++ (ADR-0009)

- Temps Donnovan : 0,25 h de relecture de la PR #21 (portée au board), plus le choix de style
- Sessions Claude Code : 1
- Contexte : Donnovan demande une norme d'écriture fixée pour tout le projet. Le C++ propose plusieurs
  conventions incompatibles et il relit tout avec 1 à 2 h par semaine : sans règle, chaque fichier dérive et la
  relecture coûte de l'attention pour rien. Le dépôt comptait 27 lignes de code — aucune conversion à faire.
- Méthode : trois variantes complètes du même allocateur linéaire (le vrai, issue #7) soumises en lecture
  plutôt qu'en discussion. Choix fait sur le code, pas sur des principes.
- Décisions (ADR-0009) : **variante C** — types `PascalCase`, fonctions et variables `camelCase`, membres
  `m_` ; Allman, 4 espaces, 100 colonnes ; décoration **modérée** (`[[nodiscard]]` seulement quand ignorer le
  retour est un bug, `noexcept` seulement quand c'est garanti) ; commentaires **en français**.
- SPECS §8 amendé : les commentaires de code passent officiellement en français. J'étais déjà en infraction
  dans la PR #21 sans l'avoir signalé — corrigé.
- Mesures : `.clang-format` appliqué aux 3 fichiers existants, build et exécution vérifiés après reformatage
  (`clang-format -i` puis `cmake --build --preset linux-debug`), puis `clang-format --dry-run --Werror` passe
  sur les 3 fichiers.
- Écarts et problèmes : clang-format **ne vérifie pas le nommage** — c'est clang-tidy
  (`readability-identifier-naming`) qui s'en chargera dans l'issue #5. Tant que ce n'est pas en place, la moitié
  nommage de l'ADR-0009 repose sur ma discipline, pas sur l'outil.
- Décision de périmètre : la CI Windows de l'issue #4 sera **non bloquante** au début. Donnovan n'a pas de
  machine Windows ; les runners GitHub n'en demandent pas non plus, et c'est le seul endroit qui vérifiera le
  pari `/std:c++latest` de l'ADR-0001. Job gardé, `continue-on-error`, rendu bloquant quand une machine sera
  disponible.
- Prochaine étape : issue #4 (CI), puis #5 (clang-tidy, doctest, protection de `main`).

## 2026-09-20 — M0.2 — Squelette de build (issue #3)

- Temps Donnovan : à renseigner (relecture estimée 0,3 h)
- Sessions Claude Code : 1
- Fait : `CMakeLists.txt` racine, `CMakePresets.json` (4 presets, Ninja), `vcpkg.json` avec baseline figée sur la
  release vcpkg `2026.07.29` (`9e593bb…`), bibliothèque `levain_core` et exécutable `levain_sandbox`,
  `engine/core/README.md`, section « Commandes de build » de CLAUDE.md complétée.
- Mesures :
  - Build complet depuis zéro (Clang 22.1.8, Debug, sans vcpkg) → **4 étapes Ninja**, sandbox lancé :
    `Levain 0.1.0 — clang 22.1.8 — __cplusplus 202302`.
    Commande : `cmake -S . -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++`
  - Garde-fou C++23 vérifié **dans le vrai build** : `compile_commands.json` contient bien `-std=c++23`
    (et non `gnu++23`) et `-pedantic-errors` ; une indexation de paquets C++26 insérée dans `main.cpp` fait
    échouer la compilation avec `error: pack indexing is a C++2c extension`.
  - `CMAKE_CXX_SCAN_FOR_MODULES OFF` : le build passe de **8 à 4 étapes** Ninja. CMake activait le scan de
    modules C++20 par défaut alors que l'ADR-0001 dit « sans modules » — deux étapes par fichier pour un
    résultat toujours vide.
- Décisions : pas d'ADR, rien de structurant. Deux choix signalés en PR (arborescence partielle, dépendances
  déclarées non liées).
- Écart levé le jour même : `zip` installé par Donnovan, vcpkg bootstrappé
  (`2026-07-27-98d7cb0c`) dans `~/vcpkg`. Le critère **« build depuis un clone propre en deux commandes » est
  vérifié sur Linux**, sur un vrai `git clone` du dépôt public :

  | Mesure | Valeur | Détail |
  |---|---:|---|
  | `cmake --preset linux-debug` **à froid** | **25 s** | dont 23 s de vcpkg compilant `nvrhi` (2026-02-26), `flecs` et `vulkan-headers` depuis les sources |
  | `cmake --build --preset linux-debug` | **1 s** | 4 étapes Ninja |
  | `cmake --preset linux-debug` **à chaud** (2ᵉ clone) | **1 s** | vcpkg résout en **16,5 ms** depuis le cache binaire |
  | Taille du cache binaire vcpkg | **19 Mo** | `~/.cache/vcpkg` |

  Commandes : `git clone https://github.com/PhantomDO/Levain.git <dir> && cd <dir> && VCPKG_ROOT=~/vcpkg
  cmake --preset linux-debug && VCPKG_ROOT=~/vcpkg cmake --build --preset linux-debug`

  Sortie du sandbox : `Levain 0.1.0 — clang 22.1.8 — __cplusplus 202302`. `compile_commands.json` : 2 entrées,
  flags `-std=c++23 -pedantic-errors`.

  **Conséquence pour l'issue #4** : 19 Mo de cache pour un gain de 23 s à chaque exécution. Le cache binaire
  vcpkg vaut clairement le coup en CI, et il tient largement dans les quotas du cache GitHub Actions.
- Note pour M1.2 : le port vcpkg de `nvrhi` indique qu'il faut lier `nvrhi` **et** `nvrhi_vk` sous Linux
  (build statique), `nvrhi` seul en build partagé. Les cibles `nvrhi_d3d11`/`nvrhi_d3d12` n'existent que sous
  Windows.
- Prochaine étape : issue #4 (CI Windows + Linux avec cache vcpkg), puis #5 (clang-format, clang-tidy, doctest,
  protection de `main`). Reste non vérifié : le chemin **Windows**, qui n'existe que sur le papier tant que la CI
  n'a pas tourné — c'est l'objet de #4.

## 2026-09-20 — M0.1 — Clôture du milestone

- Temps Donnovan : 1,25 h (estimé 1,0 h — **ratio 1,25**)
- Sessions Claude Code : 1
- Définition de « terminé » (SPECS §9) : 4 critères sur 7 s'appliquent et sont remplis (mesures consignées,
  board à jour, étude E1 écrite d'avance, docs à jour). Trois ne s'appliquent pas encore, faute de code :
  démo `sandbox/`, CI verte, binaires de Release.
- Fait : tag `m0.1` posé et poussé. **Pas de GitHub Release** — §9 la prévoit pour livrer les binaires et les
  mesures de la démo, il n'y en a aucun à ce stade. Le premier Release sera celui de M0.2, qui aura une CI.
  Milestone n°1 fermé.
- Écarts et problèmes : le dépassement de 0,25 h vient entièrement de l'amendement C++23, hors estimation
  initiale. À surveiller sur M0.2 et M0.3 : si le ratio 1,25 se confirme, la roadmap passe de 68 h à ~85 h et il
  faudra recalibrer (ROADMAP, section « Recalibrage »).
- Prochaine étape : **M0.2 — Squelette de build et CI** (3 issues, 1,5 h estimée). Deux acquis de M0.1 à
  reprendre : `cxx_std_23` avec `-pedantic-errors` sur le job Linux comme garde-fou de conformité (ADR-0001), et
  la préférence GPU discret à prévoir pour M1.2 (SPECS §10).

## 2026-09-20 — M0.1 — Le moteur s'appelle Levain

- Temps Donnovan : inclus dans l'heure de M0.1
- Sessions Claude Code : 1
- Fait : nom choisi, **Levain**. Dépôt renommé `PhantomDO/GameEngine` → `PhantomDO/Levain` (GitHub redirige
  l'ancienne URL), board renommé « Levain — Roadmap », SPECS en v0.3, README, CLAUDE.md et SETUP mis à jour.
  Namespace racine `levain`, cibles CMake préfixées `levain_`.
- Décisions : le namespace français est une **exception assumée** à la règle « identifiants en anglais »
  (SPECS §8) — nom propre, comme Godot. Notée dans les conventions plutôt que subie.
- Écarts et problèmes : le dossier local est encore `~/Projects/GameEngine`. À renommer entre deux sessions
  (`mv ~/Projects/GameEngine ~/Projects/Levain`), pas pendant, pour ne pas casser la session en cours.
- Prochaine étape : M0.1 close. M0.2 — arborescence, presets CMake avec `cxx_std_23`, vcpkg en manifeste, CI
  avec `-pedantic-errors` sur le job Linux.

## 2026-09-20 — M0.1 — Passage à C++23 (ADR-0001 amendé)

- Temps Donnovan : 1,0 h (estimé 1,0 h pour M0.1 — ratio 1,0), porté sur le board (0,75 h sur l'issue #1,
  0,25 h sur l'issue #2)
- Sessions Claude Code : 1
- Contexte : Donnovan valide SPECS, ROADMAP et les ADR, et demande de passer à C++23 si la norme est stable.
  La v1 de l'ADR-0001 prévoyait exactement cette réévaluation.
- Mesures :
  - Sonde de macros de test de fonctionnalité, `-std=c++23` : Clang 22.1.8 et GCC 16.2.1 (libstdc++ 16) →
    `__cplusplus = 202302`, 19 fonctionnalités C++23 sur 19 présentes.
  - Garde-fou conformité : `clang++ -std=c++23 -pedantic-errors` et `g++ -std=c++23 -pedantic-errors` refusent
    bien l'indexation de paquets C++26 (`P2662`), acceptée en `-std=c++26`.
- Décisions : ADR-0001 amendé, C++20 → **C++23**, fichier renommé `0001-langage-cpp23.md`. Sans modules,
  inchangé.
- Écarts et problèmes : **MSVC n'a pas de `/std:c++23`**, ni en VS 2022 ni en VS 2026 — seulement
  `/std:c++23preview` (ABI non garantie) et `/std:c++latest` (sur-ensemble débordant sur C++26). CMake 4.4 mappe
  `CMAKE_CXX_STANDARD 23` vers `-std:c++latest` chez MSVC. Conséquence : la CI Linux en `-pedantic-errors` fait
  autorité sur la conformité, à mettre en place en M0.2. Trous MSVC à éviter : `[[assume]]` (P1774R8), P2448R2,
  P2582R1, échappements Unicode.
- Relecture : PR #20 relue et fusionnée en **0,25 h** (15 min). ADR-0001 accepté, et avec lui les ADR 0002 à
  0007 (« ça me va »). SPECS passe en v0.3, statut « validé ».
- Cumul M0.1 : **1,25 h passée pour 1,0 h estimée** (ratio 1,25) — l'amendement C++23 n'était pas prévu
  dans l'estimation initiale.
- Prochaine étape : M0.2 — `cxx_std_23` dans les presets CMake et `-pedantic-errors` en CI Linux comme
  garde-fou de conformité, en plus de l'arborescence, de vcpkg et de la CI.

## 2026-09-20 — M0.1 — Dépôt local, modèles GitHub et machine de référence

- Temps Donnovan : à renseigner
- Sessions Claude Code : 1
- Fait : modèles d'issue et de PR déplacés de `tools/github-templates/` vers `.github/` ; dépôt git local initialisé
  (`main`) et premier commit ; SPECS §10 complété à partir de la machine.
- Mesures : machine de référence → CachyOS noyau 7.2.6-1-cachyos, Ryzen 7 7800X3D, RX 9070 XT (GFX1201),
  Mesa RADV 26.2.3-arch3.1, Vulkan 1.4.354, 16 Go (`vulkaninfo --summary`, `uname -r`, `/proc/cpuinfo`,
  `/proc/meminfo`). Outils présents : CMake 4.4.3, Ninja 1.13.2, Clang 22.1.8, GCC 16.2.1, git 2.55.0, gh 2.101.0.
- Écarts et problèmes : deux GPU Vulkan sur la machine (le discret et l'iGPU du 7800X3D) — la sélection de device
  en M1.2 devra préférer le discret ; plusieurs couches Vulkan implicites tierces sont installées et celle de
  Lossless Scaling est cassée (erreur du loader à chaque `vkCreateInstance`). Les deux points sont notés
  dans SPECS §10.
- Décision : licence **MIT** (`LICENSE`), retirée des questions ouvertes de SPECS §11 et inscrite dans les
  conventions (SPECS §8). Compatible avec le code adapté de Donut, qui garde son en-tête MIT.
- Suivi GitHub créé par `./tools/github-bootstrap.sh GameEngine` : dépôt public
  [PhantomDO/GameEngine](https://github.com/PhantomDO/GameEngine), 17 labels, 35 milestones (M0.1 échéance
  27/09/2026 → M8.3 échéance 08/08/2027), board n°1 avec les champs Estimé (h), Passé (h) et Phase, et les
  19 issues des phases 0 et 1, chacune avec son milestone, ses labels, son estimation et sa phase.
- Prochaine étape : M0.2 — arborescence, presets CMake, vcpkg en mode manifeste, CI, puis protection de
  `main` (issue #5). Les issues #1 et #2 (M0.1) attendent Donnovan.

## 2026-09-20 — M0.1 — Machine de référence et vérification Windows

- Temps Donnovan : à renseigner
- Fait : machine de référence renseignée (CachyOS, Ryzen 7 7800X3D, Radeon RX 9070 XT, 16 Go) ; versions d'OS et
  de pilote à compléter avec `vulkaninfo --summary`.
- Décision : pas de machine Windows ; vérification en trois niveaux (CI + WARP, binaire Windows sous Proton sur la
  machine de référence, vraie machine Windows ponctuellement). Une VM Windows écartée : sans passthrough GPU, elle
  n'apporte rien de plus que WARP.
- Dépôt GitHub : pas encore créé ; Claude Code le créera avec `tools/github-bootstrap.sh` après la connexion de
  Donnovan à `gh`. Nom provisoire : GameEngine.

## 2026-09-20 — M0.1 — Révision : NVRHI et flecs

- Temps Donnovan : à renseigner (échange vocal + relecture)
- Sessions Claude Code : 0
- Contexte : Donnovan précise que **la priorité est de faire un jeu avec notre moteur**, et qu'apprendre Vulkan en
  détail ne l'intéresse pas.
- Décisions proposées : NVRHI à la place d'une RHI maison, backends Vulkan + Direct3D 12 (ADR-0002, réécrit) ;
  flecs plutôt qu'EnTT ou un ECS maison, pour sa documentation, son explorer, ses hiérarchies, ses pipelines et sa
  réflexion JSON (ADR-0004, réécrit) ; Slang compilé en SPIR-V et DXIL (ADR-0005, révisé).
- Correction apportée : NVRHI n'est pas la norme de l'industrie (Unreal, Unity et Godot ont leur propre RHI) et
  ne gère ni OpenGL ni Metal (backends : D3D11, D3D12, Vulkan).
- Roadmap v0.2 : 77 h → 68 h Donnovan, 55 → 49 sessions, 35 milestones ; « RHI mince » supprimé ; ajout de M1.4
  (backend D3D12) et M3.5 (choix du jeu) ; le jeu jouable visé début août 2027 à 1,5 h/semaine.
- Ajouts : étude E1 (couches RHI) écrite en avance, `docs/LECTURES.md` (lectures commentées).
- Prochaine étape : inchangée (validation des specs, puis création du dépôt et M0.2).

## 2026-09-20 — M0.1 — Specs initiales

- Temps Donnovan : à renseigner (estimé 1,0 h pour tout M0.1)
- Sessions Claude Code : 0 (rédaction dans une conversation claude.ai)
- Fait : SPECS v0.1, ROADMAP v0.1 (77 h Donnovan, 55 sessions, 34 milestones), ADR 0001 à 0007, CLAUDE.md,
  modèles d'issue et de PR, script `tools/github-bootstrap.sh`.
- Décisions proposées : C++20 sans modules, Vulkan 1.3 avec RHI maison, SDL3, ECS maison à sparse sets, Slang,
  GitHub public, CMake + vcpkg.
- Prochaine étape : Donnovan valide ou amende les specs et les ADR, renseigne la machine de référence, choisit
  le nom ; puis création du dépôt avec le script et démarrage de M0.2 dans Claude Code.
