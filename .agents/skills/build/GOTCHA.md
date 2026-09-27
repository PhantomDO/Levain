# Pièges — build, tests, sanitizers, CI

Un piège par entrée : symptôme, cause, parade. Le plus récent en haut. Les pièges propres à SDL sont détaillés
dans `engine/platform/README.md`, ceux de flecs dans `engine/scene/README.md`, section « Pièges connus ».

## Helium headless ne charge aucune page (2026-09-27)

- **Symptôme** : Helium 0.18 (Chromium 154) en `--headless=new`, profil neuf : l'onglet reste sur `about:blank`,
  `Page.navigate` ne répond jamais. Même `example.com`.
- **Cause** : son uBlock Origin intégré suspend le réseau jusqu'au chargement de ses listes de filtres, qu'il
  ne peut pas télécharger (services de Helium désactivés) : `µBlock.readyToFilter` reste à `false`.
  `--disable-extensions` ne le retire pas. Chez Donnovan, Helium ne charge pas non plus google.com.
- **Parade** : pas de Helium pour les tests. Firefox pour `tools/web-smoke.sh` ; un Chromium sur téléphone
  (navigateur DuckDuckGo, WebView système) pour le critère « Chromium ».

## SDL dans une page web (2026-09-27)

- **Symptôme 1** : « Document.querySelector: '##canvas' is not a valid selector ». **Cause** :
  `SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING` rend un sélecteur, dièse compris, malgré son nom. **Parade** : le
  prendre tel quel (`device_web.cpp`).
- **Symptôme 2** : les crédits de la page ont disparu. **Cause** : avec `SDL_WINDOW_FILL_DOCUMENT`, SDL range tout
  ce qui n'est pas le canvas dans un élément caché (`SDL3_fill_document_background_elements`). **Parade** : un
  calque unique, qu'un `MutationObserver` ressort (`sandbox/web/shell.html`).
- **Symptôme 3** : une modification du modèle de page n'apparaît pas. **Cause** : `--shell-file` est une option
  de lien, pas une dépendance ; ninja ne relie pas. **Parade** : `LINK_DEPENDS` sur la cible.

## Dawn natif tolère ce que le navigateur refuse (2026-09-27)

- **Symptôme** : le cube passe en natif sur Dawn (0 pixel d'écart), mais dans Firefox la page reste noire, avec
  en console « 'depthClearValue' member … is not a finite floating-point value ».
- **Cause** : dans `webgpu.h`, `depthClearValue` vaut NaN par défaut (« indéfini »). Dawn natif l'accepte quand
  la profondeur est chargée (`LoadOp::Load`) ; la couche JavaScript du navigateur valide le dictionnaire avant
  de regarder l'opération.
- **Parade** : donner une valeur finie à tout champ numérique d'un descripteur, même inutilisé. Le test en natif
  ne suffit pas : passer `tools/web-smoke.sh` après toute modification du backend WebGPU.

## clang-tidy sur un fichier que le build ne compile pas (2026-09-27)

- **Symptôme** : l'analyse statique de la CI échoue sur `canvas.cpp` (« use of undeclared identifier
  'WebGpuCanvasDeleter' »), puis compte ses 12 erreurs à chaque fichier suivant.
- **Cause** : la CI donnait à clang-tidy tous les `.cpp` du dépôt. Un fichier compilé seulement sous Emscripten
  n'est pas dans `compile_commands.json` du build natif : clang-tidy l'analyse sans `__EMSCRIPTEN__`, donc à tort.
- **Parade** : la liste vient de `compile_commands.json` (SKILL, section clang-tidy ; `ci.yml`).

## WebAssembly révèle les alignements supposés (2026-09-27)

- **Symptôme** : en WebAssembly, « le pool distribue des blocs distincts et alignés » échoue sur
  `isAligned(block, 16)` ; en natif, tout passait.
- **Cause** : `PoolAllocator` prenait `new std::byte[]` pour assez aligné. La norme ne garantit que
  `alignof(std::max_align_t)` : 16 octets sur un PC 64 bits, 8 en wasm32. En natif, un pool aligné sur 64 octets
  (une ligne de cache) échouait déjà, sans qu'aucun test le demande.
- **Parade** : réserver `alignement − 1` octets de plus et aligner l'adresse réelle du premier bloc, comme
  `LinearAllocator`. Un test demande 64 octets. Tout calcul d'alignement se fait sur l'adresse, jamais sur un
  décalage depuis le début d'un tampon.

