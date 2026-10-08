---
name: pr-autonome
description: La boucle d'une PR en mode autonome, quand Donnovan n'est pas là pour relire — travailler dans un worktree, écrire (par un workflow si ultracode est actif), vérifier comme la CI, contre-tester chaque contrôle, faire relire par un subagent, corriger, ouvrir une PR seule ou une pile, fusionner par merge-stack. À utiliser pour chaque PR de code ou de docs en mode autonome, après le skill session.
---

# Une PR en mode autonome

Lire d'abord [`GOTCHA.md`](GOTCHA.md). Le mode autonome (AGENTS.md, décidé par Donnovan le 2026-10-04) : un
subagent relit chaque PR à sa place ; l'agent fusionne après corrections et CI verte. Ce skill est la boucle d'une
PR ; le skill `session` reste le rituel autour (journal, board, temps de Donnovan), le skill `build` les commandes.

## 1. Avant d'écrire

- Lire l'issue, l'ADR qui la porte, et les commentaires de passation de l'issue (`gh issue view N --comments`).
- **Estimer la taille** : au-delà de 400 lignes (règle n°2), prévoir la pile dès le départ, une branche par étape
  qui compile et passe ses tests seule. Une PR découpée après coup coûte une passe de plus.

## 2. Où travailler

Un **worktree par pile** quand un autre build tourne (une vérification, une fusion) : changer de branche sous un
build en cours le fausse.

```bash
git worktree add ~/Projects/Levain-<sujet> -b m<phase>.<n>/<sujet> <base>
```

Ses dossiers `build/` sont à lui ; le cache binaire de vcpkg (`~/.cache/vcpkg`) les remplit vite. À la fin :
`git worktree remove`, puis `git branch -d` des branches fusionnées.

## 3. Écrire

- La forme du code (AGENTS.md) ; un test pour chaque comportement nouveau.
- Avec ultracode, un **workflow** : une implémentation, puis trois relectures adverses en parallèle — la justesse
  (sous ASan, UBSan, et GCC quand le code est générique), les règles du projet et la taille, les contrôles qui
  doivent pouvoir échouer et le build web —, puis une correction. Donner aux agents le chemin du worktree, « ne
  touchez à aucun autre », les sections de l'ADR, la passation, les prototypes s'il y en a.
- **Les modèles** (décidé par Donnovan le 2026-10-08) : l'implémentation sur Sonnet quand son périmètre est
  borné (l'ADR décidé, les fichiers, l'API, les tests et le budget de lignes écrits dans la demande) ; les
  relectures adverses, les ADR, les découpages et le débogage sur Opus. Mesuré sur l'inspecteur (#339 à #342) :
  Sonnet a livré en 25 minutes, mais sans tenir le budget de lignes : le donner comme une règle, et le mesurer.
- **Relire soi-même** le code qui en sort, avant d'ouvrir : un agent qui dit « vérifié » a pu vérifier autre chose.

## 4. Vérifier

- **Un build à la fois**, sur la machine de référence (16 Go) : jamais deux worktrees ni deux agents qui
  compilent ensemble ; les relecteurs d'un workflow, l'un après l'autre ; le dire à chaque agent dans sa demande
  (build/GOTCHA.md, « 16 Go de RAM »). Sur la distro WSL de 64 Go (`tools/wsl/`), la limite se lève.
- `tools/verify.sh` avant **chaque** push, y compris après une petite correction (build/GOTCHA.md) : format de tout
  l'arbre, trois presets, clang-tidy des fichiers changés, web. `BASE=<précédente>` pour une PR empilée.
- **Chaque contrôle nouveau a son contre-test** : la faute injectée depuis une copie du fichier, le test rouge, la
  copie remise, le test vert (build/GOTCHA.md, « Contre-tests »). Le noter dans la PR.

## 5. Relire et corriger

- Un subagent relit la PR, avec des points précis : la couverture (rien d'oublié, rien de trop), les commentaires
  et la doc, les tests, les règles du projet. « Dire explicitement si rien ne bloque. »
- Ses corrections : **un commit de plus**, propagé aussitôt aux branches suivantes d'une pile
  (`git rebase --update-refs <branche> <sommet>`) ; pas de `--amend`, qui oblige à noter l'ancien SHA
  (session/GOTCHA.md). Puis `tools/verify.sh` à nouveau.

## 6. Ouvrir et fusionner

- Le modèle `.github/pull_request_template.md`, le guide de lecture d'abord ; le milestone ; « Partie de #N » ou
  « Closes #N ». Une pile : `gh pr create --base <branche précédente>`, dans l'ordre.
- `tools/merge-stack.sh N [N+1 …]` en arrière-plan, par l'outil de tâches de fond (qui réveille la session à la
  fin), jamais par un `&` dans un shell : il attend les checks de chacune, fusionne en merge commit,
  passe la base de la suivante à `main`. Puis, dans le dépôt principal : `git pull --ff-only`.
- Le board : l'issue passe en Done quand une PR la ferme ; « Passé (h) » quand Donnovan donne son temps.
