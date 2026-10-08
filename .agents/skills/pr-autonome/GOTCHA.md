# Pièges de la boucle autonome

Chaque entrée : symptôme, cause, parade, date. La plus récente en haut.

## `merge-stack.sh` arrêté par GitHub, deux fois dans la journée (2026-10-08)

- **Symptômes** : « #348 : checks pas verts (error connecting to api.github.com) » alors que la CI tournait ;
  puis, une pile plus loin, « #351 : base non changée » après la fusion de #350.
- **Causes** : une coupure réseau, que `gh pr checks` rend comme une sortie en erreur ; et `gh pr edit --base`, qui
  échoue avec le `gh` 2.46 d'Ubuntu 26.04 (celui de la distro WSL) sur une erreur GraphQL (« Projects (classic) is
  being deprecated ») : cette version interroge encore les anciens Projects, que GitHub a retirés de son API ; le
  `gh` récent de la machine de référence n'a pas le problème. Le script s'arrêtait proprement les deux fois, la
  branche gardée.
- **Parade** : le script attend 10 min au plus quand GitHub est injoignable, et change la base par l'API REST
  (`gh api -X PATCH repos/{owner}/{repo}/pulls/N -f base=main`). Contre-testé avec un faux `gh` dans le `PATH`.
  Pour rattraper une pile arrêtée : changer la base par l'API REST, supprimer la branche gardée
  (`git push origin --delete <branche>`), puis relancer le script sur la suite.

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