## ktx ne compile pas sous Emscripten (2026-09-27)

- **Symptôme** : `cmake --preset web` échoue dans vcpkg sur ktx, « embind requires -std=c++17 or newer », dans
  `interface/js_binding/transcoder_wrapper.cpp`. Suivi d'un trompeur « unable to find a build program
  corresponding to Ninja » : c'est la suite de l'échec de vcpkg, pas un Ninja manquant.
- **Cause** : sous Emscripten, ktx 4.4.2 compile toujours ses liaisons JavaScript, en C++11, qu'Emscripten 6
  refuse. Aucune option ne les coupe.
- **Parade** : `triplets/wasm32-emscripten.cmake` passe `-DCMAKE_CXX_STANDARD=17` à ktx seulement. À retirer
  quand la baseline aura une version de ktx qui compile sans.

## Une erreur de chargement qui finit en assertion Vulkan (2026-09-25)

- **Symptôme** : un modèle dont une texture est illisible (`unknown image type`), ou un mauvais nom de
  `--locomotion`, arrête le sandbox Debug sur « vkDestroyDevice(): … has N leaked objects » puis sur
  l'assertion `!isValidationError(severity, types)` (code 133), au lieu d'un échec propre (ADR-0008).
- **Cause** : `createDemoScene` retournait l'erreur avec sa command list d'envoi encore ouverte. NVRHI ne libère
  jamais une command list ouverte puis détruite : `open()` l'inscrit dans les ressources de son propre command
  buffer (`vulkan-commandlist.cpp`), un cycle que seule la file rompt, quand elle retire le command buffer
  soumis. Tout ce qui a été enregistré (meshes, `UploadChunk`) fuit avec elle.
- **Parade** : un retour anticipé après `open()` ferme et soumet la command list (`submitAbandonedUpload`,
  `sandbox/src/main.cpp`), ou le travail qui peut échouer passe avant `open()`, comme les noms de
  `--locomotion`. Reproduire : copier un glTF dans `data/`, remplacer sa texture par du texte, lancer
  `SDL_VIDEO_DRIVER=offscreen levain_sandbox --seconds 1 --model <gltf>` en Debug : code 1 attendu, sans fuite.

## RenderDoc ne capture rien quand la session est verrouillée (2026-09-24)

- **Symptôme** : `tools/renderdoc-mips.py` échoue sur « aucune capture reçue en 30 s », et des sandbox restent en
  vie bien après leur `--seconds`, en attente dans `poll`.
- **Cause** : le script forçait une fenêtre X11 (RenderDoc 1.45 masque la surface Wayland). Session verrouillée
  (Donnovan à distance), la fenêtre n'est jamais présentée : le sandbox attend un événement, et RenderDoc, qui
  capture à la présentation, ne reçoit rien. `gdb` ne peut pas s'attacher (`ptrace_scope = 1`) : l'état des
  threads se lit dans `/proc/<pid>/task/*/wchan`.
- **Parade** : `tools/renderdoc_capture.py` lance le sandbox **hors écran** (surface « headless », indépendante
  de tout écran), et désactive pour ce seul processus les couches Vulkan implicites de la machine
  (`DISABLE_LSFGVK`, `DISABLE_MAKO`, prévues par leurs manifestes). Ne pas toucher aux réglages du système.
  Arrêter un sandbox bloqué par son PID (`kill -9 <pid>`), jamais par `pkill -f` : le motif se retrouve dans
  la ligne de commande du shell qui le lance, et le tue.

## Une limite que seule une grosse scène atteint, et que seul le Debug voit (2026-09-24)

- **Symptôme** : le sandbox avec Sponza s'arrête en Debug et sous ASan sur « Volatile constant buffer … has
  maxVersions = 16, which is insufficient ». En Release, il tourne et l'image paraît juste.
- **Cause** : chaque `drawMesh` écrit ses constantes dans une nouvelle version du buffer volatil ; 16
  suffisaient aux quelques dessins de la démo, pas aux 105 de Sponza. La validation de NVRHI, seule à le
  détecter, est éteinte en Release.
- **Parade** : `MaxMeshDrawsPerCommandList` (4 096), et la CI lance Sponza en Debug. Toute scène de test
  lourde doit passer au moins une fois en Debug avant qu'on se fie à une image Release.

## `cmake -P` : aucune politique par défaut (2026-09-23)

