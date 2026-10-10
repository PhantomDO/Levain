---
name: pr-autonome
description: La boucle d'une PR en mode autonome, quand Donnovan n'est pas là pour relire — travailler dans un worktree, écrire (par un workflow si ultracode est actif), vérifier comme la CI, contre-tester chaque contrôle, faire relire par un subagent, corriger, ouvrir la PR d'un morceau vers la branche de sa fonctionnalité et la fusionner, puis la PR finale vers `main`, dont la CI est la porte. À utiliser pour chaque PR de code ou de docs en mode autonome, après le skill session.
---

# Une PR en mode autonome

Lire d'abord [`GOTCHA.md`](GOTCHA.md). Le mode autonome (AGENTS.md, décidé par Donnovan le 2026-10-04) : un subagent
relit chaque PR à sa place ; l'agent fusionne après corrections et vérification. La CI ne tourne qu'une fois par
fonctionnalité, sur la PR vers `main` (AGENTS.md, règle n°1). Ce skill est la boucle d'une PR ; le skill `session` reste
le rituel autour (journal, board, temps de Donnovan), le skill `build` les commandes.

## 1. Avant d'écrire

- Lire l'issue, l'ADR qui la porte, et les commentaires de passation de l'issue (`gh issue view N --comments`).
- **Estimer la taille** : au-delà de 400 lignes (règle n°2), prévoir le découpage dès le départ, une branche et une
  PR par morceau, qui compile et passe ses tests seul. Une PR découpée après coup coûte une passe de plus.
- **La branche de la fonctionnalité** existe d'abord (`m<phase>.<n>/<sujet>`, tirée de `main`) ; les morceaux en
  sont tirés et la visent (`m<phase>.<n>/<sujet>-<morceau>` : git refuse `<sujet>/<morceau>` à côté de `<sujet>`).

## 2. Où travailler

Un **worktree par fonctionnalité** quand un autre build tourne (une vérification, une fusion) : changer de branche
sous un build en cours le fausse.

```bash
git worktree add ~/Projects/Levain-<sujet> -b m<phase>.<n>/<sujet> <base>
```

Puis `tools/fetch-assets.sh` dans le worktree : les assets de test ne se partagent pas, et sans eux le build web
refuse de se configurer (`verify.sh` : « web : FAIL » ; build/GOTCHA.md). Ses dossiers `build/` sont à lui ; le
cache binaire de vcpkg (`~/.cache/vcpkg`) les remplit vite. À la fin :
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
  l'arbre, trois presets, `windows-debug` compilé (le winsysroot de la machine de référence :
  `tools/winsysroot.sh`), web, clang-tidy des fichiers changés (puis, par la base de `windows-debug`, tous ceux que
  Windows compile). `BASE=<branche de la fonctionnalité>` pour un morceau. Sans CI avant la fin, c'est lui, avec
  les tests Windows de la distro du portable (build/SKILL.md) quand le morceau touche Windows, qui tient lieu de
  porte : le lancer **complet** avant chaque push, et le dire dans la PR.
- **Chaque contrôle nouveau a son contre-test** : la faute injectée depuis une copie du fichier, le test rouge, la
  copie remise, le test vert (build/GOTCHA.md, « Contre-tests »). Le noter dans la PR.

## 5. Relire et corriger

- Un subagent relit la PR, avec des points précis : la couverture (rien d'oublié, rien de trop), les commentaires
  et la doc, les tests, les règles du projet. « Dire explicitement si rien ne bloque. »
- Ses corrections : **un commit de plus** ; pas de `--amend`, qui oblige à noter l'ancien SHA (session/GOTCHA.md).
  Puis `tools/verify.sh` à nouveau.

## 6. Ouvrir et fusionner

- Le modèle `.github/pull_request_template.md`, le guide de lecture d'abord ; le milestone ; « Partie de #N » ou
  « Closes #N ». Le texte d'une PR se change par l'API REST :
  `gh api -X PATCH repos/PhantomDO/Levain/pulls/N -F body=@fichier` ; `gh pr edit` échoue avec le `gh` 2.46 de la
  distro WSL (GOTCHA.md).
- **Un morceau** : `gh pr create --base <branche de la fonctionnalité>`, relu par le subagent, puis
  `gh pr merge N --merge` et `git push origin --delete <branche du morceau>` : aucune CI n'est lancée pour une base
  autre que `main`, il n'y a rien à attendre. Pas de `--delete-branch` : il supprime aussi la branche locale, qu'une
  vérification en cours peut lire (GOTCHA.md) ; les branches locales se suppriment à la fin (§ 2), quand plus rien ne
  s'en sert. Le suivant s'ouvre ensuite (règle n°1 : un seul à la fois).
- **La CI à la main**, avant une étape risquée : `gh workflow run ci.yml --ref <branche de la fonctionnalité>`, puis
  l'id du passage, `gh run list --workflow ci.yml --branch <b> --limit 1 --json databaseId` (`<b>` est la branche de la
  fonctionnalité), et `gh run watch <id> --exit-status` en tâche de fond (qui réveille la session à la fin), jamais par
  un `&` dans un shell. Un seul passage à la fois : les runners et le cache Windows (1 Go) sont comptés.
- **La PR finale**, fonctionnalité vers `main` : le guide de lecture renvoie aux PR des morceaux, qui ont été relus.
  `gh pr checks <N> --watch` en tâche de fond ; une fois tout vert, `gh pr merge <N> --merge`, puis
  `git push origin --delete <branche de la fonctionnalité>`. Puis, dans le dépôt principal : `git pull --ff-only`,
  `git worktree remove` de la fonctionnalité, et `git branch -d` de ses branches.
- Le board : l'issue passe en Done quand une PR la ferme ; « Passé (h) » quand Donnovan donne son temps.
