# Pièges de la boucle autonome

Chaque entrée : symptôme, cause, parade, date. La plus récente en haut.

## `/tmp` est un tmpfs de 24 Go, de la mémoire : un clone d'essai y a fait échouer l'édition de liens (2026-10-09)

- **Symptôme** : un clone d'essai du sommet de la pile, construit sous le scratchpad de la session
  (`/tmp/claude-1000/…`), s'arrête sur « ld.bfd: final link failed: No space left on device » ; `/tmp` reste à 98 %
  pendant environ cinq minutes (2026-10-09, vers 07 h 05 à 07 h 10), le temps de tuer le build et de supprimer le
  clone : tout autre processus qui écrivait dans `/tmp` pouvait y rencontrer ENOSPC.
- **Cause** : `/tmp` n'est pas sur le disque de la distro, c'est un tmpfs (`df -h /tmp` : « tmpfs 24G »), donc de la
  mémoire. Le scratchpad y gardait déjà de vieux builds et des téléchargements de xwin ; le clone a fait le reste :
  `df -h /tmp` lisait 24 Go de taille, 23 Go utilisés, 609 Mo libres, 98 %. Le clone supprimé et les vieux builds
  purgés, il lisait 21 %. Le disque de la distro avait 712 Go libres (`df -h ~`).
