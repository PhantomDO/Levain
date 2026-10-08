# Pièges de la boucle autonome

Chaque entrée : symptôme, cause, parade, date. La plus récente en haut.

## Un avis de fin traité comme sans suite : la nuit perdue (2026-10-08)

- **Symptôme** : Donnovan part la nuit (« continue comme ça, je reviens demain matin ») ; au matin, aucune PR n'a
  été ouverte depuis 00 h 40, et l'inspecteur n'a pas commencé.
- **Cause** : le workflow de la hiérarchie s'est terminé à 00 h 40 ; la session a répondu à son avis de fin sans
  rien lancer. Plus rien ne tournait pour la réveiller.
- **Parade** : en mode autonome, chaque avis de fin (workflow, agent, commande de fond) reçoit l'action suivante :
  lire, relire, ouvrir, lancer la suite. Avant de finir un tour, vérifier que quelque chose tourne encore et
  réveillera la session (un workflow, `merge-stack.sh` en tâche de fond), ou lancer la tâche suivante.

## Deux sessions, deux machines, un dépôt (2026-10-08)

- **Contexte** : une session sur la machine de référence (16 Go), une autre dans la distro WSL du PC de bureau
  (64 Go, `tools/wsl/`), sur un autre compte Claude.
- **Parade** : la règle n°1 vaut pour les deux : une seule session ouvre des PR à la fois. La passation se fait par
  le dépôt (ce skill, le JOURNAL), pas par la mémoire locale d'une session, qui ne suit pas d'une machine à
  l'autre. Une session voit l'autre par `ListAgents` et peut lui écrire (Remote Control).

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