- **Symptôme** : `cmake.vcpkg-manifest` vert en local (CMake 4.4), rouge sur les trois jobs de la CI : « Policy
  CMP0057 is not set: Support new IN_LIST if() operator », puis un manifeste correct refusé.
- **Cause** : un script lancé par `cmake -P` n'hérite d'aucun `cmake_minimum_required` : ses politiques sont
  celles des vieilles versions, et `IN_LIST` n'est pas un opérateur. Le CMake 4 local les active par défaut, et
  masquait l'erreur.
- **Parade** : un script autonome commence par `cmake_minimum_required(VERSION 3.28)`. Un fichier aussi inclus
  par `include()` règle `cmake_policy(VERSION 3.28)` seulement si `CMAKE_SCRIPT_MODE_FILE`, **avant** ses
  fonctions, qui gardent les politiques de leur définition.

## Une signature qui coûte 5 ns par entité (2026-09-22)

- **Symptôme** : le système des matrices monde passait de 1,42 à 1,91 ms sur 100 000 entités, sans changement
  de logique. Même code écrit à la main dans un binaire de test : 1,42 ms.
- **Cause** : `worldMatrix(const glm::mat4&, const Transform&)` composait la matrice locale elle-même. Écrite
  ainsi, le compilateur matérialise et copie la matrice de retour ; en lui passant la matrice locale déjà
  composée, il écrit directement dans la destination.
- **Parade** : pour une fonction appelée par entité et par image, passer ce qui est déjà calculé plutôt que ce
  qu'il faut calculer. Le bisectage se fait en réécrivant le même corps à la main dans un binaire de test, et
  en supprimant une différence à la fois (termes de requête, phase, ordre de création, signature).

## Shaders

- **Lire l'erreur d'un shader dans la sortie de ninja** (2026-09-22) : chercher « error » ramène aussi la
  ligne de commande, qui contient `-warnings-as-errors`, et les nouveaux diagnostics de slangc
  (`error[E20001]`) s'étalent sur plusieurs lignes, avec la ligne fautive et un repère. Ninja encadre chaque
  échec : `FAILED: …`, la commande, puis la sortie de la commande jusqu'à la ligne d'état suivante `[n/m]`. Garder
  la sortie de la première commande en échec (`firstFailure`, `sandbox/src/shader_reload.cpp`) : les deux
  points d'entrée d'un fichier échouent sur les mêmes erreurs.
- **`SV_VertexID` exige `shaderDrawParameters`** (2026-09-21). En HLSL, il compte depuis 0 sans le sommet de base
  du draw ; en Vulkan, il l'inclut. Slang compense en lisant ce sommet de base (capacité SPIR-V
  `DrawParameters`) : sans la fonctionnalité côté device, la validation refuse le shader à sa création.
- **Aucun `-fvk-invert-y`** : NVRHI inverse déjà le viewport sous Vulkan. L'ajouter mettrait le triangle à
  l'envers.

## Fenêtre et SDL

- **Environ 20 images/s très régulières sous Wayland, et une fenêtre X11 qui ne s'ouvre plus** (2026-09-21,
  soirée) : `SDL_CreateWindow` bloque dans `X11_ShowWindow`. Le même binaire donnait 120 images/s quelques
  heures plus tôt, et les deux symptômes ont disparu ensemble quelques minutes après, sans changement de code :
  c'est l'environnement, pas le moteur. Cause supposée, non vérifiée : l'écran HDMI éteint côté TV. En cas de
  doute, mesurer en offscreen, où rien ne bride, et relancer plus tard.

- **Fenêtre invisible sous Wayland** (2026-09-21). Une surface Wayland n'apparaît qu'après sa première image ;
  tant que le moteur ne présente rien (avant M1.2), KWin ne la connaît pas. Parade : `SDL_VIDEO_DRIVER=x11`.
- **Deux SIGTERM font sauter une assertion du SDL de Debug** (`SDL_quit.c:171`), et SDL ouvre une boîte de
  dialogue zenity sur le bureau, qui attend une réponse. Cause : `timeout` signale l'enfant **puis** son groupe
  de processus. Parade : `timeout --foreground`, toujours.
- **SIGINT ignoré en arrière-plan.** Un processus lancé avec `&` depuis un shell non interactif hérite d'un
  SIGINT ignoré, et SDL respecte ce choix : `kill -INT` ne fait rien. Arrêter le sandbox par SIGTERM.
- **Titres non ASCII perdus sous X11** (`SDL_x11window.c:2300`) : SDL abandonne en silence et fuit. Une
  assertion ASCII dans `setWindowTitle` l'empêche.