- **Parade** : les clones d'essai et les builds sur le disque de la distro (`git worktree add
  ~/Projects/Levain-<sujet>`, pr-autonome/SKILL.md § 2), jamais dans le scratchpad, qui ne reçoit que de petits
  journaux et des scripts ; purger les vieux builds du scratchpad ; `df -h /tmp` avant un build.

## Un `&` après `cd … && …` met la liste entière en arrière-plan, `cd` compris (2026-10-08)

- **Symptôme** : les commandes qui suivaient un `cd <worktree> && … &` se sont exécutées dans le dossier
  `/mnt/c/…/Projects/Levain` et ont écrit dans son `shaders/mesh.slang`, que la session ne devait pas toucher (la copie
  Windows, aux fins de ligne CRLF, d'où Claude Code a été lancé : session/GOTCHA.md, « La session distante montre le
  dossier où Claude Code a été lancé »). Le fichier a été restauré octet pour octet.
- **Cause** : en bash, `cd d && cmd &` envoie la liste entière, `cd` compris, dans un sous-shell en arrière-plan ; le
  shell courant n'a pas bougé, et la commande suivante part du dossier d'où la session a été lancée.
- **Parade** : des chemins absolus, ou `git -C <worktree>` ; un script plutôt qu'une liste ; pour le fond, l'outil de
  tâches de fond (qui réveille la session à la fin, pr-autonome/SKILL.md § 6), jamais un `&`. Un dossier qu'aucune
  commande ne doit toucher se vérifie ensuite par `git -C <dossier> status --short`.

## `merge-stack.sh` arrêté par GitHub, deux fois dans la journée (2026-10-08, 2026-10-09)

*(2026-10-09 : les piles ne s'utilisent plus et `tools/merge-stack.sh` est supprimé, la CI ne tournant qu'une fois par
fonctionnalité, AGENTS.md règle n°1 ; l'entrée reste comme histoire.)*

- **Symptômes** : « #348 : checks pas verts (error connecting to api.github.com) » alors que la CI tournait ;
  puis, une pile plus loin, « #351 : base non changée » après la fusion de #350.
- **Causes** : une coupure réseau, que `gh pr checks` rend comme une sortie en erreur ; et `gh pr edit --base`, qui
  échoue avec le `gh` 2.46 d'Ubuntu 26.04 (celui de la distro WSL) sur une erreur GraphQL (« Projects (classic) is
  being deprecated ») : cette version interroge encore les anciens Projects, que GitHub a retirés de son API ; le `gh`
  récent de la machine de référence n'a pas le problème. `gh pr edit --body-file` échoue de la même façon, avec la même
  erreur GraphQL (`projectCards`) : changer le texte d'une PR est aussi bloqué que changer sa base. Le script
  s'arrêtait proprement les deux fois, la branche gardée.
- **Parade** : le script attend 10 min au plus quand GitHub est injoignable, et change la base par l'API REST
  (`gh api -X PATCH repos/{owner}/{repo}/pulls/N -f base=main`). Contre-testé avec un faux `gh` dans le `PATH`.
  Pour rattraper une pile arrêtée : changer la base par l'API REST, supprimer la branche gardée
  (`git push origin --delete <branche>`), puis relancer le script sur la suite.
  Pour le texte d'une PR, la même voie : `gh api -X PATCH repos/PhantomDO/Levain/pulls/N -F body=@fichier.md` (le `-F`
  majuscule lit le fichier ; `-f` enverrait la chaîne « @fichier.md » comme corps).

## Les mesures d'un ADR viennent d'une commande, même pressé (2026-10-08)

- **Symptôme** : la relecture de l'ADR-0035 a trouvé dans son premier commit « 8 s » sans commande, une cause de
  l'échec de `waitEvents` affirmée sans preuve (il passait seul) et « 4 tests sur 7 » (6 sur 9) ; dans un
  brouillon, avant ce commit, « 30 ports » (25 mesurés), « 14 min » (16 au plus) et un processeur deviné (« i7 »,
  un i9-14900HX).
- **Cause** : des chiffres repris de mémoire ou de notes, pas relus contre leur commande avant le commit.
- **Parade** : chaque chiffre d'un ADR se relit contre sa commande avant la relecture (session/GOTCHA.md, « Mesurer
  avant d'annoncer un chiffre ») ; une cause non prouvée s'écrit « à trouver ». La relecture « faits » d'un
  workflow, qui refait les comptes sur les journaux, a payé.

## Un avis de fin traité comme sans suite : la nuit perdue (2026-10-08)

- **Symptôme** : Donnovan part la nuit (« continue comme ça, je reviens demain matin ») ; au matin, aucune PR n'a
  été ouverte depuis 00 h 40, et l'inspecteur n'a pas commencé.
- **Cause** : le workflow de la hiérarchie s'est terminé à 00 h 40 ; la session a répondu à son avis de fin sans
  rien lancer. Plus rien ne tournait pour la réveiller.
- **Parade** : en mode autonome, chaque avis de fin (workflow, agent, commande de fond) reçoit l'action suivante :
  lire, relire, ouvrir, lancer la suite. Avant de finir un tour, vérifier que quelque chose tourne encore et
  réveillera la session (un workflow, `merge-stack.sh` en tâche de fond), ou lancer la tâche suivante.

## Deux sessions, deux machines, un dépôt (2026-10-08)

- **Symptôme** : deux sessions travaillent sur le même dépôt, l'une sur la machine de référence (16 Go), l'autre
  dans la distro WSL du portable Windows (64 Go, `tools/wsl/`), lancée en Remote Control.
- **Cause** : la mémoire locale d'une session (ses notes, ses leçons) ne suit pas d'une machine à l'autre, et
  rien n'empêche deux sessions d'ouvrir chacune une PR.
- **Parade** : la règle n°1 vaut pour les deux : une seule session ouvre des PR à la fois. La passation se fait
  par le dépôt (ce skill, le JOURNAL). Observé le 2026-10-08 : la session WSL apparaissait dans `ListAgents` de
  l'autre, qui pouvait lui écrire, et elle répondait ; la documentation de Remote Control ne le promet que
  pour un même compte, donc ne pas compter sur ce canal.

## Une PR annoncée sous 400 lignes en fait 1 081 (2026-10-07)

- **Symptôme** : la PR de `reflection.hpp` (M7.2), prévue d'un bloc par l'ADR-0034, sort du workflow à 1 081
  lignes, deux fois et demie la règle n°2 ; la parade prévue par l'ADR (sortir les tests de refus à la compilation)
  ne suffit pas.
- **Cause** : l'estimation comptait l'en-tête, pas les tests de refus (un lancement par cas), le README, le GOTCHA,
  ni le script de l'échelle.
- **Parade** : demander à l'implémenteur de mesurer tôt (`git diff --numstat <base>`, `git add -N` pour les
  nouveaux fichiers) ; découper en branches empilées qui compilent seules, quatre ici (#326 à #329).

## Un worktree créé sur l'hôte, illisible dans la distrobox (2026-10-07)

- **Symptôme** : `git worktree remove ~/Projects/Levain-ui` répond « is not a working tree », puis « does not point
  back to .git/worktrees ».
- **Cause** : le worktree avait été créé depuis l'hôte, où `/home` est un lien vers `/var/home` (Fedora
  Atomic) ; git a écrit les chemins en `/var/home`, que la distrobox ne compare pas à `/home`.
- **Parade** : `git worktree repair <chemin>` depuis le dépôt principal, puis `git worktree remove`. Créer les
  worktrees dans la distrobox.

## Un ADR qui sert de spécification nomme ce que le code ne peut pas nommer (2026-10-07)

- **Symptôme** : l'ADR-0034 appelle le piège `fakeObject` ; clang-tidy (ADR-0009) impose `FakeObject` à une
  variable `inline constexpr`. Qui cherche le nom de l'ADR ne trouve pas le code.
- **Parade** : corriger l'ADR dans la PR qui écrit le code, et le signaler dans la PR.

## Les diagnostics de l'IDE dans un worktree neuf (2026-10-07)

- **Symptôme** : l'IDE signale des dizaines d'erreurs (« 'flecs.h' file not found ») sur des fichiers qui
  compilent.
- **Cause** : le serveur de langage n'a pas de `compile_commands.json` pour ce worktree, ni pour les prototypes du
  scratchpad.
- **Parade** : se fier au build. Pour clangd : `ln -sf build/linux-debug/compile_commands.json .` dans le worktree.
