# Pièges de la boucle autonome

Chaque entrée : symptôme, cause, parade, date. La plus récente en haut.

## Une PR annoncée sous 400 lignes en fait 1 081 (2026-10-07)

- **Symptôme** : la PR de `reflection.hpp` (M7.2), prévue d'un bloc par l'ADR-0034, sort du workflow à 1 081 lignes,
  deux fois et demie la règle n°2 ; la parade prévue par l'ADR (sortir les tests de refus à la compilation) ne
  suffit pas.
- **Cause** : l'estimation comptait l'en-tête, pas les tests de refus (un lancement par cas), le README, le GOTCHA,
  ni le script de l'échelle.
- **Parade** : demander à l'implémenteur de mesurer tôt (`git diff --numstat <base>`, `git add -N` pour les nouveaux
  fichiers) ; découper en branches empilées qui compilent seules, quatre ici (#326 à #329).

## Un worktree créé sur l'hôte, illisible dans la distrobox (2026-10-07)

- **Symptôme** : `git worktree remove ~/Projects/Levain-ui` répond « is not a working tree », puis « does not point
  back to .git/worktrees ».
- **Cause** : le worktree avait été créé depuis l'hôte, où `/home` est un lien vers `/var/home` (Fedora
  Atomic) ; git a écrit les chemins en `/var/home`, que la distrobox ne compare pas à `/home`.
- **Parade** : `git worktree repair <chemin>` depuis le dépôt principal, puis `git worktree remove`. Créer les
  worktrees dans la distrobox.

## Un ADR qui sert de spécification nomme ce que le code ne peut pas nommer (2026-10-07)

- **Symptôme** : l'ADR-0034 appelle le piège `fakeObject` ; clang-tidy (ADR-0009) impose `FakeObject` à une variable
  `inline constexpr`. Qui cherche le nom de l'ADR ne trouve pas le code.
- **Parade** : corriger l'ADR dans la PR qui écrit le code, et le signaler dans la PR.

## Les diagnostics de l'IDE dans un worktree neuf (2026-10-07)

- **Symptôme** : l'IDE signale des dizaines d'erreurs (« 'flecs.h' file not found ») sur des fichiers qui compilent.
- **Cause** : le serveur de langage n'a pas de `compile_commands.json` pour ce worktree, ni pour les prototypes du
  scratchpad.
- **Parade** : se fier au build. Pour clangd : `ln -sf build/linux-debug/compile_commands.json .` dans le worktree.