- **Dépendances système de SDL en CI.** Les paquets suggérés par le port vcpkg ne suffisent pas : SDL exige
  huit extensions X11 (contrôles stricts de `cmake/sdlchecks.cmake`). La liste de `ci.yml` vient du
  `README-linux` de SDL ; en cas d'erreur `Couldn't find dependency package for X`, chercher le `-dev` de X.
- **Sous KWin, `Scripting.start()` exécute le script plus tard** : le décharger tout de suite l'annule sans
  message. `tools/kwin-window-smoke.sh` attend 0,5 s entre les deux.

## CI

- **Le démarrage peut manger tout le délai du sandbox** (2026-09-21). Sur le runner, il varie de 1 à plus de
  10 s selon la charge (lavapipe, couches de validation, sanitizers). Un délai de 3 s, puis de 8 s, est tombé
  avant la première frame ; l'étape restait verte la première fois, le contrôle « boucle ≥ 1 s » l'a fait
  rougir la seconde (PR #59). Parade : `--seconds N`, compté depuis le premier tour de boucle ; `timeout`
  n'est plus qu'un filet contre un blocage.
- **Un cache GitHub n'est visible que de sa branche et de `main`** (2026-09-21). Le cache rempli par une PR ne
  sert pas à la PR suivante : après un changement du manifeste, les PR n'en profitent qu'une fois qu'une CI a
  tourné sur `main`. En cas d'erreur réseau (504) sur un job, relancer les jobs en échec après que `main` a
  enregistré ses caches.
- **Le bootstrap de vcpkg exige que `VCPKG_DOWNLOADS` soit un dossier existant** (2026-09-21) : « was set to
  …, but that was not a directory ». Restaurer ce cache et créer le dossier **avant** le bootstrap ; le cache
  binaire, lui, peut attendre après.
- **`timeout … | tee`** : sans `set -o pipefail`, le code de sortie est celui de `tee`, et une fuite signalée
  par LeakSanitizer passerait inaperçue.

## Sanitizers

- **UBSan dans stb_image_resize2 v2.10** (port vcpkg `stb` 2024-07-29, 2026-09-21) : « load of misaligned
  address … for type 'stbir_uint64' », ligne 3653. `STBIR_MOVE_2` copie deux `float` par une lecture 64 bits
  non alignée, un comportement indéfini. Parade : ne pas l'utiliser ; les mipmaps passent par un filtre boîte
  écrit à la main (`engine/assets/src/image.cpp`). stb_image, le décodeur, passe sous ASan et UBSan.
- **LeakSanitizer et RADV : 128 octets** (2026-09-21). Le loader Vulkan décharge le pilote à la destruction de
  l'instance ; la mémoire que RADV gardait dans une globale paraît alors perdue. Diagnostic : la fuite
  disparaît avec `LD_PRELOAD=/usr/lib/libvulkan_radeon.so`, persiste sans couche de validation et sans
  `device_select`. Vérification sans faux positif : **Wayland + RADV préchargé**, code 0.
- **Ne pas précharger RADV et libX11 ensemble sous X11** : `SDL_CreateWindow` bloque dans `X11_ShowWindow`
  (`XIfEvent` attend un `MapNotify` qui ne vient pas). Configuration de diagnostic seulement, sans incidence sur
  un lancement normal.

- **Faux positifs LeakSanitizer sous X11** : ~50 Ko en ~900 allocations, la mémoire permanente de libX11 que SDL
  décharge par `dlclose`. Pour vérifier qu'il ne reste rien de vrai, précharger les bibliothèques X11
  (`LD_PRELOAD=/usr/lib/libX11.so.6:…`) : les faux positifs disparaissent, les vraies fuites restent. La CI
  tourne en offscreen et n'est pas concernée.
- **Pile tronquée à une bibliothèque système** : `ASAN_OPTIONS=fast_unwind_on_malloc=0` pour une pile complète.
- **Un code de sortie lu à travers un pipe** est celui du dernier programme (`| tail` rend 0). Rediriger vers
  un fichier, puis lire `$?`.

## Contre-tests

- **`pkill -f <motif>` tue le shell qui le lance** (2026-09-21) : le motif figure dans sa propre ligne de
  commande. Chercher par nom exact : `pgrep -a -x levain_sandbox`, `pkill -x levain_sandbox`.
  Le 2026-09-27, trois fois de plus, sous d'autres formes : `pgrep -f`, puis `ps | grep "[h]ttp.server"`,
  alors que la même commande lançait ensuite `python3 -m http.server` (le texte du motif était dans le script
  du shell). **Garder le PID au lancement** (`programme & echo $! > fichier.pid`, puis `kill $(cat
  fichier.pid)`) pour tout processus qu'on arrêtera plus tard : serveur HTTP, Firefox de test.
- **Une faute injectée pour un contre-test se retire depuis une copie** (`cp fichier copie`, puis `cp copie
  fichier`), jamais par `git checkout -- fichier` : il efface aussi tout ce qui n'était pas encore commité. C'est
  arrivé le 2026-09-21 à l'intégration du device dans le sandbox, réécrite ensuite.
- Méthode qui marche pour la validation : un buffer Vulkan de taille 0 (`VUID-VkBufferCreateInfo-size-00912`) et
  une texture NVRHI de largeur 0 doivent chacun arrêter le programme (code 133, SIGTRAP).

## Avertissements et clang-tidy

- **Une variable qui ne sert qu'à une assertion** : `LEVAIN_ASSERT` compile son expression en Release sans
  l'évaluer (`sizeof`), donc pas d'avertissement. Une **fonction interne** dans le même cas reste signalée par
  clang (`-Wunneeded-internal-declaration`) : `[[maybe_unused]]`.
- **Constante globale d'un type non `constexpr`** (`const nvrhi::Color c{…}`) : clang-tidy la refuse
  (`bugprone-throwing-static-initialization`), une exception levée avant `main` ne se rattrape pas. La rendre
  locale à la fonction qui s'en sert.
- **Initialisation désignée incomplète** (`-Wmissing-designated-field-initializers`) : donner un initialiseur
  par défaut au champ (`PixelSize pixelSize{};`) plutôt que d'écrire `.champ = {}` partout.
- **clang-tidy 22 et `std::optional`** : `.value()` compte comme un accès non vérifié, et `REQUIRE` de doctest
  n'est pas reconnu comme une garde. Dans les tests : `value_or(T{})`, dont la valeur par défaut fait échouer
  les `CHECK`.
- **Macros de doctest** : `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` est posée par `set_source_files_properties` dans
  `tests/CMakeLists.txt`, pas par un `#define` que clang-tidy refuserait (préfixe `LEVAIN_`).
- **`main` et les exceptions** : `std::print` peut lever, et une exception qui sort de `main` est signalée par
  clang-tidy. Un `try`/`catch` au sommet de `main` (ADR-0008).

## Bureau de Donnovan

- **Ne pas faire de capture d'écran de son bureau** (2026-09-21). `spectacle -a` capture la fenêtre active, pas
  celle qu'on vise : KWin a refusé de donner le focus au profileur, et l'image montrait son navigateur sur une
  page de connexion. Supprimée aussitôt. Une capture d'écran se demande à Donnovan.
- **`pkill -f <motif>` tue aussi le shell qui l'exécute** si sa ligne de commande contient le motif. Cibler par
  nom de processus (`pgrep -x`, `ps -eo pid,comm`).

## Chaîne d'outils

- **`VCPKG_ROOT` non exportée** : CMake prenait les paquets du système (le `spdlog` d'Arch) sans rien dire. Le
  `CMakeLists.txt` racine refuse maintenant de se configurer sans la toolchain vcpkg.
- **LLVM en CI** : `update-alternatives` ne remplace pas `/usr/bin/clang++`, qui est un vrai fichier. La CI a
  tourné trois milestones sur clang 18 en croyant utiliser clang 22. Parade en place : liens dans
  `/usr/local/bin` et étape « Vérifier la chaîne », qui échoue si la version n'est pas la bonne.
- **clang-format change de sortie d'une version majeure à l'autre** : la version de la CI (`LLVM_VERSION`) doit
  rester celle de la machine de référence.
- **`$env{…}` dans une chaîne de `message()` CMake** est interprété et casse le parsing : l'écrire autrement.
- **Le bootstrap de vcpkg exige `zip`** (paquet système).
- **Le shell des commandes de l'agent est zsh** : une variable non quotée n'y est pas découpée en mots
  (`$args` valant `--seconds 2` arrive en un seul argument). Écrire les arguments en toutes lettres.
- **Le shell de Donnovan est fish** : `set -Ux` pour une variable d'environnement persistante. Les scripts du
  dépôt commencent par `#!/usr/bin/env bash`.

## RenderDoc

- **`qrenderdoc --python` n'affiche rien** (2026-09-21) : les `print` vont dans la console Python de
  qrenderdoc, pas sur la sortie standard. Écrire dans un fichier (`tools/renderdoc-mips.py`) et finir par
  `os._exit`, sinon qrenderdoc ouvre son interface après le script et ne rend jamais la main.
- **Le module Python `renderdoc` n'existe que dans qrenderdoc** : le paquet Arch ne l'installe pas pour le
  Python du système. D'où `QT_QPA_PLATFORM=offscreen qrenderdoc --python …`, sans fenêtre.
- **Au premier lancement, qrenderdoc bloque avant le script** (2026-09-21) : même un script d'une ligne ne
  s'exécute pas, le journal (`/tmp/RenderDoc/*.log`) s'arrête après `LoadLayout`. Cause : la question sur les
  statistiques d'usage (`AnalyticsPromptDialog`), invisible en offscreen. Vérifié : le script tourne dès que
  Donnovan y a répondu, en lançant `qrenderdoc` une fois. C'est son choix, pas celui de l'agent.
- **RenderDoc 1.45 masque `VK_KHR_wayland_surface`** : sous RenderDoc, `SDL_CreateWindow` échoue sous Wayland.
  Lancer le programme capturé avec `SDL_VIDEO_DRIVER=x11` (XWayland). `renderdoccmd capture -w …` montre la
  sortie du programme capturé, que `ExecuteAndInject` cache.
- **Capturer exige un bureau visible et déverrouillé** (2026-09-24) : `tools/renderdoc_capture.py` impose
  X11. Bureau verrouillé, trois sandbox `--seconds 6` ne se sont jamais arrêtés, ni sur SIGTERM (il a fallu
  `kill -9`). L'échéance ignorée fenêtre masquée est corrigée (`waitEvents` borné) ; le SIGTERM ignoré n'a pas
  été reproduit : minimisée, SIGTERM réveille bien l'attente (code 0). Cause supposée, non vérifiée : un blocage
  dans la présentation X11 plutôt que dans l'attente d'événements. En `SDL_VIDEO_DRIVER=offscreen`, le sandbox
  a aussi semblé bloquer sous RenderDoc ; piste non vérifiée, la couche Vulkan implicite de l'utilisateur
  `liblsfg-vk-layer.so` (« Failed to find vkGetInstanceProcAddr »). Ne pas toucher aux couches de l'utilisateur.
- **Mesurer une image de capture avec ImageMagick** (2026-09-21) : les PNG de RenderDoc ont un canal alpha,
  compté par `fx:standard_deviation` avec une variance nulle : le contraste mesuré est divisé par deux. Toujours
  `-alpha off`. Et `-gravity` persiste d'une option à l'autre : un `-splice` après `-gravity center` insère ses
  lignes au milieu de l'image (`+gravity` d'abord).
- **Objets sans nom dans une capture** : NVRHI ne transmet les `debugName` que s'il sait `VK_EXT_debug_utils`
  active, annoncée dans `DeviceDesc::instanceExtensions` (`engine/gpu/src/device_vk.cpp`).

## Tracy

- **`TRACY_ENABLE` est OFF par défaut depuis Tracy 0.14** (2026-09-21). Sans lui, les macros se compilent en
  rien : le build réussit, le binaire ne profile rien. Le port overlay le force, et `engine/core/CMakeLists.txt`
  refuse de configurer un build profilé sans lui. Preuve qu'un client est actif : il écoute sur le port 8086
  (`ss -ltnp | grep 8086`), et `TRACY_NO_EXIT=1` l'empêche de sortir.
- **`TRACY_NO_EXIT=1` n'est pas optionnel** pour un programme court : sans lui, le programme se termine avant
  qu'un profileur ait pu se connecter, et n'affiche aucun avertissement.
- **Client et profileur doivent avoir la même version de protocole** : 0.14.1 des deux côtés (port overlay pour
  le client, release officielle pour les outils, empreinte SHA-256 vérifiée contre celle publiée par GitHub).

## Quand Windows reviendra

Les presets et les jobs CI Windows ont été retirés (ADR-0011) : les remettre ensemble. Deux bugs de M0.2,
propres à MSVC : `__cplusplus` figé à 199711 sans `/Zc:__cplusplus`, et `<ostream>` non inclus en cascade par la
STL de Microsoft. Voir l'ADR-0011 pour le choix du compilateur (clang-cl d'abord).
